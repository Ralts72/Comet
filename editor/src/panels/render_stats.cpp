#include "panels/render_stats.h"
#include "core/engine.h"

#include <imgui.h>
#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        void show_timing(
            const char* label, const Comet::RenderDiagnostics::FrameTiming& timing) {
            ImGui::Text("%s: %.3f ms (submission %llu)", label, timing.milliseconds,
                static_cast<unsigned long long>(timing.serial));
            if(timing.truncated)
                ImGui::TextUnformatted(
                    "Pass detail truncated; GPU timing skipped for this graph.");
            if(ImGui::BeginTable(label, 2, ImGuiTableFlags_SizingStretchProp)) {
                for(const auto& pass : timing.passes) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(pass.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f ms", pass.milliseconds);
                }
                ImGui::EndTable();
            }
        }
    }

    RenderStatsPanel::RenderStatsPanel(const Comet::Engine& engine)
        : EditorPanel("Render Stats"), m_engine(engine) {
        m_user_visible = false;
    }

    void RenderStatsPanel::render() {
        if(!m_user_visible)
            return;
        if(!ImGui::Begin(m_name.c_str(), &m_user_visible)) {
            ImGui::End();
            return;
        }
        const auto& diagnostics =
            m_engine.get_renderer().get_scene_renderer().get_diagnostics();
        const auto& snapshot = diagnostics.get_snapshot();
        bool enabled = diagnostics.is_enabled();
        if(ImGui::Checkbox("Capture", &enabled))
            m_capture_request = enabled;
        if(ImGui::Button("Save allocation report"))
            m_allocation_report_request = true;
        if(!diagnostics.is_enabled())
            ImGui::TextUnformatted("Capture disabled; showing last completed samples.");
        if(const auto& timing = m_engine.get_frame_timing()) {
            ImGui::SeparatorText("CPU frame wall time (includes waits)");
            ImGui::Text("Frame %d: %.3f ms%s", timing->frame_index, timing->total_ms,
                timing->rendered ? "" : " (presentation skipped)");
            ImGui::Text(
                "Events %.3f | Update %.3f", timing->events_ms, timing->update_ms);
            ImGui::Text("Wait / prepare / UI %.3f | Render / submit %.3f",
                timing->prepare_ms, timing->render_submit_ms);
        }
        ImGui::SeparatorText("Scene graph (UI rendering excluded)");
        if(snapshot.cpu)
            show_timing("CPU recording", *snapshot.cpu);
        if(!snapshot.gpu_supported)
            ImGui::TextUnformatted(
                "GPU timestamps unavailable; CPU capture remains usable.");
        else if(!snapshot.gpu_error.empty())
            ImGui::TextWrapped("GPU timing disabled: %s", snapshot.gpu_error.c_str());
        else if(snapshot.gpu)
            show_timing("GPU completed", *snapshot.gpu);
        else
            ImGui::TextUnformatted("GPU: waiting for a completed submission.");
        ImGui::SeparatorText("VMA heaps (sampled at most once per second)");
        if(snapshot.memory_samples == 0)
            ImGui::TextUnformatted("Memory: no sample captured.");
        else
            ImGui::TextUnformatted(snapshot.memory.driver_reported
                                       ? "Usage / budget: driver reported"
                                       : "Usage / budget: VMA estimate");
        constexpr double mib = 1024.0 * 1024.0;
        for(size_t index = 0; index < snapshot.memory.heaps.size(); ++index) {
            const auto& heap = snapshot.memory.heaps[index];
            ImGui::Text("Heap %zu: %.1f / %.1f MiB", index, heap.usage_bytes / mib,
                heap.budget_bytes / mib);
            ImGui::Text("Allocated %.1f MiB (%u) | Blocks %.1f MiB (%u)",
                heap.allocation_bytes / mib, heap.allocation_count,
                heap.block_bytes / mib, heap.block_count);
            if(heap.budget_bytes > 0)
                ImGui::ProgressBar(static_cast<float>(std::clamp(
                    double(heap.usage_bytes) / double(heap.budget_bytes), 0.0, 1.0)));
        }
        ImGui::End();
    }

    std::optional<bool> RenderStatsPanel::take_capture_request() {
        return std::exchange(m_capture_request, std::nullopt);
    }

    bool RenderStatsPanel::take_allocation_report_request() {
        return std::exchange(m_allocation_report_request, false);
    }
}
