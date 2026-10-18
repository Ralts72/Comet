#include "common/process.h"
#include "common/scope_exit.h"
#include <vector>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace Comet {
    std::wstring quote_windows_argument(std::wstring_view argument) {
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for(const wchar_t character : argument) {
            if(character == L'\\') {
                ++backslashes;
                continue;
            }
            quoted.append(
                backslashes * (character == L'"' ? 2 : 1) + (character == L'"' ? 1 : 0), L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    Result<int> run_process(std::span<const std::string> arguments,
        const std::function<void(std::string_view)>& on_output) {
        if(arguments.empty() || arguments.front().empty())
            return Result<int>::failure("Process requires an executable");
        for(const auto& argument : arguments)
            if(argument.find('\0') != std::string::npos)
                return Result<int>::failure("Process argument contains NUL");
#ifdef _WIN32
        std::wstring command;
        std::wstring executable;
        for(const auto& argument : arguments) {
            const int length = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(), int(argument.size()), nullptr, 0);
            if(!argument.empty() && !length)
                return Result<int>::failure("Process argument is not valid UTF-8");
            std::wstring wide(size_t(length), L'\0');
            if(length)
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument.data(),
                    int(argument.size()), wide.data(), length);
            if(command.empty())
                executable = wide;
            else
                command.push_back(L' ');
            command += quote_windows_argument(wide);
        }
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE reader = nullptr, writer = nullptr;
        if(!CreatePipe(&reader, &writer, &attributes, 0))
            return Result<int>::failure("Cannot create process output pipe");
        ScopeExit close_reader([&] { CloseHandle(reader); });
        SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
        const auto input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes, OPEN_EXISTING, 0, nullptr);
        const ScopeExit close_input([&] {
            if(input != INVALID_HANDLE_VALUE)
                CloseHandle(input);
        });
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = startup.hStdError = writer;
        PROCESS_INFORMATION process{};
        const bool started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        const auto error = GetLastError();
        CloseHandle(writer);
        if(!started)
            return Result<int>::failure(
                "Cannot start process: " + std::system_category().message(error));
        const ScopeExit close_process([&] {
            CloseHandle(reader);
            close_reader.release();
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        });
        char buffer[4096];
        DWORD count = 0;
        while(ReadFile(reader, buffer, sizeof(buffer), &count, nullptr) && count)
            if(on_output)
                on_output({buffer, count});
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD status = 0;
        if(!GetExitCodeProcess(process.hProcess, &status))
            return Result<int>::failure("Cannot obtain process exit code");
        return Result<int>::success(int(status));
#else
        std::vector<char*> argv;
        for(const auto& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        int output[2];
        if(pipe(output) != 0)
            return Result<int>::failure(std::strerror(errno));
        ScopeExit close_output([&] { close(output[0]); });
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, output[0]);
        posix_spawn_file_actions_addclose(&actions, output[1]);
#ifdef __APPLE__
        auto environment = *_NSGetEnviron();
#else
        auto environment = environ;
#endif
        pid_t process;
        const int error =
            posix_spawnp(&process, argv.front(), &actions, nullptr, argv.data(), environment);
        posix_spawn_file_actions_destroy(&actions);
        close(output[1]);
        if(error)
            return Result<int>::failure(std::strerror(error));
        int status = 0;
        ScopeExit reap([&] {
            close(output[0]);
            close_output.release();
            while(waitpid(process, &status, 0) < 0 && errno == EINTR) {}
        });
        char buffer[4096];
        for(;;) {
            const auto count = read(output[0], buffer, sizeof(buffer));
            if(count < 0 && errno == EINTR)
                continue;
            if(count <= 0)
                break;
            if(on_output)
                on_output({buffer, size_t(count)});
        }
        pid_t waited;
        do {
            waited = waitpid(process, &status, 0);
        } while(waited < 0 && errno == EINTR);
        if(waited < 0)
            return Result<int>::failure(std::strerror(errno));
        reap.release();
        return Result<int>::success(
            WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
#endif
    }
}
