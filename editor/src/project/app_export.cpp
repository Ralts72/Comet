#include "project/app_export.h"
#include <cassert>
#include <utility>

#include "common/process.h"

namespace CometEditor {
    Comet::Result<void> export_app(
        const AppExportRequest& request, const std::function<void(std::string_view)>& on_output) {
        const auto utf8 = [](const std::filesystem::path& path) {
            const auto value = path.u8string();
            return std::string(value.begin(), value.end());
        };
        std::vector<std::string> arguments{COMET_EXPORT_PYTHON, COMET_EXPORT_SCRIPT,
            utf8(request.project), utf8(request.destination), "--cmake", COMET_EXPORT_CMAKE};
        for(const auto& scene : request.scenes) {
            arguments.emplace_back("--scene");
            arguments.push_back(utf8(scene));
        }
        std::string message;
        auto result = Comet::run_process(arguments, [&](std::string_view output) {
            message.append(output);
            if(on_output)
                on_output(output);
            if(message.size() > 65536)
                message.erase(0, message.size() - 65536);
        });
        if(!result)
            return Comet::Result<void>::failure(result.error());
        if(result.value())
            return Comet::Result<void>::failure(
                message.empty() ? "App export process failed" : message);
        return Comet::Result<void>::success();
    }

    void AppExportTask::start(AppExportRequest request) {
        assert(!m_future.valid());
        m_output.clear();
        m_future = std::async(std::launch::async, [this, request = std::move(request)] {
            try {
                return export_app(request, [this](std::string_view output) {
                    const std::lock_guard lock(m_mutex);
                    m_output.append(output);
                    if(m_output.size() > 65536)
                        m_output.erase(0, m_output.size() - 65536);
                });
            } catch(const std::exception& error) {
                return Comet::Result<void>::failure(error.what());
            }
        });
    }

    std::string AppExportTask::take_output() {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_output, {});
    }

    std::optional<Comet::Result<void>> AppExportTask::take_result() {
        if(m_future.valid()
            && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            return m_future.get();
        return std::nullopt;
    }
}
