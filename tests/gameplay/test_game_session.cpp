#include "gameplay/game_session.h"
#include "gameplay/checkpoint_store.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include <limits>
#include "scene/component_registry.h"
#include "scene/scene.h"
#include <gtest/gtest.h>

namespace Comet::Tests {
    TEST(GameSessionTest, LoadsLevelsAndRestartsAcceptedEntryWithoutKeepingRuntimeEdits) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        GameSession game(serializer);
        auto first = std::make_unique<Scene>();
        const auto first_id = first->create_entity("First").get_uuid();
        game.begin(std::move(first), "scenes/first.scene");
        auto initial = game.prepare({});
        ASSERT_TRUE(initial) << initial.error();
        EXPECT_TRUE(initial.value().scene->find_entity(first_id));
        EXPECT_TRUE(initial.value().values.empty());
        auto active = std::move(initial.value().scene);
        active->create_entity("Runtime only");
        game.accept(std::move(initial).value());

        RuntimeSession::SceneRequest request{RuntimeSession::SceneRequest::Kind::Load,
            "scenes/second.scene", {{"score", 7.f}, {"entry", std::string("west")}}};
        EntityUuid second_id;
        const auto load = [&](std::string_view path) {
            EXPECT_EQ(path, "scenes/second.scene");
            auto scene = std::make_unique<Scene>();
            second_id = scene->create_entity("Second").get_uuid();
            return Result<std::unique_ptr<Scene>>::success(std::move(scene));
        };
        auto discarded = game.prepare(request, load);
        ASSERT_TRUE(discarded);
        EXPECT_EQ(game.scene_path(), "scenes/first.scene");
        EXPECT_EQ(game.prepare({}).value().scene->entity_count(), 1);
        auto prepared = game.prepare(request, load);
        ASSERT_TRUE(prepared);
        EXPECT_EQ(prepared.value().values, request.values);
        active = std::move(prepared.value().scene);
        game.accept(std::move(prepared).value());
        EXPECT_EQ(game.scene_path(), "scenes/second.scene");
        active->destroy_entity(active->find_entity(second_id));
        active->create_entity("Second runtime only");
        auto restarted = game.prepare({});
        ASSERT_TRUE(restarted);
        EXPECT_TRUE(restarted.value().scene->find_entity(second_id));
        EXPECT_EQ(restarted.value().scene->entity_count(), 1);
        EXPECT_EQ(restarted.value().values, request.values);
        EXPECT_EQ(restarted.value().path, "scenes/second.scene");
        auto failed = game.prepare(request, [](std::string_view) {
            return Result<std::unique_ptr<Scene>>::failure("Missing level");
        });
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error(), "Missing level");
        EXPECT_TRUE(game.prepare({}).value().scene->find_entity(second_id));
        game.end();
        EXPECT_FALSE(game.prepare({}));
        EXPECT_TRUE(game.scene_path().empty());
    }

    TEST(GameSessionTest, EditorBorrowsEditSceneAndInvalidRequestsNeverReplaceItsBaseline) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        GameSession game(serializer);
        Scene edit;
        auto entity = edit.create_entity("Unsaved edit");
        game.begin(edit);
        auto play = game.prepare({});
        ASSERT_TRUE(play);
        EXPECT_NE(play.value().scene.get(), &edit);
        EXPECT_EQ(
            play.value().scene->find_entity(entity.get_uuid()).get_component<NameComponent>().name,
            "Unsaved edit");
        RuntimeSession::SceneRequest request{
            RuntimeSession::SceneRequest::Kind::Load, "next.scene", {}};
        EXPECT_FALSE(game.prepare(request));
        EXPECT_FALSE(game.prepare(request,
            [](std::string_view) { return Result<std::unique_ptr<Scene>>::success(nullptr); }));
        request.values["invalid"] = EntityUuid::generate();
        bool called = false;
        EXPECT_FALSE(game.prepare(request, [&](std::string_view) {
            called = true;
            return Result<std::unique_ptr<Scene>>::success(std::make_unique<Scene>());
        }));
        EXPECT_FALSE(called);
        EXPECT_TRUE(game.prepare({}));
        EXPECT_EQ(edit.entity_count(), 1);
    }

    TEST(CheckpointStoreTest, ReopeningRestoresTypedProgressAndInvalidWritesPreservePreviousSave) {
        const TemporaryDirectory directory;
        const auto path = directory.path() / "saves/checkpoint.json";
        const auto project = Uuid::generate();
        CheckpointStore store(project, path);
        const auto missing = store.load();
        ASSERT_TRUE(missing);
        EXPECT_FALSE(missing.value());
        const Checkpoint checkpoint{AssetHandle{123}, "西门",
            {{"score", 7.f}, {"open", true}, {"name", std::string("角色")},
                {"spawn", Math::Vec3(1, 2, 3)}}};
        ASSERT_TRUE(store.save(checkpoint));
        const auto contents = read_text_file(path);
        ASSERT_TRUE(contents);
        const auto loaded = CheckpointStore(project, path).load();
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(loaded.value());
        EXPECT_EQ(loaded.value()->scene, checkpoint.scene);
        EXPECT_EQ(loaded.value()->marker, checkpoint.marker);
        EXPECT_EQ(loaded.value()->values, checkpoint.values);
        EXPECT_FALSE(CheckpointStore(Uuid::generate(), path).load());
        EXPECT_FALSE(CheckpointStore({}, path).save(checkpoint));
        auto invalid = checkpoint;
        for(const auto& value : {ParameterValue{std::numeric_limits<float>::infinity()},
                ParameterValue{Uuid::generate()}, ParameterValue{Math::Vec4(1)},
                ParameterValue{std::string(1, char(0xff))},
                ParameterValue{std::string(4097, 'x')}}) {
            invalid.values = {{"invalid", value}};
            EXPECT_FALSE(store.save(invalid));
            EXPECT_EQ(read_text_file(path).value(), contents.value());
        }
        invalid = checkpoint;
        invalid.marker.clear();
        EXPECT_FALSE(store.save(invalid));
        invalid.marker = std::string(129, 'x');
        EXPECT_FALSE(store.save(invalid));
        invalid = checkpoint;
        invalid.scene = {};
        EXPECT_FALSE(store.save(invalid));
        EXPECT_EQ(read_text_file(path).value(), contents.value());
        EXPECT_FALSE(CheckpointStore(project, path / "blocked.json").save(checkpoint));
        EXPECT_EQ(read_text_file(path).value(), contents.value());
    }

    TEST(CheckpointStoreTest, CorruptOrUnsupportedSavesAreReportedWithoutRewritingThem) {
        const TemporaryDirectory directory;
        const auto path = directory.path() / "checkpoint.json";
        const auto project = Uuid::generate();
        CheckpointStore store(project, path);
        const auto prefix = std::string(R"({"version":1,"project":")") + project.to_string()
                            + R"(","scene":123,"checkpoint":"gate",)";
        const std::vector<std::string> corrupt{"{", prefix + R"("values":{"score":1,"score":2}})",
            prefix + R"("values":{"score":1,"\u0073core":2}})",
            prefix + R"("values":{"position":[1,2]}})", prefix + R"("values":{"score":null}})",
            prefix + R"("values":{"position":[1,2,3,4]}})",
            prefix + R"("values":{"position":[1,2,1e100]}})",
            prefix + R"("values":{"target":{"entity":"00000000-0000-0000-0000-000000000000"}}})",
            prefix + R"("values":{"score":1e100}})", prefix + R"("values":{},"unexpected":true})",
            prefix + R"("values":{},"version":2})",
            std::string(R"({"version":2,"project":")") + project.to_string()
                + R"(","scene":123,"checkpoint":"gate","values":{}})"};
        for(const auto& contents : corrupt) {
            ASSERT_TRUE(write_text_file_atomic(path, contents));
            EXPECT_FALSE(store.load()) << contents;
            EXPECT_EQ(read_text_file(path).value(), contents);
        }
        ASSERT_TRUE(store.save({AssetHandle{123}, "repaired", {}}));
        ASSERT_TRUE(store.load());
        EXPECT_EQ(store.load().value()->marker, "repaired");
    }

    TEST(GameSessionTest, NewSessionContinuesSavedSceneAfterRenameAndRestartRetainsEntryProgress) {
        const TemporaryDirectory directory;
        const auto project = Uuid::generate();
        const auto path = directory.path() / "checkpoint.json";
        const AssetHandle handle{789};
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        std::string current_path = "scenes/second.scene";
        bool exists = true;
        const auto find = [&](std::string_view name) {
            return exists && name == current_path ? handle : AssetHandle{};
        };
        const auto locate = [&](AssetHandle id) -> std::optional<std::string> {
            if(exists && id == handle)
                return current_path;
            return std::nullopt;
        };
        {
            CheckpointStore store(project, path);
            GameSession original(serializer);
            original.set_persistence(store.persistence(find, locate));
            EXPECT_FALSE(original.save_checkpoint({"gate", {}}));
            original.begin(std::make_unique<Scene>(), current_path);
            ASSERT_TRUE(original.save_checkpoint({"gate", {{"score", 7.f}}}));
            original.end();
        }
        // 同一持久身份现在指向改名后的场景，模拟下一次启动。
        current_path = "scenes/renamed.scene";
        CheckpointStore reopened(project, path);
        GameSession resumed(serializer);
        resumed.set_persistence(reopened.persistence(find, locate));
        resumed.begin(std::make_unique<Scene>(), "scenes/first.scene");
        const auto load = [&](std::string_view name) {
            EXPECT_EQ(name, current_path);
            auto scene = std::make_unique<Scene>();
            scene->create_entity("Saved level");
            return Result<std::unique_ptr<Scene>>::success(std::move(scene));
        };
        const RuntimeSession::SceneRequest next{RuntimeSession::SceneRequest::Kind::Continue};
        auto candidate = resumed.prepare(next, load);
        ASSERT_TRUE(candidate) << candidate.error();
        EXPECT_EQ(candidate.value().path, current_path);
        EXPECT_EQ(candidate.value().checkpoint, "gate");
        EXPECT_EQ(candidate.value().values, (ParameterMap{{"score", 7.f}}));
        EXPECT_EQ(resumed.scene_path(), "scenes/first.scene");
        auto active = std::move(candidate.value().scene);
        resumed.accept(std::move(candidate).value());
        active->create_entity("Runtime only");
        const auto restart = resumed.prepare({});
        ASSERT_TRUE(restart);
        EXPECT_EQ(restart.value().scene->entity_count(), 1);
        EXPECT_EQ(restart.value().checkpoint, "gate");
        EXPECT_EQ(restart.value().values, (ParameterMap{{"score", 7.f}}));
        exists = false;
        EXPECT_FALSE(resumed.prepare(next, load));
        EXPECT_EQ(resumed.scene_path(), current_path);
        EXPECT_EQ(resumed.prepare({}).value().scene->entity_count(), 1);
        resumed.end();
        resumed.begin(std::make_unique<Scene>(), "scenes/first.scene");
        EXPECT_TRUE(resumed.prepare({}).value().checkpoint.empty());
        EXPECT_TRUE(resumed.prepare({}).value().values.empty());
    }
}
