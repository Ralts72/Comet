#include "render/render_stats.h"
#include "core/engine.h"
#include "render/renderer.h"
#include "render/render_diagnostics.h"

#include <imgui.h>
#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        using Summary = Comet::TimingHistory::Summary;

        void show_trend(const Summary& summary) {
            float maximum = 0.1f;
            for(const auto& point : summary.trend)
                maximum = std::max(maximum, static_cast<float>(point.maximum));
            ImGui::TextDisabled("%s (0 - %.2f ms)", Ui::text("5 s trend: average / peak"), maximum);
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
            ImGui::SeparatorText(Ui::text(title));
            if(summary.total.count == 0)
                ImGui::TextDisabled("%s", Ui::text("Waiting for samples"));
            else {
                ImGui::Text(Ui::text("Average %.3f ms"), summary.total.average());
                ImGui::TextDisabled(Ui::text("Peak %.3f ms | %zu samples"), summary.total.maximum,
                    summary.total.count);
            }
        }

        void show_details(const char* id, const Summary& summary) {
            if(!ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchProp))
                return;
            ImGui::TableSetupColumn(Ui::text("Phase"), 0, 2.0f);
            ImGui::TableSetupColumn(Ui::text("Average (ms)"));
            ImGui::TableSetupColumn(Ui::text("Peak (ms)"));
            ImGui::TableHeadersRow();
            for(const auto& detail : summary.details) {
                if(detail.timing.count == 0)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Ui::text(detail.name.c_str()));
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", detail.timing.average());
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", detail.timing.maximum);
            }
            ImGui::EndTable();
        }
    }

    RenderStatsPanel::RenderStatsPanel(const Comet::Engine& engine)
        : EditorPanel("Render Stats"), m_engine(engine) {}

    void RenderStatsPanel::refresh_display(bool capturing) {
        const auto& diagnostics = m_engine.get_renderer().get_diagnostics();
        const auto now = Comet::TimingHistory::Clock::now();
        const auto summarize = [&](const Comet::TimingHistory& history) {
            auto time = now;
            if(!capturing)
                time = history.last_sample_time().value_or(now);
            return history.summarize(time);
        };
        m_display.frame = summarize(m_engine.frame_history());
        m_display.cpu = summarize(diagnostics.cpu_history());
        m_display.gpu = summarize(diagnostics.gpu_history());
        const auto& snapshot = diagnostics.get_snapshot();
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
        const bool capturing = m_engine.get_renderer().get_diagnostics().is_enabled();
        bool enabled = capturing;
        if(ImGui::Checkbox(Ui::label("Capture").c_str(), &enabled))
            m_capture_request = enabled;
        ImGui::SameLine();
        if(ImGui::Checkbox(Ui::label("Pause display").c_str(), &m_paused))
            m_next_refresh = 0;

        if(!m_paused && (ImGui::GetTime() >= m_next_refresh || capturing != m_was_capturing)) {
            refresh_display(capturing);
            m_next_refresh = ImGui::GetTime() + 0.25;
        }
        m_was_capturing = capturing;
        if(m_paused)
            ImGui::TextWrapped("%s", Ui::text("Display paused; collection continues if enabled."));
        else if(!capturing)
            ImGui::TextWrapped("%s", Ui::text("Capture stopped; showing the last samples."));
        ImGui::TextWrapped("%s", Ui::text("Last ~1 s average / peak; display refresh 250 ms."));
        if(capturing && !m_paused && !m_display.scene_rendered)
            ImGui::TextWrapped(
                "%s", Ui::text("Scene rendering skipped; graph timings only show recent history."));

        if(ImGui::BeginTable("timing_overview", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            show_summary("CPU frame (including waits)", m_display.frame);
            ImGui::TableNextColumn();
            show_summary("GPU scene graph (excluding UI)", m_display.gpu);
            ImGui::EndTable();
        }
        if(!m_display.gpu_supported)
            ImGui::TextWrapped(
                "%s", Ui::text("GPU timing unsupported; CPU sampling is available."));
        else if(!m_display.gpu_error.empty())
            ImGui::TextWrapped(Ui::text("GPU timing disabled: %s"), m_display.gpu_error.c_str());
        ImGui::SeparatorText(Ui::text("Memory heaps"));
        constexpr double mib = 1024.0 * 1024.0;
        double usage = 0;
        double budget = 0;
        for(const auto& heap : m_display.memory.heaps) {
            usage += heap.usage_bytes / mib;
            budget += heap.budget_bytes / mib;
        }
        if(!m_display.has_memory)
            ImGui::TextDisabled("%s", Ui::text("Waiting for samples"));
        else {
            ImGui::Text(Ui::text("Usage %.1f / Budget %.1f MiB"), usage, budget);
            if(m_display.memory.driver_reported)
                ImGui::TextWrapped(
                    "%s", Ui::text("Driver reported; sampled at most once per second."));
            else
                ImGui::TextWrapped(
                    "%s", Ui::text("VMA estimate; sampled at most once per second."));
        }
        if(ImGui::CollapsingHeader(Ui::label("Timing trends").c_str())) {
            if(ImGui::BeginTable("timing_trends", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Ui::text("CPU frame (including waits)"));
                show_trend(m_display.frame);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Ui::text("GPU scene graph (excluding UI)"));
                show_trend(m_display.gpu);
                ImGui::EndTable();
            }
        }
        ImGui::TextWrapped("%s",
            Ui::text(
                "CPU and GPU are sampled independently, not necessarily from the same frame."));

        if(ImGui::CollapsingHeader(Ui::label("CPU phase details").c_str()))
            show_details("frame_phases", m_display.frame);
        if(ImGui::CollapsingHeader(Ui::label("Render pass details").c_str())) {
            show_summary("CPU graph recording", m_display.cpu);
            show_details("cpu_passes", m_display.cpu);
            ImGui::SeparatorText(Ui::text("GPU scene graph (excluding UI)"));
            show_details("gpu_passes", m_display.gpu);
            if(m_display.truncated)
                ImGui::TextWrapped(
                    "%s", Ui::text("Pass details truncated; GPU timing is skipped."));
        }

        if(ImGui::CollapsingHeader(Ui::label("Heap details").c_str())) {
            for(size_t index = 0; index < m_display.memory.heaps.size(); ++index) {
                const auto& heap = m_display.memory.heaps[index];
                ImGui::Text(Ui::text("Heap %zu: %.1f / %.1f MiB"), index, heap.usage_bytes / mib,
                    heap.budget_bytes / mib);
                ImGui::Text(Ui::text("Allocated %.1f MiB (%u); blocks %.1f MiB (%u)"),
                    heap.allocation_bytes / mib, heap.allocation_count, heap.block_bytes / mib,
                    heap.block_count);
                if(heap.budget_bytes > 0)
                    ImGui::ProgressBar(static_cast<float>(std::clamp(
                        double(heap.usage_bytes) / double(heap.budget_bytes), 0.0, 1.0)));
            }
        }
        if(ImGui::Button(Ui::label("Save allocation report").c_str()))
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
