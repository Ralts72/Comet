#include "common/player_settings_path.h"

#include <cstdlib>

namespace Comet {
    Result<std::filesystem::path> player_settings_directory(Uuid project_id) {
        using Path = Result<std::filesystem::path>;
        if(!project_id)
            return Path::failure("Player settings require a non-zero project ID");
#ifdef _WIN32
        const char* variable = "APPDATA";
        const std::filesystem::path suffix = "Comet";
#elif defined(__APPLE__)
        const char* variable = "HOME";
        const std::filesystem::path suffix = "Library/Application Support/Comet";
#else
        const char* configured = std::getenv("XDG_CONFIG_HOME");
        const bool use_xdg = configured && *configured;
        const char* variable = use_xdg ? "XDG_CONFIG_HOME" : "HOME";
        const std::filesystem::path suffix = use_xdg ? "comet" : ".config/comet";
#endif
        const char* value = std::getenv(variable);
        if(!value || !*value)
            return Path::failure(std::string(variable) + " is unavailable for player settings");
        const std::filesystem::path base(value);
        if(!base.is_absolute())
            return Path::failure(std::string(variable) + " must be absolute for player settings");
        return Path::success(base / suffix / "players" / project_id.to_string() / "default");
    }
}
