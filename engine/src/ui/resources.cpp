#include "ui/resources.h"

#include <algorithm>
#include <string>

namespace Comet::Ui {
    const std::filesystem::path& builtin_resource_directory() {
        static const std::filesystem::path directory(COMET_UI_RESOURCE_DIRECTORY);
        return directory;
    }

    Result<std::filesystem::path> resolve_builtin_resource(const std::string_view source) {
        using Resolved = Result<std::filesystem::path>;
        const auto fail = [&](std::string_view reason) {
            return Resolved::failure(std::string(reason) + ": " + std::string(source));
        };
        if(!source.starts_with(builtin_resource_prefix))
            return fail("Unknown built-in UI resource");
        const auto name = source.substr(builtin_resource_prefix.size());
        const std::filesystem::path relative(name);
        if(relative.empty() || relative.is_absolute() || relative.has_root_name()
            || name.find_first_of(":\\") != std::string_view::npos
            || name.find('\0') != std::string_view::npos
            || std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
            return fail("Invalid built-in UI resource path");
        std::error_code error;
        const auto root = std::filesystem::canonical(builtin_resource_directory(), error);
        if(error)
            return fail("Cannot open built-in UI resource directory");
        const auto path = std::filesystem::canonical(root / relative, error);
        if(error)
            return fail("Cannot resolve built-in UI resource");
        const auto resolved = path.lexically_relative(root);
        if(resolved.empty() || resolved.is_absolute() || *resolved.begin() == "..")
            return fail("Built-in UI resource is outside its directory");
        if(!std::filesystem::is_regular_file(path, error) || error)
            return fail("Built-in UI resource is not a regular file");
        return Resolved::success(path);
    }
}
