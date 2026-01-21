#pragma once
#include "graphics/vk_common.h"
#include "graphics/shader.h"
#include "graphics/sampler.h"
#include "graphics/pipeline.h"
#include "common/export.h"

namespace Comet {
    class Device;
    class Texture;
    class ShaderManager;
    class SamplerManager;

    enum class MaterialPropertyType {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Int,
        Sampler,
    };

    struct MaterialProperty {
        MaterialPropertyType type;
        std::string name;
        std::variant<float, Math::Vec2, Math::Vec3, Math::Vec4, int, std::shared_ptr<Sampler>> value;
    };

    class COMET_API MaterialConfig {
    public:
        MaterialConfig() = default;
        ~MaterialConfig() = default;

        void set_vertex_shader(const std::shared_ptr<Shader>& shader) { m_vertex_shader = shader; }
        void set_fragment_shader(const std::shared_ptr<Shader>& shader) { m_fragment_shader = shader; }
        void set_cull_mode(const CullMode mode) { m_cull_mode = mode; }
        void set_polygon_mode(const PolygonMode mode) { m_polygon_mode = mode; }
        void set_topology(const Topology topology) { m_topology = topology; }

        void enable_depth_test(const bool enable = true) { m_depth_test_enable = enable; }
        void enable_depth_write(const bool enable = true) { m_depth_write_enable = enable; }
        void set_depth_compare_op(const CompareOp op) { m_depth_compare_op = op; }

        void enable_alpha_blend(const bool enable = true) { m_alpha_blend_enable = enable; }
        void set_blend_op(const BlendOp color_op, const BlendOp alpha_op = BlendOp::Add);
        void set_blend_factors(const BlendFactor src_color, const BlendFactor dst_color,
            const BlendFactor src_alpha = BlendFactor::One, const BlendFactor dst_alpha = BlendFactor::Zero);

        [[nodiscard]] const std::shared_ptr<Shader>& get_vertex_shader() const { return m_vertex_shader; }
        [[nodiscard]] const std::shared_ptr<Shader>& get_fragment_shader() const { return m_fragment_shader; }
        [[nodiscard]] CullMode get_cull_mode() const { return m_cull_mode; }
        [[nodiscard]] PolygonMode get_polygon_mode() const { return m_polygon_mode; }
        [[nodiscard]] Topology get_topology() const { return m_topology; }
        [[nodiscard]] bool is_depth_test_enabled() const { return m_depth_test_enable; }
        [[nodiscard]] bool is_depth_write_enabled() const { return m_depth_write_enable; }
        [[nodiscard]] CompareOp get_depth_compare_op() const { return m_depth_compare_op; }
        [[nodiscard]] bool is_alpha_blend_enabled() const { return m_alpha_blend_enable; }
        [[nodiscard]] BlendOp get_color_blend_op() const { return m_color_blend_op; }
        [[nodiscard]] BlendOp get_alpha_blend_op() const { return m_alpha_blend_op; }
        [[nodiscard]] BlendFactor get_src_color_blend_factor() const { return m_src_color_blend_factor; }
        [[nodiscard]] BlendFactor get_dst_color_blend_factor() const { return m_dst_color_blend_factor; }
        [[nodiscard]] BlendFactor get_src_alpha_blend_factor() const { return m_src_alpha_blend_factor; }
        [[nodiscard]] BlendFactor get_dst_alpha_blend_factor() const { return m_dst_alpha_blend_factor; }

    private:
        std::shared_ptr<Shader> m_vertex_shader;
        std::shared_ptr<Shader> m_fragment_shader;

        CullMode m_cull_mode = CullMode::Back;
        PolygonMode m_polygon_mode = PolygonMode::Fill;
        Topology m_topology = Topology::TriangleList;

        bool m_depth_test_enable = true;
        bool m_depth_write_enable = true;
        CompareOp m_depth_compare_op = CompareOp::Less;

        bool m_alpha_blend_enable = false;
        BlendOp m_color_blend_op = BlendOp::Add;
        BlendOp m_alpha_blend_op = BlendOp::Add;
        BlendFactor m_src_color_blend_factor = BlendFactor::SrcAlpha;
        BlendFactor m_dst_color_blend_factor = BlendFactor::OneMinusSrcAlpha;
        BlendFactor m_src_alpha_blend_factor = BlendFactor::One;
        BlendFactor m_dst_alpha_blend_factor = BlendFactor::Zero;
    };

    class COMET_API Material {
    public:
        Material(const std::string& name, const MaterialConfig& config);
        ~Material() = default;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] const MaterialConfig& get_config() const { return m_config; }
        [[nodiscard]] MaterialConfig& get_config_mut() { return m_config; }

        void set_property(const std::string& name, float value);
        void set_property(const std::string& name, const Math::Vec2& value);
        void set_property(const std::string& name, const Math::Vec3& value);
        void set_property(const std::string& name, const Math::Vec4& value);
        void set_property(const std::string& name, int value);
        void set_property_sampler(const std::string& name, const std::shared_ptr<Sampler>& sampler);

        [[nodiscard]] bool has_property(const std::string& name) const;
        [[nodiscard]] const MaterialProperty& get_property(const std::string& name) const;
        [[nodiscard]] const std::map<std::string, MaterialProperty>& get_properties() const { return m_properties; }

    private:
        std::string m_name;
        MaterialConfig m_config;
        std::map<std::string, MaterialProperty> m_properties;
    };

    class COMET_API MaterialInstance {
    public:
        explicit MaterialInstance(std::shared_ptr<Material> material);
        ~MaterialInstance() = default;

        [[nodiscard]] const std::shared_ptr<Material>& get_material() const { return m_material; }
        [[nodiscard]] const std::shared_ptr<Pipeline>& get_pipeline() const { return m_pipeline; }
        void set_pipeline(const std::shared_ptr<Pipeline>& pipeline) { m_pipeline = pipeline; }

        void set_property(const std::string& name, float value);
        void set_property(const std::string& name, const Math::Vec2& value);
        void set_property(const std::string& name, const Math::Vec3& value);
        void set_property(const std::string& name, const Math::Vec4& value);
        void set_property(const std::string& name, int value);
        void set_property_sampler(const std::string& name, const std::shared_ptr<Sampler>& sampler);

        [[nodiscard]] bool has_property_override(const std::string& name) const;
        [[nodiscard]] const MaterialProperty& get_property_or_default(const std::string& name) const;
        [[nodiscard]] const std::map<std::string, MaterialProperty>& get_property_overrides() const { return m_property_overrides; }

        void clear_property_overrides() { m_property_overrides.clear(); }
        void clear_pipeline() { m_pipeline.reset(); }

    private:
        std::shared_ptr<Material> m_material;
        std::shared_ptr<Pipeline> m_pipeline;
        std::map<std::string, MaterialProperty> m_property_overrides;
    };

    class COMET_API MaterialManager {
    public:
        MaterialManager(Device* device, ShaderManager* shader_manager, SamplerManager* sampler_manager);
        ~MaterialManager() = default;

        std::shared_ptr<Material> create_material(const std::string& name, const MaterialConfig& config);

        [[nodiscard]] std::shared_ptr<Material> get_material(const std::string& name) const;
        [[nodiscard]] std::shared_ptr<MaterialInstance> create_instance(const std::string& material_name) const;
        [[nodiscard]] const std::unordered_map<std::string, std::shared_ptr<Material>>& get_materials() const { return m_materials; }

        void clean_up();

    private:
        std::unordered_map<std::string, std::shared_ptr<Material>> m_materials;
    };
}