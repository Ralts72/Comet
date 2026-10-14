#pragma once

#include "animation/animation_controller.h"
#include "asset/handle.h"
#include "scene/entity_uuid.h"

#include <imgui.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CometEditor {
    class AnimationPreview;

    // 只编辑传入草稿及编辑器布局；资产读写、场景姿态发布仍由宿主负责。
    class AnimationControllerEditor {
    public:
        struct Model {
            Comet::EntityUuid entity;
            std::string name;
            Comet::AssetHandle controller;
            std::shared_ptr<const Comet::AnimationData> animation;
            std::shared_ptr<const Comet::AnimationPlayback> playback;
            Comet::ParameterMap parameters;
        };
        enum class Action { None, Save, Discard };

        void reset(const Comet::AnimationController& data);
        void set_models(std::vector<Model> models, Comet::AssetHandle controller, bool runtime);
        [[nodiscard]] const Model* model() const;
        [[nodiscard]] bool is_open() const { return m_open; }
        void open() {
            m_open = true;
            m_focus = true;
        }
        void close() { m_open = false; }
        [[nodiscard]] bool can_undo() const { return !m_undo.empty(); }
        [[nodiscard]] bool can_redo() const { return !m_redo.empty(); }
        bool undo(Comet::AnimationController& data);
        bool redo(Comet::AnimationController& data);
        void render_details(Comet::AnimationController& data);
        Action render_window(Comet::AnimationController& data, bool dirty,
            AnimationPreview* preview, std::string_view asset_name = {});

    private:
        void track_edit(
            const Comet::AnimationController& before, const Comet::AnimationController& after);
        void select_state(std::size_t index);
        void render_parameters(Comet::AnimationController& data);
        void render_state(Comet::AnimationController& data, std::size_t index);
        void render_events(Comet::AnimationController::State& state);
        void render_transition(Comet::AnimationController& data, std::size_t index);
        void render_parameter(Comet::AnimationController& data, const std::string& name);
        void render_graph(Comet::AnimationController& data);
        void render_preview(const Comet::AnimationController& data, AnimationPreview* preview);
        void rename_state(Comet::AnimationController& data, std::size_t index, std::string name);
        void remove_state(Comet::AnimationController& data, std::size_t index);
        void clip_choice(const char* label, std::string& name);
        void arrange(const Comet::AnimationController& data);

        bool m_open = false;
        bool m_focus = false;
        bool m_runtime = false;
        Comet::AssetHandle m_controller;
        std::string m_error;
        std::optional<std::size_t> m_state;
        std::optional<std::size_t> m_event;
        std::optional<std::size_t> m_preview_index;
        std::optional<std::size_t> m_transition;
        std::string m_parameter;
        std::optional<std::string> m_connect_from;
        std::map<std::string, ImVec2> m_positions;
        ImVec2 m_pan{35, 40};
        float m_zoom = 1;
        std::vector<Model> m_models;
        Comet::EntityUuid m_model;
        Comet::ParameterMap m_preview_values;
        std::string m_preview_state;
        std::vector<Comet::AnimationController> m_undo;
        std::vector<Comet::AnimationController> m_redo;
        std::optional<Comet::AnimationController> m_edit_before;
    };
}
