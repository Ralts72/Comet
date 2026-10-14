#include "assets/asset_thumbnails.h"
#include "assets/thumbnail_renderer.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "render/renderer.h"
#include "asset/artifact/texture_artifact.h"
#include "asset/import/import_service.h"
#include "asset/import/texture_importer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace CometEditor {
    namespace {
        constexpr std::size_t working_bytes = 128ull * 1024 * 1024;

        Comet::Result<Comet::TextureData> read_thumbnail(
            const Comet::ProjectPaths& paths, const Comet::AssetRecord& record) {
            using Result = Comet::Result<Comet::TextureData>;
            if(record.source_asset) {
                const auto index =
                    Comet::ImportService(paths).model_artifact_path(record.source_asset);
                const auto model = Comet::ModelArtifact::load(index, record.source_asset);
                if(!model
                    || !Comet::import_inputs_are_current(paths.assets(), model->source_inputs))
                    return Result::failure("等待源模型完成导入");
                auto texture = Comet::TextureArtifact::load(
                    model->product_path(index, record.handle), record.handle, working_bytes);
                if(!texture)
                    return Result::failure("模型贴图产品不可用");
                return AssetThumbnails::resize(texture->data);
            }
            auto source = paths.resolve_asset_path(record.path);
            if(!source)
                return Result::failure(source.error());
            auto settings = std::get<Comet::TextureImportSettings>(record.import_settings);
            settings.generate_mipmaps = false;
            auto data = Comet::TextureImporter{}.import(source.value(), settings, working_bytes);
            if(!data)
                return data;
            return AssetThumbnails::resize(data.value());
        }
    }

    AssetThumbnails::AssetThumbnails(
        Comet::AssetDatabase& database, Comet::TaskScheduler& scheduler)
        : m_database(database), m_scheduler(scheduler) {}

    AssetThumbnails::~AssetThumbnails() = default;

    void AssetThumbnails::enable_rendered_previews(
        Comet::Renderer& renderer, Comet::RenderResourceFactory& factory) {
        m_renderer = std::make_unique<ThumbnailRenderer>(renderer, factory);
        m_factory = &factory;
        m_host_renderer = &renderer;
    }

    bool AssetThumbnails::supports(Comet::AssetType type) const {
        return type == Comet::AssetType::Texture
               || (m_renderer
                   && (type == Comet::AssetType::Mesh || type == Comet::AssetType::Model
                       || type == Comet::AssetType::Material));
    }

    void AssetThumbnails::invalidate(Comet::AssetHandle handle) {
        std::erase_if(m_entries, [handle](const auto& entry) {
            return entry.second.rendered || entry.second.target.handle == handle
                   || entry.second.source.handle == handle;
        });
    }

    void AssetThumbnails::update() {
        ++m_frame;
        const auto shader_revision =
            m_host_renderer ? m_host_renderer->get_material_program_feedback()->builtin_revision
                            : 0;
        if(m_database_generation != m_database.generation()
            || m_shader_revision != shader_revision) {
            m_database_generation = m_database.generation();
            m_shader_revision = shader_revision;
            // 贴图／Shader 等依赖的发布同样改变三维预览；缓存最多 32 项。
            std::erase_if(m_entries, [](const auto& entry) { return entry.second.rendered; });
        }
        std::erase_if(m_entries, [&](const auto& entry) {
            return !m_database.is_current(entry.second.target.handle, entry.second.target.revision)
                   || (entry.second.source.handle
                       && !m_database.is_current(
                           entry.second.source.handle, entry.second.source.revision));
        });
        update_rendered();
        if(!m_pending
            || m_pending->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        auto pending = std::move(*m_pending);
        m_pending.reset();
        auto found = m_entries.find(pending.target.handle);
        if(found == m_entries.end() || found->second.target != pending.target
            || found->second.source != pending.source)
            return;
        auto& preview = found->second.preview;
        try {
            auto result = pending.result.get();
            if(result)
                preview.image =
                    std::make_shared<const Comet::TextureData>(std::move(result).value());
            else
                preview.error = result.error();
        } catch(const std::exception& error) {
            preview.error = error.what();
        }
    }

    void AssetThumbnails::update_rendered() {
        if(!m_render_pending)
            return;
        const auto pending = *m_render_pending;
        auto found = m_entries.find(pending.target.handle);
        const bool current = found != m_entries.end() && found->second.target == pending.target
                             && found->second.source == pending.source;
        if(!pending.submitted) {
            if(!current) {
                m_render_pending.reset();
                return;
            }
            // 预览引用随捕获的世界保活，不把浏览过的资源积累到游戏 Registry。
            Comet::AssetRegistry registry;
            Comet::AssetManager assets(m_database, registry, *m_factory, m_scheduler);
            auto submitted = m_renderer->submit(assets, pending.target.handle);
            if(!submitted) {
                found->second.preview.error = submitted.error().message;
                m_render_pending.reset();
            } else {
                m_render_pending->submitted = true;
            }
            return;
        }
        auto result = m_renderer->poll();
        if(result && !result.value())
            return;
        m_render_pending.reset();
        if(!current)
            return;
        if(result)
            found->second.preview.image =
                std::make_shared<const Comet::TextureData>(std::move(*result.value()));
        else
            found->second.preview.error = result.error().message;
    }

    AssetThumbnails::Preview AssetThumbnails::request(Comet::AssetHandle handle) {
        const auto* record = m_database.find(handle);
        if(!record || !supports(record->type))
            return {};
        const Comet::AssetTarget target{handle, m_database.get_revision(handle)};
        const Comet::AssetTarget source{
            record->source_asset, m_database.get_revision(record->source_asset)};
        auto found = m_entries.find(handle);
        if(found != m_entries.end()
            && (found->second.target != target || found->second.source != source)) {
            m_entries.erase(found);
            found = m_entries.end();
        }
        if(found == m_entries.end()) {
            if(m_pending || m_render_pending)
                return {};
            if(m_entries.size() == CACHE_SIZE) {
                const auto oldest = std::ranges::min_element(
                    m_entries, {}, [](const auto& item) { return item.second.used; });
                if(oldest->second.frame + 1 >= m_frame)
                    return {};
                m_entries.erase(oldest);
            }
            found = m_entries.emplace(handle, Entry{target, source, {}, 0}).first;
            found->second.rendered = record->type != Comet::AssetType::Texture;
        }
        auto& entry = found->second;
        entry.used = ++m_usage;
        entry.frame = m_frame;
        if(!entry.preview.image && entry.preview.error.empty() && !m_pending && !m_render_pending) {
            if(entry.rendered) {
                m_render_pending = RenderPending{target, source};
                return entry.preview;
            }
            auto task =
                m_scheduler.try_submit_result([paths = m_database.paths(), record = *record] {
                    return read_thumbnail(paths, record);
                });
            if(task)
                m_pending.emplace(Pending{target, source, std::move(*task)});
        }
        return entry.preview;
    }

    Comet::Result<Comet::TextureData> AssetThumbnails::resize(const Comet::TextureData& data) {
        using Result = Comet::Result<Comet::TextureData>;
        if(data.width <= 0 || data.height <= 0 || data.cubemap
            || (data.format != Comet::Format::R8G8B8A8_UNORM
                && data.format != Comet::Format::R8G8B8A8_SRGB)
            || std::uint64_t(data.width) * data.height * 4 > data.pixels.size())
            return Result::failure("缩略图需要 RGBA8 二维贴图");
        const float scale = std::min(1.0f, float(IMAGE_SIZE) / std::max(data.width, data.height));
        Comet::TextureData output{.width = std::max(1, int(std::round(data.width * scale))),
            .height = std::max(1, int(std::round(data.height * scale))),
            .format = data.format};
        output.pixels.resize(std::size_t(output.width) * output.height * 4);
        const bool srgb = data.format == Comet::Format::R8G8B8A8_SRGB;
        static const auto linear = [] {
            std::array<float, 256> values{};
            for(std::size_t i = 0; i < values.size(); ++i) {
                const float color = float(i) / 255;
                values[i] =
                    color <= 0.04045f ? color / 12.92f : std::pow((color + 0.055f) / 1.055f, 2.4f);
            }
            return values;
        }();
        for(int y = 0; y < output.height; ++y) {
            const int y0 = int(std::int64_t(y) * data.height / output.height);
            const int y1 = int(std::int64_t(y + 1) * data.height / output.height);
            for(int x = 0; x < output.width; ++x) {
                const int x0 = int(std::int64_t(x) * data.width / output.width);
                const int x1 = int(std::int64_t(x + 1) * data.width / output.width);
                std::array<double, 4> sum{};
                for(int sy = y0; sy < y1; ++sy)
                    for(int sx = x0; sx < x1; ++sx) {
                        const auto offset = (std::size_t(sy) * data.width + sx) * 4;
                        const double alpha = double(data.pixels[offset + 3]) / 255;
                        for(int c = 0; c < 3; ++c) {
                            const auto pixel = data.pixels[offset + c];
                            sum[c] += (srgb ? linear[pixel] : double(pixel) / 255) * alpha;
                        }
                        sum[3] += alpha;
                    }
                const auto offset = (std::size_t(y) * output.width + x) * 4;
                for(int c = 0; c < 3; ++c) {
                    double color = sum[3] > 0 ? sum[c] / sum[3] : 0;
                    if(srgb)
                        color = color <= 0.0031308 ? color * 12.92
                                                   : 1.055 * std::pow(color, 1.0 / 2.4) - 0.055;
                    output.pixels[offset + c] =
                        std::uint8_t(std::clamp(std::round(color * 255), 0.0, 255.0));
                }
                const double samples = double(x1 - x0) * (y1 - y0);
                output.pixels[offset + 3] =
                    std::uint8_t(std::clamp(std::round(sum[3] / samples * 255), 0.0, 255.0));
            }
        }
        return Result::success(std::move(output));
    }
}
