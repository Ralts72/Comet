#include "config/config_loader.h"
#include "common/file_io.h"
#include "common/json.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace Comet {
    namespace {
        constexpr std::array SURFACE_FORMATS = {std::pair{"bgra8_srgb", Format::B8G8R8A8_SRGB},
            std::pair{"bgra8_unorm", Format::B8G8R8A8_UNORM},
            std::pair{"rgba8_srgb", Format::R8G8B8A8_SRGB},
            std::pair{"rgba8_unorm", Format::R8G8B8A8_UNORM}};
        constexpr std::array COLOR_SPACES = {
            std::pair{"srgb_nonlinear", ImageColorSpace::SrgbNonlinearKHR}};
        constexpr std::array DEPTH_FORMATS = {std::pair{"d32_float", Format::D32_SFLOAT},
            std::pair{"d24_unorm_s8_uint", Format::D24_UNORM_S8_UINT},
            std::pair{"d32_float_s8_uint", Format::D32_SFLOAT_S8_UINT}};
        std::string config_error(std::string_view path, std::string_view profile,
            std::string_view key, std::string_view detail) {
            const auto kind = "config profile " + std::string(profile);
            return Json::Context(kind, path).error(key, detail);
        }
        class ConfigReader {
        public:
            ConfigReader(Json::Node root, const Json::Context& context)
                : m_root(root), m_context(context) {}
            bool keys(std::string_view section, std::initializer_list<std::string_view> allowed) {
                std::optional<Json::Node> node;
                if(!find(section, node))
                    return false;
                if(!node)
                    return true;
                auto fields = m_context.object(*node, section.empty() ? "<root>" : section);
                if(!fields) {
                    m_error = fields.error();
                    return false;
                }
                for(const auto entry : fields.value()) {
                    const auto key = entry.key;
                    if(std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
                        auto location = std::string(section);
                        if(!location.empty())
                            location += '.';
                        return fail(location + std::string(key), "unknown developer setting");
                    }
                }
                return true;
            }
            template<typename T>
            bool read(std::string_view key, T& value, std::string_view expected) {
                std::optional<Json::Node> node;
                if(!find(key, node))
                    return false;
                if(!node)
                    return true;
                auto candidate = m_context.read_scalar<T>(*node, key, expected);
                if(!candidate) {
                    m_error = candidate.error();
                    return false;
                }
                value = std::move(candidate).value();
                return true;
            }
            template<typename T, std::size_t Size>
            bool named(std::string_view key, T& value,
                const std::array<std::pair<const char*, T>, Size>& names) {
                std::optional<Json::Node> node;
                if(!find(key, node))
                    return false;
                if(!node)
                    return true;
                auto parsed = m_context.read_scalar<std::string>(*node, key, "a string");
                if(!parsed) {
                    m_error = parsed.error();
                    return false;
                }
                const auto& name = parsed.value();
                std::string expected;
                for(const auto& [label, candidate] : names) {
                    if(name == label) {
                        value = candidate;
                        return true;
                    }
                    if(!expected.empty())
                        expected += ", ";
                    expected += label;
                }
                return fail(key, "unknown value '" + name + "'; expected one of: " + expected);
            }
            bool megabytes(std::string_view key, std::size_t& bytes) {
                return scaled_bytes(key, bytes, 1024 * 1024, "an integer number of MiB");
            }
            bool kilobytes(std::string_view key, std::size_t& bytes) {
                return scaled_bytes(key, bytes, 1024, "an integer number of KiB");
            }
            const std::string& error() const { return m_error; }

        private:
            bool scaled_bytes(std::string_view key, std::size_t& bytes, const std::size_t scale,
                std::string_view expected) {
                std::size_t units = bytes / scale;
                if(!read(key, units, expected))
                    return false;
                if(units == 0 || units > std::numeric_limits<std::size_t>::max() / scale)
                    return fail(key, "must be a positive value that fits in memory size");
                bytes = units * scale;
                return true;
            }
            bool fail(std::string_view key, std::string_view detail) {
                m_error = m_context.error(key, detail);
                return false;
            }
            bool find(std::string_view key, std::optional<Json::Node>& output) {
                auto node = m_context.find(m_root, key);
                if(!node) {
                    m_error = node.error();
                    return false;
                }
                output = node.value();
                return true;
            }
            Json::Node m_root;
            const Json::Context& m_context;
            std::string m_error;
        };
        Result<void> read_profile(
            Config& config, const std::string& path, const std::string_view profile) {
            auto text = read_text_file(path);
            if(!text)
                return Result<void>::failure(text.error());
            const auto kind = "config profile " + std::string(profile);
            const Json::Context context(kind, path);
            simdjson::dom::parser parser;
            auto root = context.parse(parser, text.value());
            if(!root)
                return Result<void>::failure(root.error());
            if(auto fields = context.object(root.value(), "<root>"); !fields)
                return Result<void>::failure(fields.error());
            auto selected = context.required_child(root.value(), profile);
            if(!selected)
                return Result<void>::failure(selected.error());
            ConfigReader reader(selected.value(), context);
            if(!reader.keys("", {"diagnostics", "vulkan", "render", "assets"})
                || !reader.keys(
                    "diagnostics", {"enable_file_logging", "log_level", "enable_profiler",
                                       "enable_render_diagnostics", "enable_validation"})
                || !reader.keys("vulkan",
                    {"surface_format", "color_space", "depth_format", "swapchain_image_count"})
                || !reader.keys("render", {"max_frames_in_flight"})
                || !reader.keys("assets",
                    {"async", "source_max_mib", "texture_working_mib", "mesh_working_mib",
                        "mesh_owner_inspect_kib", "external_file_mib", "external_file_queue"})
                || !reader.keys("assets.async", {"in_flight", "queued", "working_mib"}))
                return Result<void>::failure(reader.error());
            if(!reader.read("diagnostics.enable_file_logging",
                   config.diagnostics.log.enable_file_logging, "a boolean")
                || !reader.read("diagnostics.log_level", config.diagnostics.log.level, "a string")
                || !reader.read(
                    "diagnostics.enable_profiler", config.diagnostics.enable_profiler, "a boolean")
                || !reader.read("diagnostics.enable_render_diagnostics",
                    config.diagnostics.enable_render_diagnostics, "a boolean")
                || !reader.named(
                    "vulkan.surface_format", config.vulkan.surface_format, SURFACE_FORMATS)
                || !reader.named("vulkan.color_space", config.vulkan.color_space, COLOR_SPACES)
                || !reader.named("vulkan.depth_format", config.vulkan.depth_format, DEPTH_FORMATS)
                || !reader.read("vulkan.swapchain_image_count", config.vulkan.swapchain_image_count,
                    "a non-negative integer")
                || !reader.read(
                    "diagnostics.enable_validation", config.vulkan.enable_validation, "a boolean")
                || !reader.read("render.max_frames_in_flight", config.render.max_frames_in_flight,
                    "a non-negative integer"))
                return Result<void>::failure(reader.error());
            if(!reader.read(
                   "assets.async.in_flight", config.assets.async.in_flight, "a positive integer")
                || !reader.read(
                    "assets.async.queued", config.assets.async.queued, "a positive integer")
                || !reader.megabytes("assets.async.working_mib", config.assets.async.working_bytes)
                || !reader.megabytes("assets.source_max_mib", config.assets.source_bytes)
                || !reader.megabytes(
                    "assets.texture_working_mib", config.assets.texture_working_bytes)
                || !reader.megabytes("assets.mesh_working_mib", config.assets.mesh_working_bytes)
                || !reader.kilobytes(
                    "assets.mesh_owner_inspect_kib", config.assets.mesh_owner_inspect_bytes)
                || !reader.megabytes("assets.external_file_mib", config.assets.external_file_bytes)
                || !reader.read("assets.external_file_queue", config.assets.external_file_queue,
                    "a positive integer"))
                return Result<void>::failure(reader.error());
            return Result<void>::success();
        }
    }
    Result<Config> ConfigLoader::load(
        const std::string& path, const std::string_view profile) const {
        Config config;
        if(auto result = read_profile(config, path, profile); !result)
            return Result<Config>::failure(result.error());
        if(config.vulkan.swapchain_image_count == 0)
            return Result<Config>::failure(config_error(
                path, profile, "vulkan.swapchain_image_count", "must be greater than zero"));
        if(config.render.max_frames_in_flight == 0)
            return Result<Config>::failure(config_error(
                path, profile, "render.max_frames_in_flight", "must be greater than zero"));
        if(config.assets.async.in_flight == 0 || config.assets.async.queued == 0
            || config.assets.external_file_queue == 0)
            return Result<Config>::failure(
                config_error(path, profile, "assets", "queue counts must be positive"));
        if(config.assets.async.in_flight > 64 || config.assets.async.queued > 4096
            || config.assets.external_file_queue > 256)
            return Result<Config>::failure(
                config_error(path, profile, "assets", "queue counts exceed supported limits"));
        if(config.assets.source_bytes > 1024ull * 1024 * 1024)
            return Result<Config>::failure(
                config_error(path, profile, "assets.source_max_mib", "must not exceed 1024 MiB"));
        if(config.assets.texture_working_bytes > 4ull * 1024 * 1024 * 1024
            || config.assets.mesh_working_bytes > 4ull * 1024 * 1024 * 1024
            || config.assets.async.working_bytes > 16ull * 1024 * 1024 * 1024)
            return Result<Config>::failure(
                config_error(path, profile, "assets", "working budgets exceed supported limits"));
        if(config.assets.texture_working_bytes <= 16ull * 1024 * 1024
            || config.assets.mesh_working_bytes <= 16ull * 1024 * 1024)
            return Result<Config>::failure(config_error(
                path, profile, "assets", "texture and mesh working budgets must exceed 16 MiB"));
        if(config.assets.mesh_owner_inspect_bytes > config.assets.source_bytes
            || config.assets.mesh_owner_inspect_bytes > 1024 * 1024)
            return Result<Config>::failure(config_error(path, profile,
                "assets.mesh_owner_inspect_kib", "must not exceed 1 MiB or the source limit"));
        return Result<Config>::success(std::move(config));
    }
}
