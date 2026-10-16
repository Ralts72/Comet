#include "animation/animation_controller_editor.h"
#include "inspector/animation_preview.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace CometEditor {
    namespace {
        using Controller = Comet::AnimationController;
        ImVec2 add(ImVec2 a, ImVec2 b) {
            return {a.x + b.x, a.y + b.y};
        }
        ImVec2 scale(ImVec2 a, float factor) {
            return {a.x * factor, a.y * factor};
        }
        void value_widget(const char* label, Comet::ParameterValue& value) {
            if(auto* boolean = std::get_if<bool>(&value))
                ImGui::Checkbox(label, boolean);
            else if(auto* text = std::get_if<std::string>(&value))
                Ui::input_text(label, *text);
            else
                ImGui::DragFloat(label, &std::get<float>(value), .05f);
        }
        void state_choice(const char* label, std::string& name, const Controller& data, bool any) {
            if(ImGui::BeginCombo(label, name.empty() ? "任意状态" : name.c_str())) {
                if(any && ImGui::Selectable("任意状态", name.empty()))
                    name.clear();
                for(const auto& state : data.states)
                    if(ImGui::Selectable(state.name.c_str(), state.name == name))
                        name = state.name;
                ImGui::EndCombo();
            }
        }
        std::string number_text(float value) {
            std::array<char, 32> text{};
            std::snprintf(text.data(), text.size(), "%.3g", double(value));
            return text.data();
        }
        std::string condition_summary(const Controller::Transition& transition) {
            std::string result = transition.after_finished ? "播放完成" : "";
            constexpr std::array operators{" = ", " ≠ ", " > ", " ≥ ", " < ", " ≤ "};
            for(const auto& condition : transition.conditions) {
                if(!result.empty())
                    result += " 且 ";
                result += condition.parameter + operators.at(std::size_t(condition.compare));
                if(const auto* boolean = std::get_if<bool>(&condition.value))
                    result += *boolean ? "是" : "否";
                else {
                    result += number_text(std::get<float>(condition.value));
                }
            }
            return result.empty() ? "无条件" : result;
        }
        float distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) {
            const float x = b.x - a.x, y = b.y - a.y;
            const float length = x * x + y * y;
            const float t =
                length > 0 ? std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / length, 0.f, 1.f) : 0;
            return std::hypot(p.x - a.x - t * x, p.y - a.y - t * y);
        }
        ImVec2 bezier(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, float t) {
            const float s = 1 - t;
            return add(add(scale(a, s * s * s), scale(b, 3 * s * s * t)),
                add(scale(c, 3 * s * t * t), scale(d, t * t * t)));
        }
    }

    void AnimationControllerEditor::reset(const Controller& data) {
        m_state.reset();
        m_event.reset();
        const auto found = std::ranges::find(data.states, data.initial, &Controller::State::name);
        if(found != data.states.end())
            m_state = std::size_t(found - data.states.begin());
        m_preview_index = m_state;
        m_transition.reset();
        m_parameter.clear();
        m_connect_from.reset();
        m_preview_values = data.parameters;
        m_preview_state.clear();
        m_error.clear();
        m_model = {};
        m_undo.clear();
        m_redo.clear();
        m_edit_before.reset();
        arrange(data);
        open();
    }

    void AnimationControllerEditor::arrange(const Controller& data) {
        m_positions.clear();
        m_positions[""] = {0, 0};
        for(std::size_t i = 0; i < data.states.size(); ++i)
            m_positions[data.states[i].name] = {float(i % 3) * 250, 125 + float(i / 3) * 160};
        m_pan = {35, 40};
        m_zoom = 1;
    }

    void AnimationControllerEditor::set_models(
        std::vector<Model> models, Comet::AssetHandle controller, bool runtime) {
        m_models = std::move(models);
        m_runtime = runtime;
        m_controller = controller;
        if(model())
            return;
        const auto found = std::ranges::find(m_models, controller, &Model::controller);
        if(found != m_models.end())
            m_model = found->entity;
        else
            m_model = m_models.empty() ? Comet::EntityUuid{} : m_models.front().entity;
    }

    const AnimationControllerEditor::Model* AnimationControllerEditor::model() const {
        const auto found = std::ranges::find(m_models, m_model, &Model::entity);
        return found == m_models.end() ? nullptr : &*found;
    }

    void AnimationControllerEditor::track_edit(const Controller& before, const Controller& after) {
        if(before != after && !m_edit_before)
            m_edit_before = before;
        if(m_edit_before && !ImGui::IsAnyItemActive()) {
            if(*m_edit_before != after) {
                if(m_undo.size() == 64)
                    m_undo.erase(m_undo.begin());
                m_undo.push_back(std::move(*m_edit_before));
                m_redo.clear();
            }
            m_edit_before.reset();
        }
    }

    bool AnimationControllerEditor::undo(Controller& data) {
        if(m_edit_before) {
            m_undo.push_back(std::move(*m_edit_before));
            m_edit_before.reset();
        }
        if(m_undo.empty())
            return false;
        m_redo.push_back(data);
        data = std::move(m_undo.back());
        m_undo.pop_back();
        m_transition.reset();
        m_connect_from.reset();
        m_state.reset();
        m_parameter.clear();
        m_error.clear();
        return true;
    }

    bool AnimationControllerEditor::redo(Controller& data) {
        if(m_redo.empty())
            return false;
        m_undo.push_back(data);
        data = std::move(m_redo.back());
        m_redo.pop_back();
        m_transition.reset();
        m_connect_from.reset();
        m_state.reset();
        m_parameter.clear();
        m_error.clear();
        return true;
    }

    void AnimationControllerEditor::select_state(std::size_t index) {
        if(m_state != index)
            m_event.reset();
        m_state = index;
        m_preview_index = index;
        m_transition.reset();
        m_parameter.clear();
    }

    void AnimationControllerEditor::rename_state(
        Controller& data, std::size_t index, std::string name) {
        if(name.empty() || (data.find_state(name) && data.states[index].name != name)) {
            m_error = "状态名称不能为空，也不能与已有状态重复。";
            return;
        }
        m_error.clear();
        const auto previous = data.states[index].name;
        data.states[index].name = name;
        if(m_connect_from == previous)
            m_connect_from = name;
        if(data.initial == previous)
            data.initial = name;
        for(auto& layer : data.layers)
            if(layer.state == previous)
                layer.state = name;
        for(auto& transition : data.transitions) {
            if(transition.from == previous)
                transition.from = name;
            if(transition.to == previous)
                transition.to = name;
        }
        if(auto position = m_positions.extract(previous); !position.empty()) {
            position.key() = name;
            m_positions.insert(std::move(position));
        }
    }

    void AnimationControllerEditor::remove_state(Controller& data, std::size_t index) {
        if(data.states.size() <= 1)
            return;
        const auto name = data.states[index].name;
        if(std::ranges::any_of(
               data.layers, [&](const auto& layer) { return layer.state == name; })) {
            m_error = "此状态仍被动画层引用，请先修改或删除对应层。";
            return;
        }
        data.states.erase(data.states.begin() + index);
        std::erase_if(data.transitions,
            [&](const auto& edge) { return edge.from == name || edge.to == name; });
        if(data.initial == name)
            data.initial = data.states.front().name;
        m_positions.erase(name);
        if(m_connect_from == name)
            m_connect_from.reset();
        select_state(0);
    }

    void AnimationControllerEditor::clip_choice(const char* label, std::string& name) {
        const auto* source = model();
        if(m_library) {
            if(ImGui::BeginCombo(label, name.c_str())) {
                for(const auto& entry : m_library->entries)
                    if(ImGui::Selectable(entry.name.c_str(), name == entry.name))
                        name = entry.name;
                ImGui::EndCombo();
            }
            return;
        }
        if(!source) {
            Ui::input_text(label, name);
            ImGui::TextDisabled("选择预览角色后可从真实动作列表选择。");
            return;
        }
        if(ImGui::BeginCombo(label, name.empty() ? "选择动画片段" : name.c_str())) {
            for(const auto& clip : source->animation->clips)
                if(ImGui::Selectable(clip.name.c_str(), clip.name == name))
                    name = clip.name;
            ImGui::EndCombo();
        }
        if(!source->animation->find_clip(name))
            ImGui::TextColored({1, .45f, .35f, 1}, "角色中没有片段：%s", name.c_str());
    }

    void AnimationControllerEditor::render_parameter(Controller& data, const std::string& name) {
        const auto found = data.parameters.find(name);
        if(found == data.parameters.end())
            return;
        ImGui::SeparatorText("参数");
        auto edited = name;
        if(Ui::input_text("名称（回车确认）", edited, ImGuiInputTextFlags_EnterReturnsTrue)
            && !edited.empty() && !data.parameters.contains(edited)) {
            auto entry = data.parameters.extract(name);
            entry.key() = edited;
            data.parameters.insert(std::move(entry));
            for(auto& state : data.states) {
                if(state.parameter == name)
                    state.parameter = edited;
                if(state.parameter_y == name)
                    state.parameter_y = edited;
            }
            for(auto& layer : data.layers)
                if(layer.weight_parameter == name)
                    layer.weight_parameter = edited;
            for(auto& transition : data.transitions)
                for(auto& condition : transition.conditions)
                    if(condition.parameter == name)
                        condition.parameter = edited;
            if(auto value = m_preview_values.extract(name); !value.empty()) {
                value.key() = edited;
                m_preview_values.insert(std::move(value));
            }
            m_parameter = edited;
            return;
        }
        const bool blended = std::ranges::any_of(data.states, [&](const auto& state) {
            return state.parameter == name || state.parameter_y == name;
        }) || std::ranges::any_of(data.layers, [&](const auto& layer) {
            return layer.weight_parameter == name;
        });
        auto& value = found->second;
        int type = std::holds_alternative<bool>(value) ? 0 : 1;
        ImGui::BeginDisabled(blended);
        if(ImGui::Combo("类型", &type, "开关\0数值\0")) {
            value = type == 0 ? Comet::ParameterValue(false) : Comet::ParameterValue(0.f);
            for(auto& transition : data.transitions)
                for(auto& condition : transition.conditions)
                    if(condition.parameter == name) {
                        condition.value = value;
                        condition.compare = Controller::Condition::Compare::Equal;
                    }
        }
        ImGui::EndDisabled();
        value_widget("默认值", value);
        ImGui::TextWrapped(
            "脚本通过此名称驱动控制器。左侧预览值用于试播，保存时只写入这里的默认值。");
        bool used = blended;
        for(const auto& transition : data.transitions)
            used |= std::ranges::any_of(transition.conditions,
                [&](const auto& condition) { return condition.parameter == name; });
        ImGui::BeginDisabled(used);
        if(ImGui::Button("删除参数")) {
            data.parameters.erase(name);
            m_preview_values.erase(name);
            m_parameter.clear();
        }
        ImGui::EndDisabled();
        if(used)
            ImGui::TextDisabled("仍被状态或切换条件引用，移除引用后可删除。");
    }

    void AnimationControllerEditor::render_state(Controller& data, std::size_t index) {
        auto& state = data.states[index];
        ImGui::SeparatorText("动画状态");
        auto name = state.name;
        if(Ui::input_text("名称（回车确认）", name, ImGuiInputTextFlags_EnterReturnsTrue))
            rename_state(data, index, std::move(name));
        if(state.name == data.initial)
            ImGui::TextColored({.4f, .85f, .45f, 1}, "初始状态");
        else if(ImGui::Button("设为初始状态"))
            data.initial = state.name;
        int type = state.parameter.empty() ? 0 : (state.parameter_y.empty() ? 1 : 2);
        if(ImGui::Combo("播放方式", &type, "单动画\0一维混合（1D）\0二维混合（2D）\0")) {
            state.parameter_y.clear();
            if(type == 0) {
                state.parameter.clear();
                state.motions.resize(1);
            } else {
                for(const auto& [key, value] : data.parameters)
                    if(std::holds_alternative<float>(value)) {
                        state.parameter = key;
                        break;
                    }
                if(state.parameter.empty()) {
                    for(unsigned i = 1;; ++i) {
                        const auto key = "blend" + std::to_string(i);
                        if(data.parameters.emplace(key, 0.f).second) {
                            state.parameter = key;
                            break;
                        }
                    }
                }
            }
        }
        if(type == 2 && state.parameter_y.empty()) {
            std::string name = "blend_y";
            for(unsigned i = 1; data.parameters.contains(name); ++i)
                name = "blend_y" + std::to_string(i);
            data.parameters.emplace(name, 0.f);
            state.parameter_y = name;
        }
        ImGui::Checkbox("循环播放", &state.loop);
        if(state.parameter.empty()) {
            if(state.motions.empty())
                state.motions.emplace_back();
            clip_choice("动画片段", state.motions.front().clip);
        } else {
            if(ImGui::BeginCombo("驱动参数", state.parameter.c_str())) {
                for(const auto& [key, value] : data.parameters)
                    if(std::holds_alternative<float>(value)
                        && ImGui::Selectable(key.c_str(), key == state.parameter))
                        state.parameter = key;
                ImGui::EndCombo();
            }
            if(type == 2 && ImGui::BeginCombo("Y 驱动参数", state.parameter_y.c_str())) {
                for(const auto& [key, value] : data.parameters)
                    if(std::holds_alternative<float>(value)
                        && ImGui::Selectable(key.c_str(), key == state.parameter_y))
                        state.parameter_y = key;
                ImGui::EndCombo();
            }
            ImGui::DragFloat("参数平滑 (s)", &state.smoothing, .01f, 0, 10);
            ImGui::TextWrapped(
                type == 2 ? "X/Y 参数共同决定动作权重；采样位置不能重复，轴应使用相近量纲。"
                          : "参数落在两个点之间时混合相邻动画；阈值需递增。");
            if(type == 2 && !state.motions.empty()) {
                const auto origin = ImGui::GetCursorScreenPos();
                const float width = std::max(100.f, ImGui::GetContentRegionAvail().x);
                float min_x = state.motions.front().threshold, max_x = min_x,
                      min_y = state.motions.front().y, max_y = min_y;
                for(const auto& p : state.motions) {
                    min_x = std::min(min_x, p.threshold);
                    max_x = std::max(max_x, p.threshold);
                    min_y = std::min(min_y, p.y);
                    max_y = std::max(max_y, p.y);
                }
                const auto position = [&](float x, float y) {
                    return add(
                        origin, {15 + (width - 30) * (x - min_x) / std::max(.001f, max_x - min_x),
                                    135 - 120 * (y - min_y) / std::max(.001f, max_y - min_y)});
                };
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(origin, add(origin, {width, 150}), IM_COL32(30, 35, 42, 255));
                for(const auto& p : state.motions) {
                    const auto pos = position(p.threshold, p.y);
                    draw->AddCircleFilled(pos, 4, IM_COL32(80, 170, 240, 255));
                    draw->AddText(add(pos, {6, -12}), IM_COL32_WHITE, p.clip.c_str());
                }
                const auto value = data.blend_parameters(state, m_preview_values);
                draw->AddCircle(
                    position(std::clamp(value.x, min_x, max_x), std::clamp(value.y, min_y, max_y)),
                    7, IM_COL32(250, 190, 75, 255));
                ImGui::Dummy({width, 155});
            }
            if(type != 2 && !state.motions.empty()) {
                const auto origin = ImGui::GetCursorScreenPos();
                const float width = std::max(80.f, ImGui::GetContentRegionAvail().x - 30);
                auto* draw = ImGui::GetWindowDrawList();
                const float minimum = state.motions.front().threshold;
                const float range = std::max(.001f, state.motions.back().threshold - minimum);
                draw->AddLine(add(origin, {15, 25}), add(origin, {15 + width, 25}),
                    IM_COL32(160, 160, 160, 255), 2);
                for(const auto& point : state.motions) {
                    const float x =
                        15 + width * std::clamp((point.threshold - minimum) / range, 0.f, 1.f);
                    draw->AddCircleFilled(add(origin, {x, 25}), 5, IM_COL32(80, 170, 240, 255));
                    const auto threshold = number_text(point.threshold);
                    draw->AddText(add(origin, {x - 8, 2}), IM_COL32_WHITE, threshold.c_str());
                    const float text_width = ImGui::CalcTextSize(point.clip.c_str()).x;
                    draw->AddText(add(origin, {std::clamp(x - text_width / 2, 0.f,
                                                   std::max(0.f, width + 30 - text_width)),
                                                  35}),
                        IM_COL32(190, 195, 210, 255), point.clip.c_str());
                }
                ImGui::Dummy({width + 30, 58});
            }
            std::optional<std::size_t> removed;
            for(std::size_t i = 0; i < state.motions.size(); ++i) {
                ImGui::PushID(int(i));
                ImGui::SetNextItemWidth(85);
                ImGui::DragFloat(type == 2 ? "X" : "阈值", &state.motions[i].threshold, .1f);
                if(type == 2)
                    ImGui::DragFloat("Y", &state.motions[i].y, .1f);
                clip_choice("动画片段", state.motions[i].clip);
                if(state.motions.size() > 1 && ImGui::SmallButton("移除片段"))
                    removed = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if(removed)
                state.motions.erase(state.motions.begin() + *removed);
            if(ImGui::Button("添加混合点")) {
                const float threshold =
                    state.motions.empty() ? 0 : state.motions.back().threshold + 1;
                state.motions.push_back(
                    {threshold, state.motions.empty() ? "idle" : state.motions.back().clip});
            }
            ImGui::SameLine();
            if(ImGui::Button("按阈值排序"))
                std::ranges::sort(state.motions, {}, &Comet::AnimationBlendPoint::threshold);
        }
        render_events(state);
        ImGui::SeparatorText("离开此状态的切换");
        for(std::size_t i = 0; i < data.transitions.size(); ++i) {
            const auto& edge = data.transitions[i];
            if(edge.from != state.name && !edge.from.empty())
                continue;
            const auto label =
                std::to_string(i + 1) + ". → " + edge.to + "  " + condition_summary(edge);
            if(ImGui::Selectable(label.c_str())) {
                m_transition = i;
                m_state.reset();
            }
        }
        if(ImGui::Button("从此状态建立切换")) {
            m_connect_from = state.name;
            open();
        }
        ImGui::BeginDisabled(data.states.size() <= 1);
        if(ImGui::Button("删除状态及关联切换"))
            remove_state(data, index);
        ImGui::EndDisabled();
    }

    void AnimationControllerEditor::render_events(Controller::State& state) {
        ImGui::SeparatorText("动画事件");
        ImGui::TextWrapped(
            "点击标记编辑，拖动调整时机；双击时间轴添加。时间按一个周期的比例保存。");
        const auto origin = ImGui::GetCursorScreenPos();
        const float width = std::max(60.f, ImGui::GetContentRegionAvail().x - 20);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddLine({origin.x + 10, origin.y + 25}, {origin.x + width + 10, origin.y + 25},
            IM_COL32(155, 160, 170, 255), 2);
        draw->AddText(origin, IM_COL32_WHITE, "0%");
        draw->AddText({origin.x + width - 10, origin.y}, IM_COL32_WHITE, "100%");
        const auto insert = [&](float phase) {
            state.events.push_back({phase, "event", std::string{}});
            std::stable_sort(state.events.begin(), state.events.end(),
                [](const auto& a, const auto& b) { return a.phase < b.phase; });
            m_event = std::size_t(std::ranges::find_if(state.events, [&](const auto& event) {
                return event.phase == phase && event.name == "event";
            }) - state.events.begin());
        };
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Event timeline", {width + 20, 54});
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            insert(std::clamp((ImGui::GetMousePos().x - origin.x - 10) / width, 0.f, 1.f));
        bool sort = false;
        for(std::size_t i = 0; i < state.events.size(); ++i) {
            auto& event = state.events[i];
            const float x = origin.x + 10 + event.phase * width;
            draw->AddTriangleFilled({x, origin.y + 19}, {x - 6, origin.y + 33},
                {x + 6, origin.y + 33},
                m_event == i ? IM_COL32(255, 200, 75, 255) : IM_COL32(80, 170, 240, 255));
            ImGui::SetCursorScreenPos({x - 7, origin.y + 17});
            ImGui::PushID(int(i));
            ImGui::InvisibleButton("Event marker", {14, 20});
            if(ImGui::IsItemClicked())
                m_event = i;
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                event.phase =
                    std::clamp((ImGui::GetMousePos().x - origin.x - 10) / width, 0.f, 1.f);
            sort |= ImGui::IsItemDeactivated();
            ImGui::SetItemTooltip("%s · %.1f%%", event.name.c_str(), double(event.phase * 100));
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({origin.x, origin.y + 54});
        if(ImGui::Button("添加事件"))
            insert(.5f);
        if(m_event && *m_event < state.events.size()) {
            auto& event = state.events[*m_event];
            Ui::input_text("事件名", event.name);
            ImGui::SliderFloat("触发比例", &event.phase, 0, 1, "%.3f");
            sort |= ImGui::IsItemDeactivatedAfterEdit();
            int type = 0;
            if(std::holds_alternative<float>(event.value))
                type = 1;
            else if(std::holds_alternative<bool>(event.value))
                type = 2;
            if(ImGui::Combo("事件参数类型", &type, "文本\0数值\0开关\0")) {
                if(type == 0)
                    event.value = std::string{};
                else if(type == 1)
                    event.value = 0.f;
                else
                    event.value = false;
            }
            value_widget("事件参数", event.value);
            if(ImGui::Button("删除事件")) {
                state.events.erase(state.events.begin() + *m_event);
                m_event.reset();
            }
        }
        if(sort && m_event && *m_event < state.events.size()) {
            const auto selected = state.events[*m_event];
            std::stable_sort(state.events.begin(), state.events.end(),
                [](const auto& a, const auto& b) { return a.phase < b.phase; });
            m_event = std::size_t(std::ranges::find(state.events, selected) - state.events.begin());
        }
    }

    void AnimationControllerEditor::render_transition(Controller& data, std::size_t index) {
        auto& edge = data.transitions[index];
        ImGui::SeparatorText("状态切换");
        state_choice("来源", edge.from, data, true);
        state_choice("目标", edge.to, data, false);
        ImGui::DragFloat("混合时间 (s)", &edge.duration, .01f, 0, 10);
        ImGui::Checkbox("等待动画播放完成一次", &edge.after_finished);
        ImGui::TextWrapped("下列条件需同时满足。同一帧有多个切换成立时，序号较小的优先。");
        std::optional<std::size_t> removed;
        for(std::size_t i = 0; i < edge.conditions.size(); ++i) {
            ImGui::PushID(int(i));
            auto& condition = edge.conditions[i];
            if(ImGui::BeginCombo("参数", condition.parameter.c_str())) {
                for(const auto& [key, value] : data.parameters)
                    if(ImGui::Selectable(key.c_str(), key == condition.parameter)) {
                        condition.parameter = key;
                        condition.value = value;
                        condition.compare = Controller::Condition::Compare::Equal;
                    }
                ImGui::EndCombo();
            }
            constexpr std::array labels{"等于", "不等于", "大于", "大于等于", "小于", "小于等于"};
            int comparison = int(condition.compare);
            const int count = std::holds_alternative<bool>(condition.value) ? 2 : 6;
            if(ImGui::Combo("关系", &comparison, labels.data(), count))
                condition.compare = Controller::Condition::Compare(comparison);
            value_widget("比较值", condition.value);
            if(ImGui::SmallButton("移除条件"))
                removed = i;
            ImGui::Separator();
            ImGui::PopID();
        }
        if(removed)
            edge.conditions.erase(edge.conditions.begin() + *removed);
        ImGui::BeginDisabled(data.parameters.empty());
        if(ImGui::Button("添加条件")) {
            const auto& parameter = *data.parameters.begin();
            edge.conditions.push_back(
                {parameter.first, Controller::Condition::Compare::Equal, parameter.second});
        }
        ImGui::EndDisabled();
        ImGui::Text("优先序号：%zu", index + 1);
        ImGui::BeginDisabled(index == 0);
        if(ImGui::Button("提高优先级")) {
            std::swap(data.transitions[index], data.transitions[index - 1]);
            m_transition = index - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1 == data.transitions.size());
        if(ImGui::Button("降低优先级")) {
            std::swap(data.transitions[index], data.transitions[index + 1]);
            m_transition = index + 1;
        }
        ImGui::EndDisabled();
        if(ImGui::Button("删除切换")) {
            data.transitions.erase(data.transitions.begin() + *m_transition);
            m_transition.reset();
        }
    }

    void AnimationControllerEditor::render_layers(Controller& data) {
        std::string selected = "使用模型自带片段";
        for(const auto& [handle, name] : m_libraries)
            if(handle == data.library)
                selected = name;
        if(data.library && selected == "使用模型自带片段")
            selected = "缺失动画库";
        if(ImGui::BeginCombo("动画库", selected.c_str())) {
            if(ImGui::Selectable("使用模型自带片段", !data.library)) {
                data.library = {};
                m_library.reset();
            }
            for(const auto& [handle, name] : m_libraries)
                if(ImGui::Selectable(name.c_str(), handle == data.library)
                    && handle != data.library) {
                    data.library = handle;
                    m_library.reset();
                }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped(
            "层按顺序叠加：覆盖层替换姿态，加法层叠加相对参考姿态的变化。遮罩包含后代，空遮罩为全身；事件只来自基础状态。");
        std::optional<std::size_t> removed;
        for(std::size_t i = 0; i < data.layers.size(); ++i) {
            auto& layer = data.layers[i];
            ImGui::PushID(int(i));
            Ui::input_text("层名称", layer.name);
            state_choice("动作状态", layer.state, data, false);
            int mode = int(layer.mode);
            if(ImGui::Combo("混合方式", &mode, "覆盖\0加法\0")) {
                layer.mode = Comet::AnimationLayer::Mode(mode);
                if(layer.mode == Comet::AnimationLayer::Mode::Override) {
                    layer.reference_clip.clear();
                    layer.reference_time = 0;
                }
            }
            if(layer.mode == Comet::AnimationLayer::Mode::Additive) {
                bool rest = layer.reference_clip.empty();
                if(ImGui::Checkbox("使用绑定姿态作为参考", &rest)) {
                    layer.reference_clip.clear();
                    layer.reference_time = 0;
                    if(!rest) {
                        const auto* state = data.find_state(layer.state);
                        if(state && !state->motions.empty())
                            layer.reference_clip = state->motions.front().clip;
                    }
                }
                if(!layer.reference_clip.empty()) {
                    clip_choice("参考片段", layer.reference_clip);
                    ImGui::DragFloat("参考时间 (s)", &layer.reference_time, .01f, 0, 3600);
                }
                ImGui::TextWrapped(
                    "叠加动作相对参考的平移、旋转和缩放，适合呼吸、瞄准与受击。参考片段使用固定时间。");
            }
            ImGui::SliderFloat("权重", &layer.weight, 0, 1);
            if(ImGui::BeginCombo("权重参数",
                   layer.weight_parameter.empty() ? "固定权重" : layer.weight_parameter.c_str())) {
                if(ImGui::Selectable("固定权重", layer.weight_parameter.empty()))
                    layer.weight_parameter.clear();
                for(const auto& [name, value] : data.parameters)
                    if(std::holds_alternative<float>(value)
                        && ImGui::Selectable(name.c_str(), name == layer.weight_parameter))
                        layer.weight_parameter = name;
                ImGui::EndCombo();
            }
            std::optional<std::size_t> remove_bone;
            for(std::size_t j = 0; j < layer.mask.size(); ++j) {
                ImGui::PushID(int(j));
                if(const auto* source = model()) {
                    if(ImGui::BeginCombo("遮罩根节点", layer.mask[j].c_str())) {
                        for(const auto& node : source->animation->nodes)
                            if(ImGui::Selectable(node.name.c_str(), layer.mask[j] == node.name))
                                layer.mask[j] = node.name;
                        ImGui::EndCombo();
                    }
                    if(std::ranges::find(
                           source->animation->nodes, layer.mask[j], &Comet::AnimationNode::name)
                        == source->animation->nodes.end())
                        ImGui::TextColored({1, .45f, .35f, 1}, "预览模型缺少此节点");
                } else
                    Ui::input_text("遮罩根节点", layer.mask[j]);
                ImGui::SameLine();
                if(ImGui::SmallButton("移除"))
                    remove_bone = j;
                ImGui::PopID();
            }
            if(remove_bone)
                layer.mask.erase(layer.mask.begin() + *remove_bone);
            if(ImGui::SmallButton("添加遮罩节点"))
                layer.mask.emplace_back();
            ImGui::SameLine();
            if(ImGui::SmallButton("删除层"))
                removed = i;
            if(i > 0) {
                ImGui::SameLine();
                if(ImGui::SmallButton("上移"))
                    std::swap(data.layers[i], data.layers[i - 1]);
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if(removed)
            data.layers.erase(data.layers.begin() + *removed);
        ImGui::BeginDisabled(data.layers.size() >= 8 || data.states.empty());
        if(ImGui::Button("添加动画层")) {
            for(unsigned i = 1;; ++i) {
                const auto name = "layer" + std::to_string(i);
                if(std::ranges::any_of(
                       data.layers, [&](const auto& layer) { return layer.name == name; }))
                    continue;
                data.layers.push_back({name, data.states.front().name});
                break;
            }
        }
        ImGui::EndDisabled();
    }
    void AnimationControllerEditor::render_details(Controller& data) {
        const auto before = data;
        if(ImGui::CollapsingHeader("动画库与分层"))
            render_layers(data);
        if(m_state && *m_state < data.states.size())
            render_state(data, *m_state);
        else if(m_transition && *m_transition < data.transitions.size())
            render_transition(data, *m_transition);
        else if(!m_parameter.empty())
            render_parameter(data, m_parameter);
        else
            ImGui::TextWrapped("在动画控制器面板中选择状态、连接箭头或参数，在这里编辑其属性。");
        if(!m_error.empty())
            ImGui::TextColored({1, .45f, .35f, 1}, "%s", m_error.c_str());
        if(auto valid = data.validate(); !valid)
            ImGui::TextColored({1, .45f, .35f, 1}, "%s", valid.error().c_str());
        track_edit(before, data);
    }

    void AnimationControllerEditor::render_parameters(Controller& data) {
        ImGui::SeparatorText(m_runtime ? "运行参数" : "参数与预览值");
        for(const auto& [name, value] : data.parameters) {
            ImGui::PushID(name.c_str());
            const auto label = name + (std::holds_alternative<bool>(value) ? " · 开关" : " · 数值");
            if(ImGui::Selectable(label.c_str(), m_parameter == name)) {
                m_parameter = name;
                m_state.reset();
                m_transition.reset();
            }
            auto found = m_preview_values.try_emplace(name, value).first;
            if(found->second.index() != value.index())
                found->second = value;
            auto displayed = found->second;
            if(m_runtime && model())
                if(const auto current = model()->parameters.find(name);
                    current != model()->parameters.end())
                    displayed = current->second;
            ImGui::BeginDisabled(m_runtime);
            ImGui::SetNextItemWidth(-1);
            const Controller::State* blended = nullptr;
            if(m_preview_index && *m_preview_index < data.states.size()) {
                const auto& state = data.states[*m_preview_index];
                if(state.parameter == name && state.motions.size() > 1
                    && state.motions.back().threshold > state.motions.front().threshold)
                    blended = &state;
            }
            if(blended && std::holds_alternative<float>(displayed))
                ImGui::SliderFloat("##Preview value", &std::get<float>(displayed),
                    blended->motions.front().threshold, blended->motions.back().threshold);
            else
                value_widget("##Preview value", displayed);
            if(!m_runtime)
                found->second = displayed;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if(ImGui::Button("添加参数")) {
            for(unsigned i = 1;; ++i) {
                const auto name = "parameter" + std::to_string(i);
                if(data.parameters.emplace(name, 0.f).second) {
                    m_parameter = name;
                    m_state.reset();
                    m_transition.reset();
                    break;
                }
            }
        }
        ImGui::TextWrapped("点击参数名称编辑类型和默认值。预览值不写入资产。");
        ImGui::SeparatorText("状态");
        for(std::size_t i = 0; i < data.states.size(); ++i) {
            const auto label =
                data.states[i].name + (data.states[i].name == data.initial ? " · 初始" : "");
            if(ImGui::Selectable(label.c_str(), m_state == i))
                select_state(i);
        }
    }

    void AnimationControllerEditor::render_preview(
        const Controller& data, AnimationPreview* preview) {
        const auto* source = model();
        ImGui::SetNextItemWidth(190);
        if(ImGui::BeginCombo("预览角色", source ? source->name.c_str() : "无可用骨骼模型")) {
            for(const auto& candidate : m_models) {
                const auto label = candidate.name + "###" + candidate.entity.to_string();
                if(ImGui::Selectable(label.c_str(), m_model == candidate.entity)) {
                    m_model = candidate.entity;
                    m_preview_state.clear();
                    preview = nullptr; // 下一帧由 Inspector 绑定新目标。
                }
            }
            ImGui::EndCombo();
        }
        if(m_runtime) {
            if(source && source->controller != m_controller)
                ImGui::TextDisabled("该角色未引用当前控制器。");
            else if(source && source->playback) {
                ImGui::SameLine();
                ImGui::Text("当前状态：%s · 过渡 %.0f%%", source->playback->state.c_str(),
                    source->playback->blend * 100);
            }
            return;
        }
        if(!preview || !m_preview_index || *m_preview_index >= data.states.size()) {
            ImGui::TextDisabled("选择状态后，在现有视口预览；保存前可调整左侧参数试播。");
            return;
        }
        const auto& state = data.states[*m_preview_index];
        if(data.library && !m_library) {
            preview->stop();
            ImGui::TextWrapped("动画库不可用：%s", m_library_error.c_str());
            return;
        }
        if(auto bound = preview->set_library(data.library ? m_library : nullptr); !bound) {
            preview->stop();
            ImGui::TextWrapped("%s", bound.error().c_str());
            return;
        }
        auto mixed = data.sample(state, data.blend_parameters(state, m_preview_values));
        if(!mixed) {
            preview->stop();
            ImGui::TextWrapped("%s", mixed.error().c_str());
            return;
        }
        auto sample = std::move(mixed).value();
        std::vector<Comet::AnimationLayer> layers;
        for(const auto& layer : data.layers) {
            const auto* layer_state = data.find_state(layer.state);
            if(!layer_state)
                continue;
            auto mixed_layer = data.sample_layer(
                layer, m_preview_values, data.blend_parameters(*layer_state, m_preview_values));
            if(!mixed_layer) {
                preview->stop();
                ImGui::TextColored({1, .45f, .35f, 1}, "%s", mixed_layer.error().c_str());
                return;
            }
            layers.push_back(std::move(mixed_layer).value());
        }
        preview->set_layers(std::move(layers));
        if(!preview->select_sample(sample, m_preview_state != state.name)) {
            preview->stop();
            ImGui::TextColored({1, .45f, .35f, 1}, "预览角色缺少此状态的动画片段。");
            return;
        }
        m_preview_state = state.name;
        ImGui::SameLine();
        if(preview->playing()) {
            if(ImGui::Button("暂停预览###Pause Controller Preview"))
                preview->pause();
        } else if(ImGui::Button("播放预览###Play Controller Preview"))
            static_cast<void>(preview->play());
        ImGui::SameLine();
        if(ImGui::Button("结束预览###Stop Controller Preview"))
            preview->stop();
        float time = float(preview->time());
        ImGui::SetNextItemWidth(250);
        const char* time_label = sample.normalized_time ? "周期进度###Controller Preview Time"
                                                        : "时间 (s)###Controller Preview Time";
        if(ImGui::SliderFloat(time_label, &time, 0, float(preview->duration()), "%.3f"))
            static_cast<void>(preview->seek(time));
        ImGui::SameLine();
        float speed = preview->speed();
        ImGui::SetNextItemWidth(80);
        if(ImGui::DragFloat("播放倍率", &speed, .05f, 0, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            static_cast<void>(preview->set_speed(speed));
        float base = 1;
        for(const auto& part : sample.blends)
            base -= part.weight;
        ImGui::Text("%s %.0f%%", sample.clip.c_str(), base * 100);
        for(const auto& part : sample.blends) {
            ImGui::SameLine();
            ImGui::Text("+ %s %.0f%%", part.clip.c_str(), part.weight * 100);
        }
    }

    void AnimationControllerEditor::render_graph(Controller& data) {
        ImGui::BeginChild("State graph", {0, 0}, ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto origin = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(available.x, 50.f), std::max(available.y, 50.f)};
        const auto end = add(origin, size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, end, true);
        draw->AddRectFilled(origin, end, IM_COL32(30, 33, 38, 255));
        ImGui::InvisibleButton("Canvas", size, ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const auto& io = ImGui::GetIO();
        if(hovered && io.MouseWheel != 0) {
            const float next = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), .45f, 1.5f);
            const auto local = ImVec2{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
            m_pan = add(local, scale({m_pan.x - local.x, m_pan.y - local.y}, next / m_zoom));
            m_zoom = next;
        }
        if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            m_pan = add(m_pan, io.MouseDelta);
        const float grid = 25 * m_zoom;
        for(float x = std::fmod(m_pan.x, grid); x < size.x; x += grid)
            draw->AddLine(add(origin, {x, 0}), add(origin, {x, size.y}), IM_COL32(43, 47, 53, 255));
        for(float y = std::fmod(m_pan.y, grid); y < size.y; y += grid)
            draw->AddLine(add(origin, {0, y}), add(origin, {size.x, y}), IM_COL32(43, 47, 53, 255));
        for(std::size_t i = 0; i < data.states.size(); ++i)
            m_positions.try_emplace(
                data.states[i].name, ImVec2{float(i % 3) * 250, 125 + float(i / 3) * 160});
        const auto position = [&](const std::string& name) {
            return add(add(origin, m_pan), scale(m_positions.at(name), m_zoom));
        };
        const auto* source = model();
        const auto* frame = source && m_runtime && source->controller == m_controller
                                ? source->playback.get()
                                : nullptr;
        for(std::size_t i = 0; i < data.transitions.size(); ++i) {
            const auto& edge = data.transitions[i];
            if(!m_positions.contains(edge.from) || !m_positions.contains(edge.to))
                continue;
            const auto from = position(edge.from), to = position(edge.to);
            const bool right = to.x >= from.x;
            const ImVec2 a{from.x + (right ? 180 : 0) * m_zoom, from.y + 36 * m_zoom};
            const ImVec2 d{to.x + (right ? 0 : 180) * m_zoom, to.y + 36 * m_zoom};
            const float bend = std::max(55.f, std::abs(d.x - a.x) * .45f);
            const ImVec2 b{a.x + (right ? bend : -bend), a.y + 12 * m_zoom};
            const ImVec2 c{d.x - (right ? bend : -bend), d.y + 12 * m_zoom};
            bool edge_hovered = false;
            auto last = a;
            for(int segment = 1; segment <= 24; ++segment) {
                const auto point = bezier(a, b, c, d, segment / 24.f);
                edge_hovered |= distance_to_segment(io.MousePos, last, point) < 7;
                last = point;
            }
            edge_hovered &= hovered;
            const bool active = frame && frame->blend < 1 && frame->state == edge.to
                                && (edge.from.empty() || frame->previous_state == edge.from);
            const auto color =
                active ? IM_COL32(100, 220, 130, 255)
                       : (m_transition == i || edge_hovered ? IM_COL32(240, 185, 70, 255)
                                                            : IM_COL32(130, 155, 185, 255));
            draw->AddBezierCubic(a, b, c, d, color, active ? 3 : 2);
            const auto near = bezier(a, b, c, d, .96f);
            const float angle = std::atan2(d.y - near.y, d.x - near.x);
            const ImVec2 left{d.x - 10 * std::cos(angle - .45f), d.y - 10 * std::sin(angle - .45f)};
            const ImVec2 right_point{
                d.x - 10 * std::cos(angle + .45f), d.y - 10 * std::sin(angle + .45f)};
            draw->AddTriangleFilled(d, left, right_point, color);
            draw->AddText(bezier(a, b, c, d, .5f), color, std::to_string(i + 1).c_str());
            if(edge_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_transition = i;
                m_state.reset();
                m_parameter.clear();
            }
            if(edge_hovered)
                ImGui::SetTooltip("%s → %s\n%s", edge.from.empty() ? "任意状态" : edge.from.c_str(),
                    edge.to.c_str(), condition_summary(edge).c_str());
        }
        const auto node = [&](const std::string& name, std::optional<std::size_t> index) {
            const auto top = position(name);
            const auto bottom = add(top, scale({180, 72}, m_zoom));
            const bool selected = index && m_state == index;
            const bool current = frame && frame->state == name;
            const bool initial = !name.empty() && name == data.initial;
            draw->AddRectFilled(
                top, bottom, current ? IM_COL32(38, 90, 65, 255) : IM_COL32(55, 64, 78, 255), 6);
            draw->AddRect(top, bottom,
                selected ? IM_COL32(250, 190, 75, 255)
                         : (initial ? IM_COL32(90, 185, 120, 255) : IM_COL32(105, 120, 140, 255)),
                6.f, selected ? 3.f : 1.f);
            draw->AddText(add(top, scale({12, 12}, m_zoom)), IM_COL32_WHITE,
                name.empty() ? "任意状态" : name.c_str());
            std::string kind = "可从任意状态进入目标";
            if(index) {
                const auto& state = data.states[*index];
                kind = state.parameter.empty()
                           ? "单动画"
                           : (state.parameter_y.empty() ? "1D 混合 · " : "2D 混合 · ")
                                 + state.parameter;
                if(state.parameter.empty() && !state.motions.empty())
                    kind += " · " + state.motions.front().clip;
            }
            draw->AddText(
                add(top, scale({12, 43}, m_zoom)), IM_COL32(185, 195, 210, 255), kind.c_str());
            if(initial)
                draw->AddCircleFilled(
                    add(top, scale({165, 15}, m_zoom)), 4, IM_COL32(100, 220, 130, 255));
            ImGui::SetCursorScreenPos(top);
            ImGui::PushID(name.c_str());
            ImGui::InvisibleButton("State", scale({180, 72}, m_zoom));
            if(ImGui::IsItemClicked()) {
                if(m_connect_from && !name.empty() && *m_connect_from != name) {
                    data.transitions.push_back({.from = *m_connect_from, .to = name});
                    m_transition = data.transitions.size() - 1;
                    m_state.reset();
                    m_parameter.clear();
                    m_connect_from.reset();
                } else if(index)
                    select_state(*index);
            }
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                m_positions[name] = add(m_positions[name], scale(io.MouseDelta, 1 / m_zoom));
            if(ImGui::BeginPopupContextItem()) {
                if(ImGui::MenuItem("从这里连接"))
                    m_connect_from = name;
                if(index && ImGui::MenuItem("设为初始状态"))
                    data.initial = name;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };
        node("", {});
        for(std::size_t i = 0; i < data.states.size(); ++i)
            node(data.states[i].name, i);
        draw->PopClipRect();
        ImGui::SetCursorScreenPos(end);
        ImGui::Dummy({0, 0});
        ImGui::EndChild();
    }

    AnimationControllerEditor::Action AnimationControllerEditor::render_window(
        Controller& data, bool dirty, AnimationPreview* preview, std::string_view asset_name) {
        if(!m_open)
            return Action::None;
        ImGui::SetNextWindowSize({900, 540}, ImGuiCond_FirstUseEver);
        if(m_focus) {
            ImGui::SetNextWindowFocus();
            m_focus = false;
        }
        if(!ImGui::Begin("动画控制器", &m_open)) {
            ImGui::End();
            return Action::None;
        }
        if(!asset_name.empty())
            ImGui::Text("%.*s%s", int(asset_name.size()), asset_name.data(), dirty ? " *" : "");
        Action action = Action::None;
        ImGui::BeginDisabled(!dirty || !data.validate());
        if(ImGui::Button("保存###Save Controller"))
            action = Action::Save;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if(ImGui::Button("放弃修改###Discard Controller"))
            action = Action::Discard;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_undo());
        if(ImGui::Button("撤销###Undo Controller"))
            undo(data);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_redo());
        if(ImGui::Button("重做###Redo Controller"))
            redo(data);
        ImGui::EndDisabled();
        const auto before = data;
        render_preview(data, preview);
        ImGui::Separator();
        ImGui::BeginChild("Parameters", {210, 0}, ImGuiChildFlags_Borders);
        render_parameters(data);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginGroup();
        if(ImGui::Button("添加状态")) {
            for(unsigned i = 1;; ++i) {
                const auto name = "state" + std::to_string(i);
                if(data.find_state(name))
                    continue;
                data.states.push_back({name});
                if(model() && !model()->animation->clips.empty())
                    data.states.back().motions.front().clip =
                        model()->animation->clips.front().name;
                select_state(data.states.size() - 1);
                break;
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("连接状态"))
            m_connect_from = m_state && *m_state < data.states.size()
                                 ? std::optional(data.states[*m_state].name)
                                 : std::optional(std::string{});
        ImGui::SameLine();
        if(ImGui::Button("整理布局"))
            arrange(data);
        if(m_connect_from) {
            ImGui::Text("选择目标状态：%s → …",
                m_connect_from->empty() ? "任意状态" : m_connect_from->c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton("取消连接"))
                m_connect_from.reset();
        } else
            ImGui::TextDisabled("拖动移动节点 · 中键平移 · 滚轮缩放 · 点击箭头编辑条件");
        render_graph(data);
        ImGui::EndGroup();
        track_edit(before, data);
        ImGui::End();
        return action;
    }
}
