#include "project/editor_paths.h"

#include <cstdlib>

namespace CometEditor {
    Comet::Result<std::filesystem::path> editor_user_state_directory() {
        using Result = Comet::Result<std::filesystem::path>;
#ifdef _WIN32
        const char* base = std::getenv("APPDATA");
        if(base && *base)
            return Result::success(std::filesystem::path(base) / "Comet");
        return Result::failure("APPDATA is not available for editor state");
#elif defined(__APPLE__)
        const char* base = std::getenv("HOME");
        if(base && *base)
            return Result::success(std::filesystem::path(base) / "Library/Application Support/Comet");
        return Result::failure("HOME is not available for editor state");
#else
        if(const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
            const std::filesystem::path directory(state);
            if(directory.is_absolute())
                return Result::success(directory / "comet");
        }
        const char* base = std::getenv("HOME");
        if(base && *base)
            return Result::success(std::filesystem::path(base) / ".local/state/comet");
        return Result::failure("HOME is not available for editor state");
#endif
    }

}
