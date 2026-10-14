#include "project/app_export.h"

#ifdef __APPLE__
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <crt_externs.h>
#endif

namespace CometEditor {
    Comet::Result<void> export_app(const AppExportRequest& request) {
#ifndef __APPLE__
        return Comet::Result<void>::failure("App 打包目前支持 macOS；其他平台可使用资源目录导出。");
#else
        std::vector<std::string> arguments{COMET_EXPORT_PYTHON, COMET_EXPORT_SCRIPT,
            request.project.string(), request.destination.string(), "--build",
            request.build.string()};
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
}
