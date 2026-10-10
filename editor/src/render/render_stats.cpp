#include "render/render_stats.h"
#include "diagnostics/frame_diagnostics.h"
#include "render/render_diagnostics.h"

#include <imgui.h>
#include <algorithm>
#include <string_view>
#include <span>
#include <utility>

namespace CometEditor {
    namespace {
        using Summary = Comet::TimingHistory::Summary;

        const char* phase_caption(const std::string& phase) {
            static constexpr std::pair<std::string_view, const char*> captions[]{
                {"Events", "事件"},
                {"Update", "更新"},
                {"Prepare / UI", "等待 / 准备 / UI"},
                {"Render / submit", "渲染 / 提交"},
                {"Scene extract", "场景提取"},
                {"Asset resolve", "资产解析"},
                {"Material prep", "材质程序准备"},
                {"Geometry bounds", "世界界限计算"},
                {"Lighting prep", "光源与阴影准备"},
                {"Submission prep", "提交准备"},
                {"Queue submit", "队列提交"},
                {"Present", "呈现调用"},
            };
            for(const auto& [id, caption] : captions)
                if(id == phase)
                    return caption;
            return phase.c_str();
        }

        void show_trend(const Summary& summary) {
            float maximum = 0.1f;
            for(const auto& point : summary.trend)
                maximum = std::max(maximum, static_cast<float>(point.maximum));
            ImGui::TextDisabled("%s (0 - %.2f ms)", "近 5 秒趋势：均值 / 峰值", maximum);
            const auto origin = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), 55.0f);
            ImGui::Dummy(size);
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
            const auto point_at = [&](size_t index, double value) {
                return ImVec2(
                    origin.x + size.x * static_cast<float>(index) / (summary.trend.size() - 1),
                    origin.y + size.y * (1.0f - static_cast<float>(value) / maximum));
            };
            const auto mean_color = IM_COL32(80, 190, 220, 255);
            const auto peak_color = IM_COL32(235, 175, 70, 255);
            for(size_t index = 0; index < summary.trend.size(); ++index) {
                const auto& sample = summary.trend[index];
                if(sample.count == 0)
                    continue;
                draw->AddCircleFilled(point_at(index, sample.maximum), 1.5f, peak_color);
                if(index > 0 && summary.trend[index - 1].count > 0)
                    draw->AddLine(point_at(index - 1, summary.trend[index - 1].average()),
                        point_at(index, sample.average()), mean_color, 1.5f);
            }
        }

        void show_summary(const char* title, const Summary& summary) {
            ImGui::SeparatorText(title);
            if(summary.total.count == 0)
                ImGui::TextDisabled("%s", "等待采样");
            else {
                ImGui::Text("均值 %.3f ms", summary.total.average());
                ImGui::TextDisabled(
                    "峰值 %.3f ms | %zu 个样本", summary.total.maximum, summary.total.count);
            }
        }

        void show_details(const char* id, const Summary& summary) {
            if(!ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchProp))
                return;
            ImGui::TableSetupColumn("阶段", 0, 2.0f);
            ImGui::TableSetupColumn("均值（毫秒）");
            ImGui::TableSetupColumn("峰值（毫秒）");
            ImGui::TableHeadersRow();
            for(const auto& detail : summary.details) {
                if(detail.timing.count == 0)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(phase_caption(detail.name));
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", detail.timing.average());
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", detail.timing.maximum);
            }
            ImGui::EndTable();
        }
    }

    RenderStatsPanel::RenderStatsPanel(
        const Comet::FrameDiagnostics& frame, const Comet::RenderDiagnostics& render)
        : EditorPanel("渲染统计###Render Stats"), m_frame(frame), m_render(render) {}

    void RenderStatsPanel::refresh_display(bool capturing) {
        const auto now = Comet::TimingHistory::Clock::now();
        const auto summarize = [&](const Comet::TimingHistory& history) {
            auto time = now;
            if(!capturing)
                time = history.last_sample_time().value_or(now);
            return history.summarize(time);
        };
        m_display.frame = summarize(m_frame.history());
        m_display.cpu = summarize(m_render.cpu_history());
        m_display.preparation = summarize(m_render.preparation_history());
        m_display.submission = summarize(m_render.submission_history());
        m_display.gpu = summarize(m_render.gpu_history());
        const auto& snapshot = m_render.get_snapshot();
        m_display.scene_rendered = snapshot.scene_rendered;
        m_display.memory = snapshot.memory;
        m_display.has_memory = snapshot.memory_samples > 0;
        m_display.gpu_supported = snapshot.gpu_supported;
        m_display.gpu_error = snapshot.gpu_error;
        m_display.truncated = snapshot.cpu && snapshot.cpu->truncated;
    }

    void RenderStatsPanel::render() {
        if(!m_user_visible)
            return;
        if(!ImGui::Begin(window_label().c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }
        const bool capturing = m_render.is_enabled();
        bool enabled = capturing;
        if(ImGui::Checkbox("采集数据###Capture", &enabled))
            m_capture_request = enabled;
        ImGui::SameLine();
        if(ImGui::Checkbox("暂停显示###Pause display", &m_paused))
            m_next_refresh = 0;

        if(!m_paused && (ImGui::GetTime() >= m_next_refresh || capturing != m_was_capturing)) {
            refresh_display(capturing);
            m_next_refresh = ImGui::GetTime() + 0.25;
        }
        m_was_capturing = capturing;
        if(m_paused)
            ImGui::TextWrapped("%s", "显示已暂停；启用时仍继续采集。");
        else if(!capturing)
            ImGui::TextWrapped("%s", "已停止采集，显示最后的采样。");
        ImGui::TextWrapped("%s", "近 1 秒均值 / 峰值；显示每 250 毫秒刷新。");
        if(capturing && !m_paused && !m_display.scene_rendered)
            ImGui::TextWrapped("%s", "当前未绘制场景；图耗时仅展示近期历史。");

        if(ImGui::BeginTable("timing_overview", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            show_summary("CPU 整帧（包含等待）", m_display.frame);
            ImGui::TableNextColumn();
            show_summary("GPU 场景渲染图（不含 UI）", m_display.gpu);
            ImGui::EndTable();
        }
        if(!m_display.gpu_supported)
            ImGui::TextWrapped("%s", "设备不支持 GPU 计时，仍可采集 CPU 耗时。");
        else if(!m_display.gpu_error.empty())
            ImGui::TextWrapped("GPU 计时已停用：%s", m_display.gpu_error.c_str());
        ImGui::SeparatorText("显存堆");
        constexpr double mib = 1024.0 * 1024.0;
        double usage = 0;
        double budget = 0;
        for(const auto& heap : m_display.memory.heaps) {
            usage += heap.usage_bytes / mib;
            budget += heap.budget_bytes / mib;
        }
        if(!m_display.has_memory)
            ImGui::TextDisabled("%s", "等待采样");
        else {
            ImGui::Text("用量 %.1f / 预算 %.1f MiB", usage, budget);
            if(m_display.memory.driver_reported)
                ImGui::TextWrapped("%s", "驱动报告；至多每秒采样一次。");
            else
                ImGui::TextWrapped("%s", "VMA 估算；至多每秒采样一次。");
        }
        if(ImGui::CollapsingHeader("耗时趋势（蓝线：均值 / 黄点：峰值）###Timing trends")) {
            if(ImGui::BeginTable("timing_trends", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("CPU 整帧（包含等待）");
                show_trend(m_display.frame);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("GPU 场景渲染图（不含 UI）");
                show_trend(m_display.gpu);
                ImGui::EndTable();
            }
        }
        ImGui::TextWrapped("%s", "CPU 与 GPU 分别统计，不一定来自同一帧。");

        if(ImGui::CollapsingHeader("CPU 阶段明细###CPU phase details")) {
            show_details("frame_phases", m_display.frame);
            ImGui::TextWrapped("%s", "场景提取已计入渲染 / 提交，不重复相加。");
            show_summary("CPU 场景准备（所列阶段）", m_display.preparation);
            show_details("scene_preparation", m_display.preparation);
            ImGui::TextWrapped("%s", "准备阶段已计入渲染 / 提交，不属于渲染图录制耗时。");
            show_summary("CPU 提交与呈现（包含等待）", m_display.submission);
            show_details("submission", m_display.submission);
            ImGui::TextWrapped("%s",
                "提交准备包含命令结束；队列提交与呈现可能等待驱动或窗口系统，均已计入渲染 / 提交。");
        }
        if(ImGui::CollapsingHeader("渲染阶段明细###Render pass details")) {
            show_summary("CPU 渲染图录制", m_display.cpu);
            show_details("cpu_passes", m_display.cpu);
            ImGui::SeparatorText("GPU 场景渲染图（不含 UI）");
            show_details("gpu_passes", m_display.gpu);
            if(m_display.truncated)
                ImGui::TextWrapped("%s", "渲染阶段明细已截断；本图不采集 GPU 耗时。");
        }

        if(ImGui::CollapsingHeader("堆明细###Heap details")) {
            for(size_t index = 0; index < m_display.memory.heaps.size(); ++index) {
                const auto& heap = m_display.memory.heaps[index];
                ImGui::Text("堆 %zu：%.1f / %.1f MiB", index, heap.usage_bytes / mib,
                    heap.budget_bytes / mib);
                ImGui::Text("已分配 %.1f MiB（%u 项）；内存块 %.1f MiB（%u 块）",
                    heap.allocation_bytes / mib, heap.allocation_count, heap.block_bytes / mib,
                    heap.block_count);
                if(heap.budget_bytes > 0)
                    ImGui::ProgressBar(static_cast<float>(std::clamp(
                        double(heap.usage_bytes) / double(heap.budget_bytes), 0.0, 1.0)));
            }
        }
        if(ImGui::Button("保存显存分配报告###Save allocation report"))
            m_allocation_report_request = true;
        ImGui::End();
    }

    std::optional<bool> RenderStatsPanel::take_capture_request() {
        return std::exchange(m_capture_request, std::nullopt);
    }

    bool RenderStatsPanel::take_allocation_report_request() {
        return std::exchange(m_allocation_report_request, false);
    }
}
