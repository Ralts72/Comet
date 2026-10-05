#include "assets/system_text_editor.h"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string_view>

namespace CometEditor::SystemTextEditor {
    namespace {
        std::wstring quote_argument(std::wstring_view argument) {
            std::wstring quoted = L"\"";
            size_t backslashes = 0;
            for(const wchar_t character : argument) {
                if(character == L'\\') {
                    ++backslashes;
                    continue;
                }
                if(character == L'"') {
                    quoted.append(backslashes * 2 + 1, L'\\');
                } else {
                    quoted.append(backslashes, L'\\');
                }
                quoted.push_back(character);
                backslashes = 0;
            }
            quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }
    }

    Comet::Result<void> open(const std::filesystem::path& path) {
        std::wstring directory(MAX_PATH, L'\0');
        UINT length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
        if(length >= directory.size()) {
            directory.resize(static_cast<size_t>(length) + 1);
            length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
        }
        if(length == 0 || length >= directory.size())
            return Comet::Result<void>::failure("Cannot locate the Windows system directory");
        directory.resize(length);
        const auto executable = std::filesystem::path(directory) / L"notepad.exe";
        auto arguments = quote_argument(executable.native()) + L" " + quote_argument(path.native());

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable.c_str(), arguments.data(), nullptr, nullptr, FALSE, 0,
               nullptr, nullptr, &startup, &process))
            return Comet::Result<void>::failure(
                "Cannot start the system text editor (Windows error "
                + std::to_string(GetLastError()) + ")");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return Comet::Result<void>::success();
    }
}

#elif defined(__linux__)
#include <gio/gio.h>

namespace CometEditor::SystemTextEditor {
    Comet::Result<void> open(const std::filesystem::path& path) {
        GAppInfo* application = g_app_info_get_default_for_type("text/plain", FALSE);
        if(!application)
            return Comet::Result<void>::failure(
                "No default text editor is configured for text/plain");

        GFile* file = g_file_new_for_path(path.c_str());
        GList files{file, nullptr, nullptr};
        GError* launch_error = nullptr;
        const gboolean launched = g_app_info_launch(application, &files, nullptr, &launch_error);
        g_object_unref(file);
        g_object_unref(application);
        if(!launched) {
            std::string message = "Cannot send the source file to the text editor";
            if(launch_error) {
                message += ": ";
                message += launch_error->message;
                g_error_free(launch_error);
            }
            return Comet::Result<void>::failure(message);
        }
        return Comet::Result<void>::success();
    }
}

#else
namespace CometEditor::SystemTextEditor {
    Comet::Result<void> open(const std::filesystem::path&) {
        return Comet::Result<void>::failure(
            "Opening a text editor is not supported on this platform");
    }
}
#endif
