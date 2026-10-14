#pragma once

#include "asset/database.h"
#include "asset/data/texture_data.h"
#include "core/task_scheduler.h"

#include <future>
#include <map>
#include <memory>

namespace Comet {
    class Renderer;
    class RenderResourceFactory;
}

namespace CometEditor {
    class ThumbnailRenderer;
    // 仅保存编辑器预览像素；后台任务持有路径和记录快照，不访问资产数据库或 GPU。
    class AssetThumbnails {
    public:
        static constexpr int IMAGE_SIZE = 128;
        static constexpr std::size_t CACHE_SIZE = 32;
        struct Preview {
            std::shared_ptr<const Comet::TextureData> image;
            std::string error;
        };
        AssetThumbnails(Comet::AssetDatabase& database, Comet::TaskScheduler& scheduler);
        ~AssetThumbnails();
        void enable_rendered_previews(
            Comet::Renderer& renderer, Comet::RenderResourceFactory& factory);
        [[nodiscard]] bool supports(Comet::AssetType type) const;
        void update();
        void invalidate(Comet::AssetHandle handle);
        [[nodiscard]] Preview request(Comet::AssetHandle handle);
        [[nodiscard]] static Comet::Result<Comet::TextureData> resize(
            const Comet::TextureData& data);

    private:
        struct Entry {
            Comet::AssetTarget target;
            Comet::AssetTarget source;
            Preview preview;
            std::uint64_t used = 0;
            std::uint64_t frame = 0;
            bool rendered = false;
        };
        struct Pending {
            Comet::AssetTarget target;
            Comet::AssetTarget source;
            std::future<Comet::Result<Comet::TextureData>> result;
        };
        Comet::AssetDatabase& m_database;
        Comet::TaskScheduler& m_scheduler;
        std::map<Comet::AssetHandle, Entry> m_entries;
        std::optional<Pending> m_pending;
        struct RenderPending {
            Comet::AssetTarget target;
            Comet::AssetTarget source;
            bool submitted = false;
        };
        void update_rendered();
        std::unique_ptr<ThumbnailRenderer> m_renderer;
        Comet::RenderResourceFactory* m_factory = nullptr;
        Comet::Renderer* m_host_renderer = nullptr;
        std::optional<RenderPending> m_render_pending;
        std::uint64_t m_database_generation = 0;
        std::uint64_t m_shader_revision = 0;
        std::uint64_t m_usage = 0;
        std::uint64_t m_frame = 0;
    };
}
