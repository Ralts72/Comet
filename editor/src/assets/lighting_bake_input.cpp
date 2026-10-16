#include "assets/lighting_bake_input.h"

#include "asset/default_assets.h"
#include "asset/import/input_snapshot.h"
#include "common/binary.h"
#include "render/material/material_layout.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene.h"

#include <algorithm>
#include <set>

namespace CometEditor::LightingBakeInput {
    using namespace Comet;
    namespace {
        bool static_geometry(Scene& scene, Entity entity) {
            while(entity) {
                if(entity.has_component<AnimatorComponent>()
                    || entity.has_component<CharacterControllerComponent>()
                    || (entity.has_component<RigidBodyComponent>()
                        && entity.get_component<RigidBodyComponent>().motion != BodyMotion::Static))
                    return false;
                entity = scene.get_parent(entity);
            }
            return true;
        }
    }

    Result<Snapshot> describe(Scene& scene, const AssetDatabase& assets, const Purpose purpose,
        const uint64_t shader_fingerprint, SourceCache* source_cache) {
        using Described = Result<Snapshot>;
        Snapshot input{.scene = SceneExtractor::extract(scene)};
        std::erase_if(input.scene.render_items, [&](const auto& item) {
            return !static_geometry(scene, scene.find_entity(item.entity_id));
        });
        std::erase_if(input.scene.lights, [&](const auto& light) {
            return !static_geometry(scene, scene.find_entity(light.entity_id));
        });
        const auto by_uuid = [&](const auto& left, const auto& right) {
            return scene.find_entity(left.entity_id).get_uuid()
                   < scene.find_entity(right.entity_id).get_uuid();
        };
        std::ranges::sort(input.scene.render_items, by_uuid);
        std::ranges::sort(input.scene.lights, by_uuid);
        input.scene.reflection_probes.clear();
        input.scene.particles.reset();
        input.scene.environment.fog_enabled = false;
        input.scene.post_process = {};
        if(purpose == Purpose::Irradiance) {
            input.scene.irradiance_volumes.clear(); // 不把旧 GI 再次反弹进新 GI。
            auto& environment = input.scene.environment;
            environment.background = false;
            environment.background_color = Math::Vec3(0);
            environment.intensity = 0;
            if(!environment.lighting || !environment.asset) {
                environment.lighting = false;
                environment.asset = {};
                environment.lighting_intensity = 0;
                environment.rotation = 0;
            }
        }
        Binary::Writer writer;
        writer.write_u32(3); // 静态筛选、依赖与内置材质规则版本。
        writer.write_u32(static_cast<uint32_t>(purpose));
        writer.write_u64(shader_fingerprint);
        const auto vector = [&](const Math::Vec3& value) {
            for(int axis = 0; axis < 3; ++axis)
                writer.write_float(value[axis]);
        };
        std::set<AssetHandle> handles;
        writer.write_u64(input.scene.render_items.size());
        for(auto& item : input.scene.render_items) {
            item.lod.reset();
            item.animation.reset();
            writer.write_bytes(
                std::as_bytes(std::span(scene.find_entity(item.entity_id).get_uuid().bytes())));
            writer.write_u64(item.mesh_handle.value());
            writer.write_u64(item.material_handle.value());
            for(int column = 0; column < 4; ++column)
                for(int row = 0; row < 4; ++row)
                    writer.write_float(item.model_matrix[column][row]);
            handles.insert(item.mesh_handle);
            handles.insert(item.material_handle);
        }
        writer.write_u64(input.scene.lights.size());
        for(const auto& light : input.scene.lights) {
            writer.write_u32(static_cast<uint32_t>(light.type));
            vector(light.position);
            vector(light.direction);
            vector(light.color);
            for(const auto value : {light.intensity, light.range, light.inner_angle,
                    light.outer_angle, light.shadow_distance})
                writer.write_float(value);
            writer.write_u32(light.casts_shadow);
        }
        const auto& environment = input.scene.environment;
        writer.write_u64(environment.asset.value());
        writer.write_u32(environment.background);
        writer.write_u32(environment.lighting);
        writer.write_float(environment.intensity);
        writer.write_float(environment.lighting_intensity);
        writer.write_float(environment.rotation);
        vector(environment.background_color);
        if(environment.background || environment.lighting)
            handles.insert(environment.asset);
        writer.write_u64(input.scene.irradiance_volumes.size());
        for(const auto& volume : input.scene.irradiance_volumes) {
            writer.write_u64(volume.irradiance.value());
            writer.write_float(volume.intensity);
            writer.write_float(volume.normal_bias);
            handles.insert(volume.irradiance);
        }
        std::set<AssetHandle> visited;
        std::set<std::filesystem::path> sources;
        std::optional<std::map<AssetHandle, std::vector<AssetHandle>>> model_children;
        while(!handles.empty()) {
            const auto handle = *handles.begin();
            handles.erase(handles.begin());
            if(!handle || !visited.insert(handle).second)
                continue;
            const auto* record = assets.find(handle);
            if(!record) {
                const auto* builtin = find_default_asset(handle);
                if(!builtin)
                    return Described::failure("烘焙依赖未索引：" + std::to_string(handle.value()));
                if(builtin->type == AssetType::Material) {
                    const auto layout = MaterialLayout::find_builtin(builtin->material_template);
                    writer.write_u64(handle.value());
                    for(const auto& scalar : layout->get_scalars())
                        writer.write_float(scalar.default_value);
                    for(const auto& color : layout->get_vectors())
                        for(int channel = 0; channel < 4; ++channel)
                            writer.write_float(color.default_value[channel]);
                }
                continue;
            }
            handles.insert(record->dependencies.begin(), record->dependencies.end());
            if(record->source_asset)
                handles.insert(record->source_asset);
            if(record->type == AssetType::Model) {
                // 子材质的贴图关系保存在模型产物中；模型源已作为整体输入，版本也覆盖其子资源。
                if(!model_children) {
                    model_children.emplace();
                    for(const auto& child : assets.get_assets())
                        if(child.source_asset)
                            (*model_children)[child.source_asset].push_back(child.handle);
                }
                if(const auto found = model_children->find(record->handle);
                    found != model_children->end())
                    handles.insert(found->second.begin(), found->second.end());
            }
            sources.insert(record->path);
        }
        for(const auto& source : sources) {
            const auto* record = assets.find(source);
            if(!record)
                return Described::failure("烘焙源资产未索引：" + source.string());
            std::vector<std::filesystem::path> dependencies{
                assets.get_import_dependencies(record->handle).begin(),
                assets.get_import_dependencies(record->handle).end()};
            dependencies.push_back(metadata_path(source));
            for(auto& dependency : dependencies)
                dependency = assets.paths().assets() / dependency;
            ImportInputSnapshot inputs;
            if(source_cache && source_cache->contains(source))
                inputs = source_cache->at(source);
            else {
                auto captured = capture_import_inputs(
                    assets.paths().assets(), assets.paths().assets() / source, dependencies);
                if(!captured)
                    return Described::failure(captured.error());
                inputs = std::move(captured).value();
                if(source_cache)
                    source_cache->emplace(source, inputs);
            }
            writer.write_u64(inputs.files.size());
            for(const auto& file : inputs.files) {
                if(!writer.write_string(file.relative_path.generic_string(), 65536))
                    return Described::failure("烘焙依赖路径过长");
                writer.write_u64(file.size);
                writer.write_u64(file.hash);
            }
        }
        input.fingerprint = std::to_string(Binary::hash_bytes(writer.data()));
        input.dependencies.assign(visited.begin(), visited.end());
        return Described::success(std::move(input));
    }

    const std::string* StatusCache::find(const StatusKey& key) const {
        return m_status && m_status->first == key ? &m_status->second : nullptr;
    }

    SourceCache& StatusCache::sources(const uint64_t generation) {
        if(m_source_generation != generation) {
            m_sources.clear();
            m_source_generation = generation;
        }
        return m_sources;
    }

    void StatusCache::store(StatusKey key, std::string text) {
        m_status = std::pair{std::move(key), std::move(text)};
    }

    void StatusCache::clear() {
        m_status.reset();
        m_sources.clear();
    }
}
