#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/synchronization/resource_state.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Comet {
    class Image;
    class Buffer;
    class CommandBuffer;
    class FrameScheduler;
    class Device;

    // 单队列有序 pass 的同步与瞬态资源计划；不持有全局图像状态。
    class COMET_API RenderGraph {
    public:
        using PassId = std::size_t;
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
        // 首版逻辑图像为单 mip、单层 2D；持久图像和子资源继续显式 import。
        struct ImageDescription {
            Format format = Format::R16G16B16A16_SFLOAT;
            uint32_t width = 0;
            uint32_t height = 0;
            SampleCount samples = SampleCount::Count1;
            bool operator==(const ImageDescription&) const = default;
        };
        struct BufferDescription {
            uint64_t size = 0;
            bool operator==(const BufferDescription&) const = default;
        };
        using Description = std::variant<ImageDescription, BufferDescription>;
        struct Resource {
            std::string name;
            State initial;
            std::optional<Description> transient;
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
        // 回调同步执行，不保存；命令缓冲来自当前 FrameScheduler。
        using RecordPass = std::function<Result<void, GraphicsError>(PassId, CommandBuffer&)>;

        struct Lifetime {
            PassId first = std::numeric_limits<PassId>::max();
            PassId last = 0;
            uint32_t allocation = std::numeric_limits<uint32_t>::max();
        };
        struct Allocation {
            Description description;
            Flags<ImageUsage> image_usage;
            Flags<BufferUsage> buffer_usage;
        };
        class Plan;
        // 每个 GPU slot 拥有独立物理资源；缓存到图变化，不逐帧重新分配。
        class COMET_API Resources {
        public:
            [[nodiscard]] std::span<const Binding> get_bindings(uint32_t slot) const;
            void bind_resources(std::span<Binding> bindings, uint32_t slot) const;

        private:
            friend class Plan;
            std::vector<ResourceId> m_ids;
            std::vector<std::vector<Binding>> m_bindings;
        };

        class COMET_API Plan {
        public:
            [[nodiscard]] std::size_t resource_count() const { return m_resources.size(); }
            [[nodiscard]] std::span<const CompiledPass> get_passes() const { return m_passes; }
            [[nodiscard]] std::span<const State> get_final_states() const { return m_final_states; }
            [[nodiscard]] std::span<const Barrier> get_exports() const { return m_exports; }
            [[nodiscard]] std::span<const Lifetime> get_lifetimes() const { return m_lifetimes; }
            [[nodiscard]] std::span<const Allocation> get_allocations() const {
                return m_allocations;
            }
            [[nodiscard]] Result<Resources, GraphicsError> allocate(
                Device& device, uint32_t frame_slots) const;
            // 按 ResourceId.index 绑定；调用方仍负责提交及 imported 资源的外部依赖。
            // 回调失败后可能已有部分录制；调用方必须中止该帧，不能继续提交。
            [[nodiscard]] Result<void, GraphicsError> record(FrameScheduler& frames,
                std::span<const Binding> bindings, const RecordPass& record_pass) const;

        private:
            friend class RenderGraph;
            std::vector<Resource> m_resources;
            std::vector<Flags<ImageUsage>> m_image_usages;
            std::vector<CompiledPass> m_passes;
            std::vector<State> m_final_states;
            std::vector<Barrier> m_exports;
            std::vector<Lifetime> m_lifetimes;
            std::vector<Allocation> m_allocations;
        };

        // 构建阶段仅收集声明，compile 统一验证；ResourceId 只在当前图内有效。
        [[nodiscard]] ResourceId import_image(std::string name, ImageState initial);
        [[nodiscard]] ResourceId import_buffer(std::string name, BufferState initial);
        [[nodiscard]] ResourceId create_image(std::string name, ImageDescription description);
        [[nodiscard]] ResourceId create_buffer(std::string name, BufferDescription description);
        PassId add_pass(Pass pass);
        void export_resource(Use use);
        [[nodiscard]] Result<Plan> compile(bool reuse_transients = true) const;

    private:
        [[nodiscard]] ResourceId add_resource(Resource resource);
        std::vector<Resource> m_resources;
        std::vector<Pass> m_passes;
        std::vector<Use> m_exports;
    };
}
