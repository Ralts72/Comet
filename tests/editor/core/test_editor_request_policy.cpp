#include "scene/editor_request_policy.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(EditorRequestPolicyTest, SelectsOneRequestInExplicitPriorityOrder) {
        SceneRequestAvailability requests{
            .file_dialog = true,
            .menu = true,
            .play = true,
            .structure = true,
            .rename = true,
            .mesh_drop = true,
            .asset_assignment = true,
        };
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::FileDialog);
        requests.file_dialog = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::Menu);
        requests.menu = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::Play);
        requests.play = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::Structure);
        requests.structure = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::Rename);
        requests.rename = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::MeshDrop);
        requests.mesh_drop = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::AssetAssignment);
        requests.asset_assignment = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::None);
    }

    TEST(EditorRequestPolicyTest, PendingDocumentOnlyAcceptsDialogSubmission) {
        SceneRequestAvailability requests{
            .file_dialog = true,
            .menu = true,
            .play = true,
            .document_pending = true,
        };
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::FileDialog);
        requests.file_dialog = false;
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::None);
    }

    TEST(EditorRequestPolicyTest, DialogCancellationSuppressesEveryOtherRequest) {
        const SceneRequestAvailability requests{
            .file_dialog = true,
            .play = true,
            .dialog_cancelled = true,
        };
        EXPECT_EQ(select_scene_request(requests), SceneRequestKind::None);
    }
}
