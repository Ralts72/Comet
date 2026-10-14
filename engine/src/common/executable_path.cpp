#include "common/executable_path.h"
#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Comet {
    std::filesystem::path executable_directory() {
        std::error_code error;
#if defined(__APPLE__)
        std::vector<char> path(1024);
        auto size = static_cast<std::uint32_t>(path.size());
        if(_NSGetExecutablePath(path.data(), &size) != 0) {
            path.resize(size);
            if(_NSGetExecutablePath(path.data(), &size) != 0)
                return {};
        }
        const auto resolved = std::filesystem::weakly_canonical(path.data(), error);
        return error ? std::filesystem::path{} : resolved.parent_path();
#elif defined(_WIN32)
        std::vector<wchar_t> path(32768);
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if(size == 0 || size == path.size())
            return {};
        return std::filesystem::path(std::wstring(path.data(), size)).parent_path();
#else
        const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path{} : path.parent_path();
#endif
    }

    std::filesystem::path executable_resource_directory() {
        const auto directory = executable_directory();
        if(directory.empty())
            return {};
#if defined(__APPLE__)
        if(directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents")
            return directory.parent_path() / "Resources";
#endif
        return directory / "resources";
    }
}
