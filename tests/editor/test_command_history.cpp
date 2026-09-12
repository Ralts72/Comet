#include "command_history.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene_serializer.h"

#include <gtest/gtest.h>
#include <limits>

namespace {
    using namespace Comet;
    using namespace CometEditor;

    class CommandHistoryTest: public ::testing::Test {
    protected:
        ComponentRegistry registry = create_scene_component_registry();
        Scene scene;
        Entity entity = scene.create_entity("Edited");
        CommandHistory history{2};
        PropertyEditTransaction edit{history, registry};

        void SetUp() override { history.bind_scene(&scene); }
        PropertyEditTransaction::Target translation() const {
            return {entity.get_uuid(), "transform", "translation"};
        }
        float x() { return entity.get_component<TransformComponent>().translation.x; }
        void change(float value) {
            ASSERT_TRUE(edit.begin(translation()));
            ASSERT_TRUE(edit.preview(Math::Vec3(value, 0, 0)));
            ASSERT_TRUE(edit.commit());
        }
    };

    TEST_F(CommandHistoryTest, DiscreteApplyFinishesPreviousGestureAndRejectsWrongType) {
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(2, 0, 0)));
        EXPECT_FALSE(edit.apply(translation(), std::string("wrong type")));
        EXPECT_FALSE(edit.active());
        EXPECT_FLOAT_EQ(x(), 2);
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(edit.apply(translation(), Math::Vec3(5, 0, 0)));
        EXPECT_FALSE(edit.active());
        EXPECT_EQ(history.undo_size(), 2);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 2);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
    }

    TEST_F(CommandHistoryTest, PreviewsManyChangesButRecordsOneGesture) {
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(1, 0, 0)));
        ASSERT_TRUE(edit.preview(Math::Vec3(3, 0, 0)));
        EXPECT_FLOAT_EQ(x(), 3);
        EXPECT_EQ(history.undo_size(), 0);
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(x(), 3);
    }

    TEST_F(CommandHistoryTest, NameTransactionSharesHistoryAndPreservesSceneFormat) {
        const PropertyEditTransaction::Target name{entity.get_uuid(), "name", "name"};
        ASSERT_TRUE(edit.begin(name));
        ASSERT_TRUE(edit.preview(std::string("Renamed")));
        ASSERT_TRUE(edit.preview(std::string("长名称: ") + std::string(512, 'x')));
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(history.undo_size(), 1);
        const auto after = entity.get_component<NameComponent>().name;
        const SceneSerializer serializer(registry);
        const auto loaded = serializer.deserialize(serializer.serialize(scene));
        EXPECT_EQ(
            loaded->find_entity(entity.get_uuid()).get_component<NameComponent>().name,
            after);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Edited");
        ASSERT_TRUE(edit.begin(name));
        ASSERT_TRUE(edit.preview(std::string("Cancelled")));
        ASSERT_TRUE(edit.cancel());
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Edited");
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<NameComponent>().name, after);
    }

    TEST_F(CommandHistoryTest, CancelAndNoOpKeepRedoBranch) {
        change(2);
        ASSERT_TRUE(history.undo());
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(5, 0, 0)));
        ASSERT_TRUE(edit.cancel());
        EXPECT_FLOAT_EQ(x(), 0);
        EXPECT_EQ(history.redo_size(), 1);
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(0)));
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(history.undo_size(), 0);
        EXPECT_EQ(history.redo_size(), 1);
    }

    TEST_F(CommandHistoryTest, NewCommittedEditDropsRedoAndCapacityEvictsOldest) {
        change(1);
        change(2);
        change(3);
        EXPECT_EQ(history.undo_size(), 2);
        ASSERT_TRUE(history.undo());
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 1);
        EXPECT_FALSE(history.undo());
        change(9);
        EXPECT_FALSE(history.can_redo());
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 1);
    }

    TEST_F(CommandHistoryTest, ResetsHistoryAndInvalidatesGestureAcrossSceneReplacement) {
        change(1);
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(2, 0, 0)));
        Scene replacement;
        auto same_uuid = replacement.create_entity_with_uuid(entity.get_uuid(), "Other");
        history.bind_scene(&replacement);
        EXPECT_FALSE(edit.active());
        ASSERT_TRUE(edit.cancel());
        EXPECT_FALSE(history.can_undo());
        EXPECT_EQ(
            same_uuid.get_component<TransformComponent>().translation, Math::Vec3(0));
        EXPECT_FLOAT_EQ(x(), 2);
        history.bind_scene(nullptr);
        EXPECT_FALSE(edit.begin(translation()));
        EXPECT_FALSE(history.undo());
    }

    TEST_F(CommandHistoryTest, ExplicitRebindClearsEvenWhenAddressIsUnchanged) {
        change(1);
        history.bind_scene(&scene);
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(CommandHistoryTest, DeletedTargetFailsWithoutMovingHistoryCursor) {
        change(1);
        scene.destroy_entity(entity);
        EXPECT_FALSE(history.undo());
        EXPECT_EQ(history.undo_size(), 1);
        EXPECT_EQ(history.redo_size(), 0);
    }

    TEST_F(CommandHistoryTest, TargetUsesUuidRatherThanTransientEntityId) {
        change(7);
        const auto uuid = entity.get_uuid();
        scene.destroy_entity(entity);
        entity = scene.create_entity_with_uuid(uuid, "Restored");
        ASSERT_TRUE(history.undo());
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(x(), 7);
    }

    TEST_F(CommandHistoryTest, RejectsWrongTypesNonFiniteValuesAndMissingProperties) {
        EXPECT_FALSE(edit.begin({entity.get_uuid(), "transform", "missing"}));
        ASSERT_TRUE(edit.begin(translation()));
        EXPECT_FALSE(edit.preview(2.0f));
        EXPECT_FALSE(edit.preview(Math::Vec3(std::numeric_limits<float>::quiet_NaN())));
        EXPECT_FLOAT_EQ(x(), 0);
        ASSERT_TRUE(edit.commit());
        EXPECT_FALSE(history.can_undo());
    }

    TEST_F(CommandHistoryTest, NormalizationIsSharedByPreviewUndoAndRedo) {
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "transform", "rotation"}));
        ASSERT_TRUE(edit.preview(Math::Vec3(450, 0, 0)));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.x, 90);
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.x, 0);
        ASSERT_TRUE(history.redo());
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.x, 90);
    }

    TEST_F(CommandHistoryTest, CameraAndAssetReferenceEditsUseSameHistory) {
        entity.add_component<CameraComponent>();
        entity.add_component<MeshRendererComponent>();
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "camera", "primary"}));
        ASSERT_TRUE(edit.preview(true));
        ASSERT_TRUE(edit.commit());
        EXPECT_TRUE(entity.get_component<CameraComponent>().primary);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<CameraComponent>().primary);
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "camera", "fov"}));
        ASSERT_TRUE(edit.preview(60.0f));
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(entity.get_component<CameraComponent>().fov, 45);
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "mesh_renderer", "mesh"}));
        ASSERT_TRUE(edit.preview(AssetHandle(42)));
        ASSERT_TRUE(edit.commit());
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(entity.get_component<MeshRendererComponent>().mesh);
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(entity.get_component<MeshRendererComponent>().mesh, AssetHandle(42));
    }

    TEST_F(CommandHistoryTest, AssignmentRejectsReadOnlyAndNormalizesExactlyOnce) {
        CameraComponent camera;
        int notifications = 0;
        auto property = make_property_descriptor("fov", "FOV", &CameraComponent::fov, {},
            [&notifications](float&) { ++notifications; });
        property.read_only = true;
        EXPECT_FALSE(property.assign_value(&camera, 60.0f));
        EXPECT_EQ(notifications, 0);
        EXPECT_FLOAT_EQ(camera.fov, 45);
        property.read_only = false;
        EXPECT_FALSE(property.assign_value(nullptr, 60.0f));
        EXPECT_FALSE(property.assign_value(&camera, true));
        EXPECT_FALSE(
            property.assign_value(&camera, std::numeric_limits<float>::infinity()));
        ASSERT_TRUE(property.assign_value(&camera, 60.0f));
        EXPECT_EQ(notifications, 1);
        EXPECT_FLOAT_EQ(camera.fov, 60);
    }

    TEST_F(CommandHistoryTest, SwitchingPropertyCommitsPreviousGesture) {
        ASSERT_TRUE(edit.begin(translation()));
        ASSERT_TRUE(edit.preview(Math::Vec3(2, 0, 0)));
        ASSERT_TRUE(edit.begin({entity.get_uuid(), "transform", "scale"}));
        EXPECT_EQ(history.undo_size(), 1);
        ASSERT_TRUE(edit.preview(Math::Vec3(4)));
        ASSERT_TRUE(edit.cancel());
        EXPECT_EQ(entity.get_component<TransformComponent>().scale, Math::Vec3(1));
        ASSERT_TRUE(history.undo());
        EXPECT_FLOAT_EQ(x(), 0);
    }

    TEST_F(CommandHistoryTest, CommittedTransformFeedsCurrentSnapshotAndSave) {
        entity.add_component<MeshRendererComponent>();
        change(4);
        const auto snapshot = SceneExtractor::extract(scene);
        ASSERT_EQ(snapshot.render_items.size(), 1);
        EXPECT_FLOAT_EQ(snapshot.render_items[0].model_matrix[3].x, 4);
        SceneSerializer serializer(registry);
        const auto clone = serializer.clone(scene);
        EXPECT_FLOAT_EQ(clone->find_entity(entity.get_uuid())
                            .get_component<TransformComponent>()
                            .translation.x,
            4);
    }

    class SetValueCommand final: public CommandHistory::Command {
    public:
        explicit SetValueCommand(int& value, bool fail = false)
            : m_value(value), m_fail(fail) {}
        bool undo(Scene&) override {
            m_value = 0;
            return true;
        }
        bool redo(Scene&) override {
            if(m_fail)
                return false;
            m_value = 1;
            return true;
        }

    private:
        int& m_value;
        bool m_fail;
    };

    TEST_F(CommandHistoryTest, FailedExecutePreservesRedoAndDoesNotRecord) {
        int value = 0;
        ASSERT_TRUE(history.execute(std::make_unique<SetValueCommand>(value)));
        EXPECT_EQ(value, 1);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(history.execute(std::make_unique<SetValueCommand>(value, true)));
        EXPECT_EQ(value, 0);
        EXPECT_EQ(history.redo_size(), 1);
        EXPECT_FALSE(history.execute(nullptr));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(value, 1);
    }
}
