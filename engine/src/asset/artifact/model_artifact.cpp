#include "asset/artifact/model_artifact.h"
#include "asset/artifact/mesh_artifact.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/import/texture_importer.h"
#include "asset/serialization/material_serializer.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/binary.h"
#include "common/file_io.h"

#include <array>
#include <cmath>
#include <unordered_set>

namespace Comet {
    namespace {
        constexpr std::string_view MAGIC = "COMETMOD";
        constexpr uint32_t FORMAT_VERSION = 2;
        constexpr size_t MAX_INDEX_BYTES = 32 * 1024 * 1024;
        constexpr size_t MAX_STRING = 64 * 1024;

        bool valid_sampler(const SamplerDesc& sampler) {
            const auto filter = [](Filter value) {
                return value == Filter::Nearest || value == Filter::Linear;
            };
            const auto wrap = [](SamplerAddressMode value) {
                return value == SamplerAddressMode::Repeat
                       || value == SamplerAddressMode::MirrorRepeat
                       || value == SamplerAddressMode::ClampToEdge;
            };
            return filter(sampler.min_filter) && filter(sampler.mag_filter)
                   && (sampler.mipmap_mode == SamplerMipmapMode::Nearest
                       || sampler.mipmap_mode == SamplerMipmapMode::Linear)
                   && wrap(sampler.address_mode_u) && wrap(sampler.address_mode_v)
                   && wrap(sampler.address_mode_w) && std::isfinite(sampler.max_anisotropy)
                   && sampler.max_anisotropy >= 1.f;
        }

        bool valid_model(const ModelArtifact& artifact) {
            if(artifact.metadata.type != AssetType::Model || !artifact.source_inputs.valid()
                || artifact.model.default_scene >= artifact.model.scenes.size())
                return false;
            std::map<AssetHandle, AssetType> types;
            for(const auto& child : artifact.metadata.subresources)
                types.emplace(child.handle, child.type);
            const auto matches = [&](AssetHandle handle, AssetType type) {
                const auto found = types.find(handle);
                return found != types.end() && found->second == type;
            };
            for(const auto& [handle, sampler] : artifact.texture_samplers)
                if(!matches(handle, AssetType::Texture) || !valid_sampler(sampler))
                    return false;
            for(const auto& [handle, type] : types)
                if(type == AssetType::Texture && !artifact.texture_samplers.contains(handle))
                    return false;
            const auto& nodes = artifact.model.nodes;
            std::unordered_set<std::string> keys;
            for(const auto& node : nodes) {
                const auto& transform = node.transform;
                const auto& rotation = transform.rotation;
                if(node.key.empty() || !keys.insert(node.key).second
                    || (node.parent != ModelData::NO_PARENT && node.parent >= nodes.size())
                    || !Math::is_finite(transform.translation) || !Math::is_finite(transform.scale)
                    || !Math::is_finite(Math::Vec4(rotation.x, rotation.y, rotation.z, rotation.w))
                    || std::abs(glm::length(rotation) - 1.f) > 1e-3f)
                    return false;
                for(const auto& part : node.parts) {
                    const auto& lod = part.lod;
                    if(!matches(part.mesh, AssetType::Mesh)
                        || !matches(part.material, AssetType::Material)
                        || (lod.level1 && !matches(lod.level1, AssetType::Mesh))
                        || (lod.level2 && (!lod.level1 || !matches(lod.level2, AssetType::Mesh)))
                        || !std::isfinite(lod.level1_height) || !std::isfinite(lod.level2_height)
                        || !std::isfinite(lod.hysteresis) || lod.level1_height <= 0
                        || lod.level1_height > 1 || lod.level2_height <= 0
                        || lod.level2_height >= lod.level1_height || lod.hysteresis < 0
                        || lod.hysteresis > .5f || lod.preview != MeshLod::Preview::Auto)
                        return false;
                }
            }
            std::vector<uint8_t> visited(nodes.size());
            for(uint32_t start = 0; start < nodes.size(); ++start) {
                uint32_t index = start;
                while(index != ModelData::NO_PARENT && visited[index] == 0) {
                    visited[index] = 1;
                    index = nodes[index].parent;
                }
                if(index != ModelData::NO_PARENT && visited[index] == 1)
                    return false;
                index = start;
                while(index != ModelData::NO_PARENT && visited[index] == 1) {
                    visited[index] = 2;
                    index = nodes[index].parent;
                }
            }
            for(const auto& scene : artifact.model.scenes) {
                std::unordered_set<uint32_t> roots;
                for(const auto root : scene.roots)
                    if(root >= nodes.size() || nodes[root].parent != ModelData::NO_PARENT
                        || !roots.insert(root).second)
                        return false;
            }
            return true;
        }

        Result<std::vector<std::byte>> encode(const ModelArtifact& artifact) {
            using Encoded = Result<std::vector<std::byte>>;
            auto metadata = MetadataSerializer{}.serialize(artifact.metadata);
            if(!metadata)
                return Encoded::failure(metadata.error());
            if(!valid_model(artifact))
                return Encoded::failure("Invalid model artifact");
            Binary::Writer writer;
            writer.write_u32(artifact.importer_version);
            writer.write_u32(MeshImporter::VERSION);
            writer.write_u32(TextureImporter::VERSION);
            if(!writer.write_string(metadata.value(), MAX_INDEX_BYTES))
                return Encoded::failure("Model metadata is too large");
            writer.write_u32(uint32_t(artifact.source_inputs.files.size()));
            for(const auto& input : artifact.source_inputs.files) {
                const auto path = input.relative_path.generic_u8string();
                if(!writer.write_string(
                       std::string_view(reinterpret_cast<const char*>(path.data()), path.size()),
                       MAX_STRING))
                    return Encoded::failure("Model source path is too long");
                writer.write_u64(input.size);
                writer.write_u64(input.hash);
            }
            writer.write_u32(artifact.model.default_scene);
            writer.write_u32(uint32_t(artifact.model.nodes.size()));
            for(const auto& node : artifact.model.nodes) {
                if(!writer.write_string(node.key, MAX_STRING)
                    || !writer.write_string(node.name, MAX_STRING))
                    return Encoded::failure("Model node name is too long");
                writer.write_u32(node.parent);
                const auto& t = node.transform;
                for(float value : {t.translation.x, t.translation.y, t.translation.z, t.rotation.x,
                        t.rotation.y, t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z})
                    writer.write_float(value);
                writer.write_u32(uint32_t(node.parts.size()));
                for(const auto& part : node.parts) {
                    writer.write_u64(part.mesh.value());
                    writer.write_u64(part.material.value());
                    writer.write_u64(part.lod.level1.value());
                    writer.write_u64(part.lod.level2.value());
                    writer.write_float(part.lod.level1_height);
                    writer.write_float(part.lod.level2_height);
                    writer.write_float(part.lod.hysteresis);
                    writer.write_u32(part.collision ? 1 : 0);
                }
            }
            writer.write_u32(uint32_t(artifact.model.scenes.size()));
            for(const auto& scene : artifact.model.scenes) {
                if(!writer.write_string(scene.name, MAX_STRING))
                    return Encoded::failure("Model scene name is too long");
                writer.write_u32(uint32_t(scene.roots.size()));
                for(const auto root : scene.roots)
                    writer.write_u32(root);
            }
            writer.write_u32(uint32_t(artifact.texture_samplers.size()));
            for(const auto& [handle, sampler] : artifact.texture_samplers) {
                writer.write_u64(handle.value());
                for(auto value : {uint32_t(sampler.mag_filter), uint32_t(sampler.min_filter),
                        uint32_t(sampler.mipmap_mode), uint32_t(sampler.address_mode_u),
                        uint32_t(sampler.address_mode_v), uint32_t(sampler.address_mode_w)})
                    writer.write_u32(value);
                writer.write_float(sampler.max_anisotropy);
            }
            if(writer.data().size() > MAX_INDEX_BYTES)
                return Encoded::failure("Model index is too large");
            return Encoded::success(writer.data());
        }

        bool read_count(Binary::Reader& reader, uint32_t& count, size_t minimum_bytes) {
            return reader.read_u32(count) && count <= reader.remaining() / minimum_bytes;
        }

        std::optional<ModelArtifact> decode(std::span<const std::byte> payload) {
            Binary::Reader reader(payload);
            ModelArtifact artifact;
            std::string metadata;
            uint32_t mesh_version, texture_version;
            if(!reader.read_u32(artifact.importer_version) || !reader.read_u32(mesh_version)
                || mesh_version != MeshImporter::VERSION || !reader.read_u32(texture_version)
                || texture_version != TextureImporter::VERSION
                || !reader.read_string(metadata, MAX_INDEX_BYTES))
                return std::nullopt;
            auto parsed = MetadataSerializer{}.deserialize(metadata);
            if(!parsed)
                return std::nullopt;
            artifact.metadata = std::move(parsed).value();
            uint32_t count;
            if(!read_count(reader, count, 20))
                return std::nullopt;
            artifact.source_inputs.files.resize(count);
            for(auto& input : artifact.source_inputs.files) {
                std::string path;
                if(!reader.read_string(path, MAX_STRING) || !reader.read_u64(input.size)
                    || !reader.read_u64(input.hash))
                    return std::nullopt;
                input.relative_path =
                    std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size());
            }
            if(!reader.read_u32(artifact.model.default_scene) || !read_count(reader, count, 56))
                return std::nullopt;
            artifact.model.nodes.resize(count);
            for(auto& node : artifact.model.nodes) {
                if(!reader.read_string(node.key, MAX_STRING)
                    || !reader.read_string(node.name, MAX_STRING) || !reader.read_u32(node.parent))
                    return std::nullopt;
                auto& t = node.transform;
                for(float* value : {&t.translation.x, &t.translation.y, &t.translation.z,
                        &t.rotation.x, &t.rotation.y, &t.rotation.z, &t.rotation.w, &t.scale.x,
                        &t.scale.y, &t.scale.z})
                    if(!reader.read_float(*value))
                        return std::nullopt;
                if(!read_count(reader, count, 48))
                    return std::nullopt;
                node.parts.resize(count);
                for(auto& part : node.parts) {
                    uint64_t mesh, material;
                    if(!reader.read_u64(mesh) || !reader.read_u64(material))
                        return std::nullopt;
                    part = {AssetHandle{mesh}, AssetHandle{material}};
                    uint64_t first, second;
                    uint32_t collision;
                    if(!reader.read_u64(first) || !reader.read_u64(second)
                        || !reader.read_float(part.lod.level1_height)
                        || !reader.read_float(part.lod.level2_height)
                        || !reader.read_float(part.lod.hysteresis) || !reader.read_u32(collision)
                        || collision > 1)
                        return std::nullopt;
                    part.lod.level1 = AssetHandle{first};
                    part.lod.level2 = AssetHandle{second};
                    part.collision = collision != 0;
                }
            }
            if(!read_count(reader, count, 8))
                return std::nullopt;
            artifact.model.scenes.resize(count);
            for(auto& scene : artifact.model.scenes) {
                if(!reader.read_string(scene.name, MAX_STRING) || !read_count(reader, count, 4))
                    return std::nullopt;
                scene.roots.resize(count);
                for(auto& root : scene.roots)
                    if(!reader.read_u32(root))
                        return std::nullopt;
            }
            if(!read_count(reader, count, 36))
                return std::nullopt;
            for(uint32_t index = 0; index < count; ++index) {
                uint64_t handle;
                std::array<uint32_t, 6> values{};
                SamplerDesc sampler;
                if(!reader.read_u64(handle))
                    return std::nullopt;
                for(auto& value : values)
                    if(!reader.read_u32(value))
                        return std::nullopt;
                if(!reader.read_float(sampler.max_anisotropy))
                    return std::nullopt;
                sampler.mag_filter = static_cast<Filter>(values[0]);
                sampler.min_filter = static_cast<Filter>(values[1]);
                sampler.mipmap_mode = static_cast<SamplerMipmapMode>(values[2]);
                sampler.address_mode_u = static_cast<SamplerAddressMode>(values[3]);
                sampler.address_mode_v = static_cast<SamplerAddressMode>(values[4]);
                sampler.address_mode_w = static_cast<SamplerAddressMode>(values[5]);
                const std::array restored{uint32_t(sampler.mag_filter),
                    uint32_t(sampler.min_filter), uint32_t(sampler.mipmap_mode),
                    uint32_t(sampler.address_mode_u), uint32_t(sampler.address_mode_v),
                    uint32_t(sampler.address_mode_w)};
                if(restored != values)
                    return std::nullopt;
                if(!artifact.texture_samplers.emplace(AssetHandle{handle}, sampler).second)
                    return std::nullopt;
            }
            if(reader.remaining() != 0 || !valid_model(artifact))
                return std::nullopt;
            return artifact;
        }
    }

    std::filesystem::path ModelArtifact::product_path(
        const std::filesystem::path& index_path, AssetHandle handle) const {
        return index_path.parent_path() / std::to_string(generation)
               / std::to_string(handle.value());
    }

    Result<ModelArtifact> ModelArtifact::stage(const std::filesystem::path& index_path,
        ModelImportProduct product, ImportInputSnapshot inputs, const AssetImportLimits& limits) {
        using Staged = Result<ModelArtifact>;
        ModelArtifact artifact{.metadata = std::move(product.metadata),
            .model = std::move(product.model),
            .source_inputs = std::move(inputs)};
        for(const auto& texture : product.textures)
            artifact.texture_samplers.emplace(texture.handle, texture.sampler);
        auto payload = encode(artifact);
        if(!payload)
            return Staged::failure(payload.error());
        artifact.generation = Binary::hash_bytes(payload.value());
        for(auto& mesh : product.meshes) {
            MeshArtifact data{.handle = mesh.handle,
                .importer_version = MeshImporter::VERSION,
                .source_inputs = artifact.source_inputs,
                .data = std::move(mesh.data)};
            if(auto saved = data.publish_atomic(artifact.product_path(index_path, mesh.handle));
                !saved)
                return Staged::failure(saved.error());
        }
        product.meshes.clear();
        for(const auto& material : product.materials)
            if(auto saved = MaterialSerializer{}.save(
                   material.data, artifact.product_path(index_path, material.handle));
                !saved)
                return Staged::failure(saved.error());
        product.materials.clear();
        // 几何已落盘；保留模型索引／编码图片的空间后，逐张借用剩余解码预算。
        if(payload.value().size() > limits.mesh_working_bytes / 4)
            return Staged::failure("Model index exceeds its working budget");
        auto texture_budget = limits.mesh_working_bytes - payload.value().size() * 4;
        for(const auto& image : product.images) {
            if(image.encoded.size() > texture_budget)
                return Staged::failure("Model images exceed their working budget");
            texture_budget -= image.encoded.size();
        }
        texture_budget = std::min(texture_budget, limits.texture_working_bytes);
        for(const auto& texture : product.textures) {
            if(texture.image >= product.images.size())
                return Staged::failure("Invalid model texture image");
            const auto encoded = std::span<const std::byte>(product.images[texture.image].encoded);
            auto pixels =
                TextureImporter{}.import(encoded, texture.settings, texture_budget, limits);
            if(!pixels)
                return Staged::failure(pixels.error());
            TextureArtifact data{.handle = texture.handle,
                .importer_version = TextureImporter::VERSION,
                .source = artifact.source_inputs.files.front(),
                .settings = texture.settings,
                .data = std::move(pixels).value()};
            if(auto saved = data.publish_atomic(artifact.product_path(index_path, texture.handle));
                !saved)
                return Staged::failure(saved.error());
        }
        return Staged::success(std::move(artifact));
    }

    Result<void> ModelArtifact::publish_atomic(const std::filesystem::path& index_path) const {
        auto payload = encode(*this);
        if(!payload)
            return Result<void>::failure(payload.error());
        if(generation != Binary::hash_bytes(payload.value()))
            return Result<void>::failure("Model generation does not match its products");
        for(const auto& child : metadata.subresources) {
            std::error_code error;
            if(!std::filesystem::is_regular_file(product_path(index_path, child.handle), error)
                || error)
                return Result<void>::failure("Model product is missing");
        }
        Binary::Writer header;
        header.write_bytes(std::as_bytes(std::span(MAGIC)));
        header.write_u32(FORMAT_VERSION);
        header.write_u64(generation);
        const std::array chunks{
            std::span<const std::byte>(header.data()), std::span<const std::byte>(payload.value())};
        return write_binary_file_atomic(index_path, chunks);
    }

    std::optional<ModelArtifact> ModelArtifact::load(
        const std::filesystem::path& index_path, AssetHandle expected_handle) {
        const auto bytes = read_binary_file(index_path, MAX_INDEX_BYTES + 20);
        if(!bytes)
            return std::nullopt;
        Binary::Reader reader(bytes.value());
        uint32_t version;
        uint64_t hash;
        if(!reader.match_bytes(std::as_bytes(std::span(MAGIC))) || !reader.read_u32(version)
            || version != FORMAT_VERSION || !reader.read_u64(hash))
            return std::nullopt;
        const auto payload = std::span<const std::byte>(bytes.value()).last(reader.remaining());
        if(Binary::hash_bytes(payload) != hash)
            return std::nullopt;
        auto artifact = decode(payload);
        if(!artifact || artifact->metadata.handle != expected_handle)
            return std::nullopt;
        artifact->generation = hash;
        for(const auto& child : artifact->metadata.subresources) {
            std::error_code error;
            if(!std::filesystem::is_regular_file(
                   artifact->product_path(index_path, child.handle), error)
                || error)
                return std::nullopt;
        }
        return artifact;
    }
}
