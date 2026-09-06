#include "render/render_graph.h"
#include "render/frame_scheduler.h"
#include "graphics/synchronization/barrier.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"

#include <algorithm>
#include <stdexcept>

namespace Comet {
    namespace {
        using State = RenderGraph::State;
        ResourceState& access(State& state) {
            return std::visit(
                [](auto& value) -> ResourceState& { return value.resource; }, state);
        }
        const ResourceState& access(const State& state) {
            return std::visit(
                [](const auto& value) -> const ResourceState& { return value.resource; },
                state);
        }
        bool writes(const ResourceState& state) {
            const auto mask = Flags<Access>(Access::ShaderWrite) | Access::TransferWrite
                              | Access::ColorAttachmentWrite
                              | Access::DepthStencilAttachmentWrite | Access::HostWrite
                              | Access::MemoryWrite;
            return static_cast<bool>(state.access & mask);
        }
        bool initialized(const State& state) {
            if(const auto* image = std::get_if<ImageState>(&state))
                return image->layout != ImageLayout::Undefined;
            return static_cast<bool>(access(state).access);
        }
        bool layout_changed(const State& before, const State& after) {
            const auto* image = std::get_if<ImageState>(&before);
            return image && image->layout != std::get<ImageState>(after).layout;
        }
        bool overlaps(uint64_t a, uint64_t count_a, uint64_t b, uint64_t count_b) {
            return a < b + count_b && b < a + count_a;
        }
        bool overlaps(const ImageSubresourceRange& a, const ImageSubresourceRange& b) {
            const auto depth_stencil =
                Flags<ImageAspect>(ImageAspect::Depth) | ImageAspect::Stencil;
            const bool shared_aspect =
                static_cast<bool>(a.aspects & b.aspects)
                || (static_cast<bool>(a.aspects & depth_stencil)
                    && static_cast<bool>(b.aspects & depth_stencil));
            return shared_aspect
                   && overlaps(
                       a.base_mip_level, a.level_count, b.base_mip_level, b.level_count)
                   && overlaps(a.base_array_layer, a.layer_count, b.base_array_layer,
                       b.layer_count);
        }
        void validate_initial(const State& state) {
            const auto& resource = access(state);
            if(resource.access && !resource.stages)
                throw std::invalid_argument(
                    "RenderGraph imported access needs a stage scope");
            if(const auto* image = std::get_if<ImageState>(&state)) {
                const auto& range = image->subresources;
                if(!range.is_valid() || range.level_count == UINT32_MAX
                    || range.layer_count == UINT32_MAX
                    || uint64_t(range.base_mip_level) + range.level_count > UINT32_MAX
                    || uint64_t(range.base_array_layer) + range.layer_count > UINT32_MAX
                    || (image->layout == ImageLayout::Undefined && resource.access)
                    || image->layout == ImageLayout::Preinitialized
                    || image->layout < ImageLayout::Undefined
                    || image->layout > ImageLayout::AttachmentOptimal)
                    throw std::invalid_argument(
                        "Invalid RenderGraph imported image state");
                const auto supported = Flags<ImageAspect>(ImageAspect::Color)
                                       | ImageAspect::Depth | ImageAspect::Stencil;
                if(static_cast<bool>(range.aspects & ~supported))
                    throw std::invalid_argument(
                        "RenderGraph only supports color/depth/stencil aspects");
                if(static_cast<bool>(range.aspects & ImageAspect::Color)
                    && range.aspects != Flags<ImageAspect>(ImageAspect::Color))
                    throw std::invalid_argument(
                        "Color and depth/stencil cannot share an image range");
            } else {
                const auto& buffer = std::get<RenderGraph::BufferState>(state);
                if(buffer.size == 0 || buffer.offset > UINT64_MAX - buffer.size)
                    throw std::invalid_argument("Invalid RenderGraph buffer range");
            }
        }
        Flags<ImageUsage> image_usage(ResourceUsage usage) {
            switch(usage) {
                case ResourceUsage::TransferSource:
                    return Flags<ImageUsage>(ImageUsage::CopySrc);
                case ResourceUsage::TransferDestination:
                    return Flags<ImageUsage>(ImageUsage::CopyDst);
                case ResourceUsage::SampledRead:
                    return Flags<ImageUsage>(ImageUsage::Sampled);
                case ResourceUsage::StorageRead:
                case ResourceUsage::StorageReadWrite:
                    return Flags<ImageUsage>(ImageUsage::StorageBinding);
                case ResourceUsage::ColorAttachmentWrite:
                case ResourceUsage::Present:
                    return Flags<ImageUsage>(ImageUsage::ColorAttachment);
                case ResourceUsage::DepthStencilAttachmentWrite:
                case ResourceUsage::DepthStencilAttachmentRead:
                    return Flags<ImageUsage>(ImageUsage::DepthStencilAttachment);
                default:
                    return {};
            }
        }
        State resolve(const State& previous, const RenderGraph::Use& use) {
            if(use.usage == ResourceUsage::Undefined)
                throw std::invalid_argument(
                    "RenderGraph pass cannot use undefined contents");
            const auto queue = access(previous).queue_family;
            if(const auto* image = std::get_if<ImageState>(&previous)) {
                auto next = resolve_image_state(
                    use.usage, image->subresources, use.shader_stages, queue);
                if(!next)
                    throw std::invalid_argument("Invalid RenderGraph image usage");
                return *next;
            }
            if(use.usage == ResourceUsage::SampledRead
                || use.usage == ResourceUsage::Present
                || use.usage == ResourceUsage::ColorAttachmentWrite
                || use.usage == ResourceUsage::DepthStencilAttachmentWrite
                || use.usage == ResourceUsage::DepthStencilAttachmentRead)
                throw std::invalid_argument(
                    "Image-only usage applied to RenderGraph buffer");
            auto next = resolve_resource_state(use.usage, use.shader_stages, queue);
            if(!next)
                throw std::invalid_argument("Invalid RenderGraph buffer usage");
            auto result = std::get<RenderGraph::BufferState>(previous);
            result.resource = *next;
            return result;
        }
    }

    RenderGraph::ResourceId RenderGraph::add_resource(Resource resource) {
        if(m_resources.size() >= 256 || resource.name.empty())
            throw std::invalid_argument(
                "RenderGraph needs named resources and at most 256 resources");
        if(std::ranges::any_of(
               m_resources, [&](const auto& item) { return item.name == resource.name; }))
            throw std::invalid_argument("Duplicate RenderGraph resource name");
        validate_initial(resource.initial);
        const ResourceId id{static_cast<uint32_t>(m_resources.size())};
        m_resources.push_back(std::move(resource));
        return id;
    }
    RenderGraph::ResourceId RenderGraph::import_image(
        std::string name, ImageState initial) {
        return add_resource({std::move(name), initial});
    }
    RenderGraph::ResourceId RenderGraph::import_buffer(
        std::string name, BufferState initial) {
        return add_resource({std::move(name), initial});
    }
    void RenderGraph::add_pass(Pass pass) {
        if(m_passes.size() >= 512 || pass.uses.size() > 256 || pass.name.empty())
            throw std::invalid_argument("Invalid RenderGraph pass name or count limit");
        if(std::ranges::any_of(
               m_passes, [&](const auto& item) { return item.name == pass.name; }))
            throw std::invalid_argument("Duplicate RenderGraph pass name");
        m_passes.push_back(std::move(pass));
    }
    void RenderGraph::export_resource(Use use) {
        if(m_exports.size() >= 256)
            throw std::invalid_argument("RenderGraph export count limit exceeded");
        m_exports.push_back(use);
    }

    RenderGraph::Plan RenderGraph::compile() const {
        struct Tracked {
            State state;
            bool has_writer;
            bool has_contents;
            std::vector<ResourceState> visible;
        };
        Plan result;
        result.m_resources = m_resources;
        result.m_image_usages.resize(m_resources.size());
        std::vector<Tracked> tracked;
        std::optional<uint32_t> queue;
        for(const auto& resource : m_resources) {
            const auto owner = access(resource.initial).queue_family;
            if(queue && *queue != owner)
                throw std::invalid_argument(
                    "RenderGraph requires one queue owner; import explicit handoffs first");
            queue = owner;
            tracked.push_back({resource.initial, writes(access(resource.initial)),
                initialized(resource.initial), {}});
        }
        auto compile_uses = [&](std::span<const Use> uses, std::vector<Barrier>& barriers,
                                bool exporting) {
            std::vector<bool> used(m_resources.size());
            for(const auto& use : uses) {
                if(!exporting
                    && (use.usage == ResourceUsage::HostRead
                        || use.usage == ResourceUsage::HostWrite))
                    throw std::invalid_argument(
                        "Host access is an external handoff, not a GPU pass");
                if(use.resource.index >= tracked.size() || used[use.resource.index])
                    throw std::invalid_argument(
                        "Invalid or duplicate resource use within RenderGraph pass");
                used[use.resource.index] = true;
                auto& previous = tracked[use.resource.index];
                State after = resolve(previous.state, use);
                if(std::holds_alternative<ImageState>(after)) {
                    auto& required = result.m_image_usages[use.resource.index];
                    required = required | image_usage(use.usage);
                }
                const auto desired = access(after);
                const bool write = writes(desired);
                if(!previous.has_contents
                    && (exporting || !write
                        || use.usage == ResourceUsage::StorageReadWrite))
                    throw std::invalid_argument("RenderGraph resource '"
                                                + m_resources[use.resource.index].name
                                                + "' has no initialized producer");
                const bool changed = layout_changed(previous.state, after);
                const bool visible =
                    std::ranges::any_of(previous.visible, [&](const auto& scope) {
                        return (scope.stages & desired.stages) == desired.stages
                               && (scope.access & desired.access) == desired.access;
                    });
                if(changed || (write && previous.has_contents)
                    || (!write && previous.has_writer && !visible))
                    barriers.push_back({use.resource, previous.state, after});
                if(write && !exporting) {
                    previous = {after, true, true, {}};
                } else {
                    // 保留全部 reader 的执行范围，后续写入必须等待它们全部完成。
                    access(after).stages =
                        access(after).stages | access(previous.state).stages;
                    access(after).access =
                        access(after).access | access(previous.state).access;
                    previous.state = after;
                    previous.has_writer |= changed;
                    if(changed)
                        previous.visible = {};
                    previous.visible.push_back(desired);
                }
            }
        };
        for(const auto& pass : m_passes) {
            CompiledPass compiled{pass.name, {}};
            compile_uses(pass.uses, compiled.barriers, false);
            result.m_passes.push_back(std::move(compiled));
        }
        compile_uses(m_exports, result.m_exports, true);
        for(auto& state : tracked)
            result.m_final_states.push_back(std::move(state.state));
        return result;
    }

    void RenderGraph::Plan::record(FrameScheduler& frames,
        std::span<const Binding> bindings, const RecordPass& record_pass) const {
        if(!frames.is_recording_frame() || bindings.size() != m_resources.size()
            || !record_pass)
            throw std::invalid_argument(
                "RenderGraph requires active frame, complete bindings and recorder");
        for(size_t index = 0; index < bindings.size(); ++index) {
            const auto owner = access(m_resources[index].initial).queue_family;
            if(owner != UNSPECIFIED_QUEUE_FAMILY
                && owner != frames.get_queue_family_index())
                throw std::invalid_argument(
                    "RenderGraph resource belongs to a different recording queue family");
            if(bindings[index].index() != m_resources[index].initial.index())
                throw std::invalid_argument("RenderGraph binding kind mismatch");
            std::visit(
                [](const auto& owner) {
                    if(!owner || !owner->get())
                        throw std::invalid_argument("RenderGraph binding is null");
                },
                bindings[index]);
            if(const auto* image = std::get_if<ImageState>(&m_resources[index].initial)) {
                const auto info =
                    std::get<std::shared_ptr<Image>>(bindings[index])->get_info();
                const auto& range = image->subresources;
                if((info.usage & m_image_usages[index]) != m_image_usages[index])
                    throw std::invalid_argument(
                        "RenderGraph image binding lacks declared usage");
                if(uint64_t(range.base_mip_level) + range.level_count > info.mip_levels
                    || uint64_t(range.base_array_layer) + range.layer_count
                           > info.array_layers)
                    throw std::invalid_argument(
                        "RenderGraph image range exceeds bound image");
                const bool depth = Graphics::is_depth_stencil_format(info.format);
                if(depth && !Graphics::is_depth_only_format(info.format)
                    && range.aspects
                           != (Flags<ImageAspect>(ImageAspect::Depth)
                               | ImageAspect::Stencil))
                    throw std::invalid_argument(
                        "RenderGraph requires coupled depth/stencil without separate-layout feature");
                if(depth == static_cast<bool>(range.aspects & ImageAspect::Color)
                    || (!depth && range.aspects != Flags<ImageAspect>(ImageAspect::Color))
                    || (Graphics::is_depth_only_format(info.format)
                        && static_cast<bool>(range.aspects & ImageAspect::Stencil)))
                    throw std::invalid_argument(
                        "RenderGraph image aspects do not match format");
            } else {
                const auto& range = std::get<BufferState>(m_resources[index].initial);
                const auto size =
                    std::get<std::shared_ptr<Buffer>>(bindings[index])->get_size();
                if(range.offset > size || range.size > size - range.offset)
                    throw std::invalid_argument(
                        "RenderGraph buffer range exceeds bound buffer");
            }
            for(size_t other = 0; other < index; ++other) {
                if(bindings[index].index() != bindings[other].index())
                    continue;
                bool alias = false;
                if(const auto* image =
                        std::get_if<std::shared_ptr<Image>>(&bindings[index])) {
                    alias =
                        (*image)->get()
                            == std::get<std::shared_ptr<Image>>(bindings[other])->get()
                        && overlaps(
                            std::get<ImageState>(m_resources[index].initial).subresources,
                            std::get<ImageState>(m_resources[other].initial)
                                .subresources);
                } else {
                    const auto& a = std::get<BufferState>(m_resources[index].initial);
                    const auto& b = std::get<BufferState>(m_resources[other].initial);
                    alias =
                        std::get<std::shared_ptr<Buffer>>(bindings[index])->get()
                            == std::get<std::shared_ptr<Buffer>>(bindings[other])->get()
                        && overlaps(a.offset, a.size, b.offset, b.size);
                }
                if(alias)
                    throw std::invalid_argument(
                        "RenderGraph overlapping aliases need one resource declaration");
            }
        }
        struct NativeBarriers {
            std::vector<vk::ImageMemoryBarrier2> images;
            std::vector<vk::BufferMemoryBarrier2> buffers;
        };
        auto prepare = [&](std::span<const Barrier> barriers) {
            NativeBarriers result;
            for(const auto& barrier : barriers) {
                const auto index = barrier.resource.index;
                if(const auto* image = std::get_if<ImageState>(&barrier.before)) {
                    auto native = Graphics::build_image_memory_barrier(
                        std::get<std::shared_ptr<Image>>(bindings[index])->get(), *image,
                        std::get<ImageState>(barrier.after));
                    if(!native)
                        throw std::invalid_argument("Invalid RenderGraph image barrier");
                    result.images.push_back(*native);
                } else {
                    const auto& before = std::get<BufferState>(barrier.before);
                    auto native = Graphics::build_buffer_memory_barrier(
                        std::get<std::shared_ptr<Buffer>>(bindings[index])->get(),
                        before.resource, access(barrier.after), before.offset,
                        before.size);
                    if(!native)
                        throw std::invalid_argument("Invalid RenderGraph buffer barrier");
                    result.buffers.push_back(*native);
                }
            }
            return result;
        };
        // 所有绑定和 barrier 先验证，再对当前命令缓冲产生任何副作用。
        std::vector<NativeBarriers> native;
        for(const auto& pass : m_passes)
            native.push_back(prepare(pass.barriers));
        native.push_back(prepare(m_exports));
        for(const auto& binding : bindings)
            std::visit(
                [&](const auto& owner) { frames.retain_current_frame_resource(owner); },
                binding);
        const auto& commands = frames.get_current_command_buffer();
        auto record = [&](const NativeBarriers& barriers) {
            if(barriers.images.empty() && barriers.buffers.empty())
                return;
            vk::DependencyInfo dependency;
            dependency.setImageMemoryBarriers(barriers.images);
            dependency.setBufferMemoryBarriers(barriers.buffers);
            commands.get().pipelineBarrier2(dependency);
        };
        for(size_t index = 0; index < m_passes.size(); ++index) {
            record(native[index]);
            record_pass(index, commands);
        }
        record(native.back());
    }
}
