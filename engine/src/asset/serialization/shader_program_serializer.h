#pragma once

#include "asset/data/shader_program_data.h"
#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API ShaderProgramSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] Result<std::string> serialize(const ShaderProgramData& data) const;
        [[nodiscard]] Result<ShaderProgramData> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;
        [[nodiscard]] Result<void> save(
            const ShaderProgramData& data, const std::filesystem::path& path) const;
        [[nodiscard]] Result<ShaderProgramData> load(const std::filesystem::path& path) const;
        [[nodiscard]] Result<std::string> serialize_material(
            const ShaderProgramMaterial& material) const;
        [[nodiscard]] Result<ShaderProgramMaterial> deserialize_material(
            std::string_view contents) const;
    };
}
