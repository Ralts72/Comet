#include "asset/import/project_products.h"
#include "asset/import/import_service.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/artifact/environment_artifact.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/database.h"

namespace Comet {
    namespace {
        class ProductIndexReader: public AssetProductReader {
        public:
            ProductIndexReader(AssetDatabase& database, AssetImportLimits limits)
                : m_database(database), m_limits(limits) {}
            Result<std::filesystem::path, Error> location(const AssetRecord& record) override {
                auto path = m_database.paths().resolve_asset_path(record.path);
                if(!path)
                    return Result<std::filesystem::path, Error>::failure({path.error()});
                return Result<std::filesystem::path, Error>::success(std::move(path).value());
            }
            std::optional<ModelArtifact> model(const AssetRecord& record) override {
                auto path = location(record);
                return path ? ModelArtifact::load(path.value(), record.handle) : std::nullopt;
            }
            Result<MeshData, Error> default_character() override {
                return Result<MeshData, Error>::failure(
                    {"Default character product is not indexed"});
            }
            Result<TextureData, Error> texture(
                const AssetRecord& record, const TextureImportSettings*, bool) override {
                auto path = location(record);
                if(!path)
                    return Result<TextureData, Error>::failure(path.error());
                auto artifact = TextureArtifact::load(
                    path.value(), record.handle, m_limits.texture_working_bytes);
                if(!artifact)
                    return Result<TextureData, Error>::failure(
                        {"Texture CPU product is missing or invalid"});
                if(const auto* sampler = m_database.product_sampler(record.handle))
                    artifact->data.sampler = *sampler;
                return Result<TextureData, Error>::success(std::move(artifact->data));
            }
            Result<EnvironmentData, Error> environment(const AssetRecord& record, bool) override {
                auto path = location(record);
                if(!path)
                    return Result<EnvironmentData, Error>::failure(path.error());
                auto artifact = EnvironmentArtifact::load(
                    path.value(), record.handle, m_limits.async.working_bytes);
                if(!artifact)
                    return Result<EnvironmentData, Error>::failure(
                        {"Environment CPU product is missing or invalid"});
                return Result<EnvironmentData, Error>::success(std::move(artifact->data));
            }
            bool shader_current(const ShaderProgramArtifact&) const override { return true; }

        protected:
            AssetDatabase& m_database;
            AssetImportLimits m_limits;
        };
        class ProjectProductReader final: public ProductIndexReader {
        public:
            ProjectProductReader(
                AssetDatabase& database, ImportService& imports, AssetImportLimits limits)
                : ProductIndexReader(database, limits), m_imports(imports) {}
            Result<std::filesystem::path, Error> location(const AssetRecord& record) override {
                using Located = Result<std::filesystem::path, Error>;
                if(m_database.is_read_only())
                    return ProductIndexReader::location(record);
                switch(record.type) {
                    case AssetType::Model:
                        return Located::success(m_imports.model_artifact_path(record.handle));
                    case AssetType::Mesh:
                        return Located::success(m_imports.mesh_artifact_path(record.handle));
                    case AssetType::Texture:
                        return Located::success(m_imports.texture_artifact_path(record.handle));
                    case AssetType::Environment:
                        return Located::success(m_imports.environment_artifact_path(record.handle));
                    case AssetType::ShaderProgram:
                        return Located::success(
                            m_imports.shader_program_artifact_path(record.handle));
                    default:
                        return ProductIndexReader::location(record);
                }
            }
            std::optional<ModelArtifact> model(const AssetRecord& record) override {
                if(m_database.is_read_only())
                    return ProductIndexReader::model(record);
                return m_imports.find_current_model_artifact(record);
            }
            Result<MeshData, Error> default_character() override {
                if(m_database.is_read_only())
                    return ProductIndexReader::default_character();
                auto data = ImportService::prepare_default_character();
                if(!data)
                    return Result<MeshData, Error>::failure({data.error()});
                return Result<MeshData, Error>::success(std::move(data).value());
            }
            Result<TextureData, Error> texture(const AssetRecord& record,
                const TextureImportSettings* settings, bool prepare) override {
                if(m_database.is_read_only())
                    return ProductIndexReader::texture(record, settings, prepare);
                if(!settings)
                    settings = std::get_if<TextureImportSettings>(&record.import_settings);
                if(!settings)
                    return Result<TextureData, Error>::failure(
                        {"Texture asset has incompatible import settings"});
                if(prepare) {
                    auto data = m_imports.prepare_texture(
                        record, *settings, m_limits.texture_working_bytes);
                    if(!data)
                        return Result<TextureData, Error>::failure({data.error()});
                    return Result<TextureData, Error>::success(std::move(data).value());
                }
                auto artifact = m_imports.find_current_texture_artifact(
                    record, *settings, m_limits.texture_working_bytes);
                if(!artifact)
                    return Result<TextureData, Error>::failure(
                        {"Texture CPU product is missing or stale; wait for import"});
                return Result<TextureData, Error>::success(std::move(artifact->data));
            }
            Result<EnvironmentData, Error> environment(
                const AssetRecord& record, bool prepare) override {
                if(m_database.is_read_only())
                    return ProductIndexReader::environment(record, prepare);
                if(prepare) {
                    auto artifact =
                        m_imports.prepare_environment(record, m_limits.async.working_bytes);
                    if(!artifact)
                        return Result<EnvironmentData, Error>::failure({artifact.error()});
                    return Result<EnvironmentData, Error>::success(
                        std::move(artifact).value().data);
                }
                auto current = m_imports.find_current_environment_artifact(
                    record, m_limits.async.working_bytes);
                if(!current)
                    return Result<EnvironmentData, Error>::failure(
                        {"Environment CPU product is missing or stale; wait for import"});
                return Result<EnvironmentData, Error>::success(std::move(current->data));
            }
            bool shader_current(const ShaderProgramArtifact& artifact) const override {
                return m_database.is_read_only()
                       || import_inputs_are_current(m_database.paths().assets(), artifact.inputs);
            }

        private:
            ImportService& m_imports;
        };
    }
    std::unique_ptr<AssetProductReader> make_project_product_reader(
        AssetDatabase& database, ImportService& imports, AssetImportLimits limits) {
        // scan() 才确定是否为产品索引；每次读取按数据库当前模式选择来源。
        return std::make_unique<ProjectProductReader>(database, imports, limits);
    }
}
