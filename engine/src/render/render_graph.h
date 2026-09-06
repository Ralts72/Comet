#pragma once

#include "common/export.h"
#include "graphics/synchronization/resource_state.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Comet {
    class Image;
    class Buffer;
    class CommandBuffer;
    class FrameScheduler;

    // 有序 pass 的资源依赖计划；不拥有设备、队列或全局图像状态。
    class COMET_API RenderGraph {
    public:
        struct ResourceId {
            uint32_t index = std::numeric_limits<uint32_t>::max();
            bool operator==(const ResourceId&) const = default;
        };
        struct BufferState {
            ResourceState resource;
            uint64_t offset = 0;
            uint64_t size = 0;
            bool operator==(const BufferState&) const = default;
        };
        using State = std::variant<ImageState, BufferState>;
        struct Resource {
            std::string name;
            State initial;
        };
        struct Use {
            ResourceId resource;
            ResourceUsage usage;
            Flags<PipelineStage> shader_stages;
        };
        struct Pass {
            std::string name;
            std::vector<Use> uses;
        };
        struct Barrier {
            ResourceId resource;
            State before;
            State after;
        };
        struct CompiledPass {
            std::string name;
            std::vector<Barrier> barriers;
        };
        using Binding = std::variant<std::shared_ptr<Image>, std::shared_ptr<Buffer>>;
        using RecordPass = std::function<void(size_t, const CommandBuffer&)>;

        class COMET_API Plan {
        public:
            [[nodiscard]] std::span<const CompiledPass> get_passes() const {
                return m_passes;
            }
            [[nodiscard]] std::span<const State> get_final_states() const {
                return m_final_states;
            }
            [[nodiscard]] std::span<const Barrier> get_exports() const {
                return m_exports;
            }
            // 按 ResourceId.index 绑定；调用方仍负责提交及 imported 资源的外部依赖。
            void record(FrameScheduler& frames, std::span<const Binding> bindings,
                const RecordPass& record_pass) const;

        private:
            friend class RenderGraph;
            std::vector<Resource> m_resources;
            std::vector<Flags<ImageUsage>> m_image_usages;
            std::vector<CompiledPass> m_passes;
            std::vector<State> m_final_states;
            std::vector<Barrier> m_exports;
        };

        [[nodiscard]] ResourceId import_image(std::string name, ImageState initial);
        [[nodiscard]] ResourceId import_buffer(std::string name, BufferState initial);
        void add_pass(Pass pass);
        void export_resource(Use use);
        [[nodiscard]] Plan compile() const;

    private:
        [[nodiscard]] ResourceId add_resource(Resource resource);
        std::vector<Resource> m_resources;
        std::vector<Pass> m_passes;
        std::vector<Use> m_exports;
    };
}
