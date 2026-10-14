#include "project/app_export.h"
#include <cassert>
#include <utility>

#ifdef __APPLE__
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <crt_externs.h>
#endif

namespace CometEditor {
    Comet::Result<void> export_app(
        const AppExportRequest& request, const std::function<void(std::string_view)>& on_output) {
#ifndef __APPLE__
        return Comet::Result<void>::failure("App 打包目前支持 macOS；其他平台可使用资源目录导出。");
#else
        std::vector<std::string> arguments{COMET_EXPORT_PYTHON, COMET_EXPORT_SCRIPT,
            request.project.string(), request.destination.string(), "--cmake", COMET_EXPORT_CMAKE};
        for(const auto& scene : request.scenes) {
            arguments.emplace_back("--scene");
            arguments.push_back(scene.generic_string());
        }
        std::vector<char*> argv;
        for(auto& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);

        int output[2];
        if(pipe(output) != 0)
            return Comet::Result<void>::failure(std::strerror(errno));
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, output[0]);
        posix_spawn_file_actions_addclose(&actions, output[1]);
        pid_t process;
        const int error =
            posix_spawn(&process, argv.front(), &actions, nullptr, argv.data(), *_NSGetEnviron());
        posix_spawn_file_actions_destroy(&actions);
        close(output[1]);
        if(error != 0) {
            close(output[0]);
            return Comet::Result<void>::failure(
                std::string("Cannot start App exporter: ") + std::strerror(error));
        }
        std::string message;
        char buffer[4096];
        for(;;) {
            const auto count = read(output[0], buffer, sizeof(buffer));
            if(count < 0 && errno == EINTR)
                continue;
            if(count <= 0)
                break;
            message.append(buffer, static_cast<std::size_t>(count));
            if(on_output)
                on_output({buffer, static_cast<std::size_t>(count)});
            if(message.size() > 65536)
                message.erase(0, message.size() - 65536);
        }
        close(output[0]);
        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(process, &status, 0);
        } while(waited < 0 && errno == EINTR);
        if(waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return Comet::Result<void>::failure(
                message.empty() ? "App export process failed" : message);
        return Comet::Result<void>::success();
#endif
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
