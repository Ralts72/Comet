#pragma once

namespace CometEditor {
    enum class SceneRequestKind {
        None,
        FileDialog,
        Menu,
        Play,
        Structure,
        Rename,
        MeshDrop,
        AssetAssignment
    };

    struct SceneRequestAvailability {
        bool file_dialog = false;
        bool menu = false;
        bool play = false;
        bool structure = false;
        bool rename = false;
        bool mesh_drop = false;
        bool asset_assignment = false;
        bool document_pending = false;
        bool dialog_cancelled = false;
    };

    [[nodiscard]] constexpr SceneRequestKind select_scene_request(
        const SceneRequestAvailability& requests) {
        if(requests.dialog_cancelled)
            return SceneRequestKind::None;
        // 未保存确认期间仍需允许文件弹窗提交 Save；其余旧请求全部丢弃。
        if(requests.file_dialog)
            return SceneRequestKind::FileDialog;
        if(requests.document_pending)
            return SceneRequestKind::None;
        if(requests.menu)
            return SceneRequestKind::Menu;
        if(requests.play)
            return SceneRequestKind::Play;
        if(requests.structure)
            return SceneRequestKind::Structure;
        if(requests.rename)
            return SceneRequestKind::Rename;
        if(requests.mesh_drop)
            return SceneRequestKind::MeshDrop;
        if(requests.asset_assignment)
            return SceneRequestKind::AssetAssignment;
        return SceneRequestKind::None;
    }
}
