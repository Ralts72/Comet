#include "render/render_graph.h"
#include "render/frame_scheduler.h"
#include "graphics/synchronization/barrier.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "graphics/device.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <unordered_set>

namespace Comet {
    namespace {
        using State = RenderGraph::State;
        ResourceState& access(State& state) {
            return std::visit([](auto& value) -> ResourceState& { return value.resource; }, state);
        }
        const ResourceState& access(const State& state) {
            return std::visit(
                [](const auto& value) -> const ResourceState& { return value.resource; }, state);
        }
        struct NativeBarriers {
            std::vector<vk::ImageMemoryBarrier2> images;
            std::vector<vk::BufferMemoryBarrier2> buffers;
        };
        Result<NativeBarriers, GraphicsError> prepare_barriers(
            std::span<const RenderGraph::Barrier> barriers,
            std::span<const RenderGraph::Binding> bindings) {
            NativeBarriers result;
            const auto images = std::ranges::count_if(barriers, [](const auto& barrier) {
                return std::holds_alternative<ImageState>(barrier.before);
            });
            result.images.reserve(images);
            result.buffers.reserve(barriers.size() - images);
            for(const auto& barrier : barriers) {
                const auto index = barrier.resource.index;
                if(const auto* image = std::get_if<ImageState>(&barrier.before)) {
                    auto native = Graphics::build_image_memory_barrier(
                        std::get<std::shared_ptr<Image>>(bindings[index])->get(), *image,
                        std::get<ImageState>(barrier.after));
                    if(!native)
                        return Result<NativeBarriers, GraphicsError>::failure(
                            {"Invalid RenderGraph image barrier"});
                    result.images.push_back(*native);
                } else {
                    const auto& before = std::get<RenderGraph::BufferState>(barrier.before);
                    auto native = Graphics::build_buffer_memory_barrier(
                        std::get<std::shared_ptr<Buffer>>(bindings[index])->get(), before.resource,
                        access(barrier.after), before.offset, before.size);
                    if(!native)
                        return Result<NativeBarriers, GraphicsError>::failure(
                            {"Invalid RenderGraph buffer barrier"});
                    result.buffers.push_back(*native);
                }
            }
            return Result<NativeBarriers, GraphicsError>::success(std::move(result));
        }
        void record_barriers(const CommandBuffer& commands, const NativeBarriers& barriers) {
            if(barriers.images.empty() && barriers.buffers.empty())
                return;
            vk::DependencyInfo dependency;
            dependency.setImageMemoryBarriers(barriers.images);
            dependency.setBufferMemoryBarriers(barriers.buffers);
            commands.get().pipelineBarrier2(dependency);
        }

        bool writes(const ResourceState& state) {
            const auto mask = Flags<Access>(Access::ShaderWrite) | Access::TransferWrite
                              | Access::ColorAttachmentWrite | Access::DepthStencilAttachmentWrite
                              | Access::HostWrite | Access::MemoryWrite;
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
            const bool shared_aspect = static_cast<bool>(a.aspects & b.aspects)
                                       || (static_cast<bool>(a.aspects & depth_stencil)
                                           && static_cast<bool>(b.aspects & depth_stencil));
            return shared_aspect
                   && overlaps(a.base_mip_level, a.level_count, b.base_mip_level, b.level_count)
                   && overlaps(
                       a.base_array_layer, a.layer_count, b.base_array_layer, b.layer_count);
        }
        Result<void> validate_initial(const State& state) {
            const auto& resource = access(state);
            if(resource.access && !resource.stages)
                return Result<void>::failure("RenderGraph imported access needs a stage scope");
            if(const auto* image = std::get_if<ImageState>(&state)) {
                const auto& range = image->subresources;
                if(!range.is_valid() || range.level_count == UINT32_MAX
                    || range.layer_count == UINT32_MAX
                    || uint64_t(range.base_mip_level) + range.level_count > UINT32_MAX
                    || uint64_t(range.base_array_layer) + range.layer_count > UINT32_MAX
                    || image->layout == ImageLayout::Preinitialized
                    || image->layout < ImageLayout::Undefined
                    || image->layout > ImageLayout::AttachmentOptimal)
                    return Result<void>::failure("Invalid RenderGraph imported image state");
                const auto supported = Flags<ImageAspect>(ImageAspect::Color) | ImageAspect::Depth
                                       | ImageAspect::Stencil;
                if(static_cast<bool>(range.aspects & ~supported))
                    return Result<void>::failure(
                        "RenderGraph only supports color/depth/stencil aspects");
                if(static_cast<bool>(range.aspects & ImageAspect::Color)
                    && range.aspects != Flags<ImageAspect>(ImageAspect::Color))
                    return Result<void>::failure(
                        "Color and depth/stencil cannot share an image range");
            } else {
                const auto& buffer = std::get<RenderGraph::BufferState>(state);
                if(buffer.size == 0 || buffer.offset > UINT64_MAX - buffer.size)
                    return Result<void>::failure("Invalid RenderGraph buffer range");
            }
            return Result<void>::success();
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
                case ResourceUsage::StorageWrite:
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
        Flags<BufferUsage> buffer_usage(ResourceUsage usage) {
            switch(usage) {
                case ResourceUsage::TransferSource:
                    return Flags<BufferUsage>(BufferUsage::CopySrc);
                case ResourceUsage::TransferDestination:
                    return Flags<BufferUsage>(BufferUsage::CopyDst);
                case ResourceUsage::StorageRead:
                case ResourceUsage::StorageWrite:
                case ResourceUsage::StorageReadWrite:
                    return Flags<BufferUsage>(BufferUsage::Storage);
                case ResourceUsage::UniformRead:
                    return Flags<BufferUsage>(BufferUsage::Uniform);
                case ResourceUsage::VertexBuffer:
                    return Flags<BufferUsage>(BufferUsage::Vertex);
                case ResourceUsage::IndexBuffer:
                    return Flags<BufferUsage>(BufferUsage::Index);
                case ResourceUsage::IndirectBuffer:
                    return Flags<BufferUsage>(BufferUsage::Indirect);
                default:
                    return {};
            }
        }
        Result<State> resolve(const State& previous, const RenderGraph::Use& use) {
            if(use.usage == ResourceUsage::Undefined)
                return Result<State>::failure("RenderGraph pass cannot use undefined contents");
            const auto queue = access(previous).queue_family;
            if(const auto* image = std::get_if<ImageState>(&previous)) {
                auto next =
                    resolve_image_state(use.usage, image->subresources, use.shader_stages, queue);
                if(!next)
                    return Result<State>::failure("Invalid RenderGraph image usage");
                return Result<State>::success(*next);
            }
            if(use.usage == ResourceUsage::SampledRead || use.usage == ResourceUsage::Present
                || use.usage == ResourceUsage::ColorAttachmentWrite
                || use.usage == ResourceUsage::DepthStencilAttachmentWrite
                || use.usage == ResourceUsage::DepthStencilAttachmentRead)
                return Result<State>::failure("Image-only usage applied to RenderGraph buffer");
            auto next = resolve_resource_state(use.usage, use.shader_stages, queue);
            if(!next)
                return Result<State>::failure("Invalid RenderGraph buffer usage");
            auto result = std::get<RenderGraph::BufferState>(previous);
            result.resource = *next;
            return Result<State>::success(result);
        }

        struct TrackedResource {
            State state;
            bool has_writer;
            bool has_contents;
            std::vector<ResourceState> visible;
        };

        Result<void> compile_uses(std::span<const RenderGraph::Use> uses,
            std::span<const RenderGraph::Resource> resources, std::vector<TrackedResource>& tracked,
            std::vector<Flags<ImageUsage>>& image_usages,
            std::vector<RenderGraph::Barrier>& barriers, bool exporting) {
            std::vector<bool> used(resources.size());
            for(const auto& use : uses) {
                if(!exporting
                    && (use.usage == ResourceUsage::HostRead
                        || use.usage == ResourceUsage::HostWrite))
                    return Result<void>::failure(
                        "Host access is an external handoff, not a GPU pass");
                if(use.resource.index >= tracked.size() || used[use.resource.index])
                    return Result<void>::failure(
                        "Invalid or duplicate resource use within RenderGraph pass");
                used[use.resource.index] = true;
                auto& previous = tracked[use.resource.index];
                auto resolved = resolve(previous.state, use);
                if(!resolved)
                    return Result<void>::failure(resolved.error());
                State after = std::move(resolved).value();
                if(std::holds_alternative<ImageState>(after)) {
                    auto& required = image_usages[use.resource.index];
                    required = required | image_usage(use.usage);
                }
                const auto desired = access(after);
                const bool write = writes(desired);
                if(!previous.has_contents
                    && (exporting || !write || use.usage == ResourceUsage::StorageReadWrite))
                    return Result<void>::failure("RenderGraph resource '"
                                                 + resources[use.resource.index].name
                                                 + "' has no initialized producer");
                const bool changed = layout_changed(previous.state, after);
                const bool visible = std::ranges::any_of(previous.visible, [&](const auto& scope) {
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
                    access(after).stages = access(after).stages | access(previous.state).stages;
                    access(after).access = access(after).access | access(previous.state).access;
                    previous.state = after;
                    previous.has_writer |= changed;
                    if(changed)
                        previous.visible = {};
                    previous.visible.push_back(desired);
                }
            }
            return Result<void>::success();
        }
    }

    RenderGraph::ResourceId RenderGraph::add_resource(Resource resource) {
        const ResourceId id{static_cast<uint32_t>(m_resources.size())};
        m_resources.push_back(std::move(resource));
        return id;
    }
    RenderGraph::ResourceId RenderGraph::import_image(std::string name, ImageState initial) {
        return add_resource({std::move(name), initial});
    }
    RenderGraph::ResourceId RenderGraph::import_buffer(std::string name, BufferState initial) {
        return add_resource({std::move(name), initial});
    }
    RenderGraph::ResourceId RenderGraph::create_image(
        std::string name, ImageDescription description) {
        auto aspects = Flags<ImageAspect>(ImageAspect::Color);
        if(Graphics::is_depth_stencil_format(description.format)) {
            aspects = Flags<ImageAspect>(ImageAspect::Depth);
            if(!Graphics::is_depth_only_format(description.format))
                aspects |= ImageAspect::Stencil;
        }
        return add_resource(
            {std::move(name), ImageState{.subresources = {.aspects = aspects}}, description});
    }
    RenderGraph::ResourceId RenderGraph::create_buffer(
        std::string name, BufferDescription description) {
        return add_resource({std::move(name), BufferState{{}, 0, description.size}, description});
    }
    RenderGraph::PassId RenderGraph::add_pass(Pass pass) {
        const PassId id = m_passes.size();
        m_passes.push_back(std::move(pass));
        return id;
    }
    void RenderGraph::export_resource(Use use) {
        m_exports.push_back(use);
    }

    Result<RenderGraph::Plan> RenderGraph::compile(const bool reuse_transients) const {
        if(m_resources.size() > 256 || m_passes.size() > 512 || m_exports.size() > 256)
            return Result<Plan>::failure("RenderGraph declaration limit exceeded");
        std::unordered_set<std::string> names;
        for(const auto& resource : m_resources) {
            if(resource.name.empty() || !names.insert(resource.name).second)
                return Result<Plan>::failure(
                    "RenderGraph resource names must be nonempty and unique");
            if(auto valid = validate_initial(resource.initial); !valid)
                return Result<Plan>::failure(resource.name + ": " + valid.error());
            if(resource.transient) {
                if(const auto* image = std::get_if<ImageDescription>(&*resource.transient)) {
                    const auto samples = static_cast<uint32_t>(image->samples);
                    if(image->format == Format::UNDEFINED || !image->width || !image->height
                        || !samples || samples > 64 || (samples & (samples - 1)))
                        return Result<Plan>::failure("Invalid transient image description");
                }
            }
        }
        names.clear();
        for(const auto& pass : m_passes)
            if(pass.name.empty() || !names.insert(pass.name).second || pass.uses.size() > 256)
                return Result<Plan>::failure("Invalid RenderGraph pass name or use count");
        Plan result;
        result.m_resources = m_resources;
        result.m_image_usages.resize(m_resources.size());
        std::vector<TrackedResource> tracked;
        std::optional<uint32_t> queue;
        for(const auto& resource : m_resources) {
            const auto owner = access(resource.initial).queue_family;
            if(!resource.transient && queue && *queue != owner)
                return Result<Plan>::failure(
                    "RenderGraph requires one queue owner; import explicit handoffs first");
            if(!resource.transient)
                queue = owner;
            tracked.push_back({resource.initial, writes(access(resource.initial)),
                initialized(resource.initial), {}});
        }
        for(const auto& pass : m_passes) {
            CompiledPass compiled{pass.name, {}};
            if(auto uses = compile_uses(pass.uses, m_resources, tracked, result.m_image_usages,
                   compiled.barriers, false);
                !uses)
                return Result<Plan>::failure(pass.name + ": " + uses.error());
            result.m_passes.push_back(std::move(compiled));
        }
        if(auto uses = compile_uses(
               m_exports, m_resources, tracked, result.m_image_usages, result.m_exports, true);
            !uses)
            return Result<Plan>::failure("Exports: " + uses.error());
        for(auto& state : tracked)
            result.m_final_states.push_back(std::move(state.state));

        result.m_lifetimes.resize(m_resources.size());
        std::vector<Flags<BufferUsage>> buffer_usages(m_resources.size());
        for(PassId pass = 0; pass < m_passes.size(); ++pass) {
            for(const auto& use : m_passes[pass].uses) {
                auto& lifetime = result.m_lifetimes[use.resource.index];
                lifetime.first = std::min(lifetime.first, pass);
                lifetime.last = pass;
                buffer_usages[use.resource.index] =
                    buffer_usages[use.resource.index] | buffer_usage(use.usage);
            }
        }
        for(const auto& use : m_exports) {
            result.m_lifetimes[use.resource.index].last = m_passes.size();
            buffer_usages[use.resource.index] =
                buffer_usages[use.resource.index] | buffer_usage(use.usage);
        }
        std::vector<uint32_t> ordered(m_resources.size());
        std::iota(ordered.begin(), ordered.end(), 0);
        std::ranges::stable_sort(ordered, [&](uint32_t a, uint32_t b) {
            return result.m_lifetimes[a].first < result.m_lifetimes[b].first;
        });
        std::vector<std::vector<uint32_t>> occupants;
        for(const auto index : ordered) {
            const auto& description = m_resources[index].transient;
            if(!description)
                continue;
            auto& lifetime = result.m_lifetimes[index];
            if(lifetime.first == std::numeric_limits<PassId>::max())
                return Result<Plan>::failure(
                    "Unused transient resource: " + m_resources[index].name);
            uint32_t allocation = 0;
            for(; allocation < result.m_allocations.size(); ++allocation) {
                const auto previous = occupants[allocation].back();
                if(reuse_transients && result.m_allocations[allocation].description == *description
                    && result.m_lifetimes[previous].last < lifetime.first)
                    break;
            }
            if(allocation == result.m_allocations.size()) {
                result.m_allocations.push_back({*description, {}, {}});
                occupants.emplace_back();
            }
            lifetime.allocation = allocation;
            occupants[allocation].push_back(index);
            auto& physical = result.m_allocations[allocation];
            physical.image_usage = physical.image_usage | result.m_image_usages[index];
            physical.buffer_usage = physical.buffer_usage | buffer_usages[index];
        }
        // 同一 VkImage/VkBuffer 的逻辑内容可丢弃，先前 reader/writer 的执行依赖不能丢。
        // 第一位 occupant 还要等待该 slot 上一次执行计划的最后使用。
        for(const auto& group : occupants) {
            auto previous = group.back();
            for(const auto index : group) {
                const auto first = result.m_lifetimes[index].first;
                auto& barriers = result.m_passes[first].barriers;
                auto before = m_resources[index].initial;
                access(before) = access(result.m_final_states[previous]);
                const auto& uses = m_passes[first].uses;
                const auto use = std::ranges::find(uses, ResourceId{index}, &Use::resource);
                const auto after = resolve(m_resources[index].initial, *use).value();
                const auto barrier =
                    std::ranges::find(barriers, ResourceId{index}, &Barrier::resource);
                if(barrier == barriers.end())
                    barriers.push_back({{index}, std::move(before), after});
                else
                    barrier->before = std::move(before);
                previous = index;
            }
        }
        return Result<Plan>::success(std::move(result));
    }

    Result<RenderGraph::Resources, GraphicsError> RenderGraph::Plan::allocate(
        Device& device, const uint32_t frame_slots) const {
        using Creation = Result<Resources, GraphicsError>;
        if(!frame_slots)
            return Creation::failure({"RenderGraph requires at least one frame slot"});
        for(const auto& allocation : m_allocations) {
            if(const auto* image = std::get_if<ImageDescription>(&allocation.description)) {
                vk::ImageFormatProperties properties;
                const auto status =
                    device.get_capability().physical_device.getImageFormatProperties(
                        Graphics::format_to_vk(image->format), vk::ImageType::e2D,
                        vk::ImageTiling::eOptimal,
                        Graphics::image_usage_to_vk(allocation.image_usage), {}, &properties);
                if(status != vk::Result::eSuccess || image->width > properties.maxExtent.width
                    || image->height > properties.maxExtent.height
                    || !(properties.sampleCounts & Graphics::sample_count_to_vk(image->samples)))
                    return Creation::failure({"Unsupported RenderGraph transient image description",
                        vk::Result::eErrorFormatNotSupported});
            }
        }
        Resources result;
        for(uint32_t index = 0; index < m_resources.size(); ++index)
            if(m_resources[index].transient)
                result.m_ids.push_back({index});
        for(uint32_t slot = 0; slot < frame_slots; ++slot) {
            std::vector<Binding> physical;
            for(const auto& allocation : m_allocations) {
                if(const auto* image = std::get_if<ImageDescription>(&allocation.description)) {
                    auto created = Image::try_create(device,
                        {.format = image->format,
                            .extent = {image->width, image->height, 1},
                            .usage = allocation.image_usage},
                        true, image->samples, "render graph transient image");
                    if(!created)
                        return Creation::failure(created.error());
                    physical.emplace_back(std::move(created).value());
                } else {
                    auto created = Buffer::try_create_gpu_buffer(device, allocation.buffer_usage,
                        std::get<BufferDescription>(allocation.description).size, true,
                        "render graph transient buffer");
                    if(!created)
                        return Creation::failure(created.error());
                    physical.emplace_back(std::move(created).value());
                }
            }
            std::vector<Binding> bindings(m_resources.size());
            for(const auto id : result.m_ids)
                bindings[id.index] = physical[m_lifetimes[id.index].allocation];
            result.m_bindings.push_back(std::move(bindings));
        }
        return Creation::success(std::move(result));
    }

    std::span<const RenderGraph::Binding> RenderGraph::Resources::get_bindings(
        const uint32_t slot) const {
        return m_bindings.at(slot);
    }
    void RenderGraph::Resources::bind_resources(
        std::span<Binding> bindings, const uint32_t slot) const {
        const auto source = get_bindings(slot);
        assert(bindings.size() == source.size());
        for(const auto id : m_ids)
            bindings[id.index] = source[id.index];
    }

    Result<void, GraphicsError> RenderGraph::Plan::record(FrameScheduler& frames,
        std::span<const Binding> bindings, const RecordPass& record_pass,
        Execution execution) const {
        if(!frames.is_recording_frame() || bindings.size() != m_resources.size() || !record_pass)
            return Result<void, GraphicsError>::failure(
                {"RenderGraph requires active frame, complete bindings and recorder"});
        for(size_t index = 0; index < bindings.size(); ++index) {
            const auto owner = access(m_resources[index].initial).queue_family;
            if(owner != UNSPECIFIED_QUEUE_FAMILY && owner != frames.get_queue_family_index())
                return Result<void, GraphicsError>::failure(
                    {"RenderGraph resource belongs to a different recording queue family"});
            if(bindings[index].index() != m_resources[index].initial.index())
                return Result<void, GraphicsError>::failure({"RenderGraph binding kind mismatch"});
            const bool valid_owner = std::visit(
                [&](const auto& owner) {
                    return owner && owner->get() && &owner->get_device() == &frames.get_device();
                },
                bindings[index]);
            if(!valid_owner)
                return Result<void, GraphicsError>::failure(
                    {"RenderGraph binding is null or belongs to another device"});
            if(const auto* image = std::get_if<ImageState>(&m_resources[index].initial)) {
                const auto info = std::get<std::shared_ptr<Image>>(bindings[index])->get_info();
                if(m_resources[index].transient) {
                    const auto& description =
                        std::get<ImageDescription>(*m_resources[index].transient);
                    if(info.format != description.format
                        || info.extent != Math::Vec3u(description.width, description.height, 1))
                        return Result<void, GraphicsError>::failure(
                            {"RenderGraph transient image binding does not match description"});
                }
                const auto& range = image->subresources;
                if((info.usage & m_image_usages[index]) != m_image_usages[index])
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph image binding lacks declared usage"});
                if(uint64_t(range.base_mip_level) + range.level_count > info.mip_levels
                    || uint64_t(range.base_array_layer) + range.layer_count > info.array_layers)
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph image range exceeds bound image"});
                const bool depth = Graphics::is_depth_stencil_format(info.format);
                if(depth && !Graphics::is_depth_only_format(info.format)
                    && range.aspects
                           != (Flags<ImageAspect>(ImageAspect::Depth) | ImageAspect::Stencil))
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph requires coupled depth/stencil without separate-layout feature"});
                if(depth == static_cast<bool>(range.aspects & ImageAspect::Color)
                    || (!depth && range.aspects != Flags<ImageAspect>(ImageAspect::Color))
                    || (Graphics::is_depth_only_format(info.format)
                        && static_cast<bool>(range.aspects & ImageAspect::Stencil)))
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph image aspects do not match format"});
            } else {
                const auto& range = std::get<BufferState>(m_resources[index].initial);
                const auto size = std::get<std::shared_ptr<Buffer>>(bindings[index])->get_size();
                if(range.offset > size || range.size > size - range.offset)
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph buffer range exceeds bound buffer"});
            }
            for(size_t other = 0; other < index; ++other) {
                if(bindings[index].index() != bindings[other].index())
                    continue;
                bool alias = false;
                if(const auto* image = std::get_if<std::shared_ptr<Image>>(&bindings[index])) {
                    alias =
                        (*image)->get() == std::get<std::shared_ptr<Image>>(bindings[other])->get()
                        && overlaps(std::get<ImageState>(m_resources[index].initial).subresources,
                            std::get<ImageState>(m_resources[other].initial).subresources);
                } else {
                    const auto& a = std::get<BufferState>(m_resources[index].initial);
                    const auto& b = std::get<BufferState>(m_resources[other].initial);
                    alias = std::get<std::shared_ptr<Buffer>>(bindings[index])->get()
                                == std::get<std::shared_ptr<Buffer>>(bindings[other])->get()
                            && overlaps(a.offset, a.size, b.offset, b.size);
                }
                const auto allocation = m_lifetimes[index].allocation;
                const bool planned = m_resources[index].transient && m_resources[other].transient
                                     && allocation == m_lifetimes[other].allocation;
                if(alias && !planned)
                    return Result<void, GraphicsError>::failure(
                        {"RenderGraph overlapping aliases need one resource declaration"});
            }
        }
        if(execution == Execution::IndependentCompute)
            for(const auto& resource : m_resources) {
                const auto& state = access(resource.initial);
                if(state.access && state.access != Flags<Access>(Access::HostWrite))
                    return Result<void, GraphicsError>::failure(
                        {"Independent graph inputs must be host-written or undefined; GPU producers require an explicit queue dependency"});
                if(const auto* image = std::get_if<ImageState>(&resource.initial);
                    image && image->layout != ImageLayout::Undefined)
                    return Result<void, GraphicsError>::failure(
                        {"Independent graph images require undefined initial layout"});
            }
        // 所有绑定和 barrier 先验证，再对当前命令缓冲产生任何副作用。
        std::vector<NativeBarriers> native;
        native.reserve(m_passes.size() + 1);
        for(const auto& pass : m_passes) {
            auto prepared = prepare_barriers(pass.barriers, bindings);
            if(!prepared)
                return Result<void, GraphicsError>::failure(prepared.error());
            native.push_back(std::move(prepared).value());
        }
        auto exports = prepare_barriers(m_exports, bindings);
        if(!exports)
            return Result<void, GraphicsError>::failure(exports.error());
        native.push_back(std::move(exports).value());
        for(const auto& binding : bindings)
            std::visit(
                [&](const auto& owner) { frames.retain_current_frame_resource(owner); }, binding);
        auto record = [barriers = std::move(native), recorder = record_pass,
                          count = m_passes.size()](
                          CommandBuffer& commands) -> Result<void, GraphicsError> {
            for(size_t index = 0; index < count; ++index) {
                record_barriers(commands, barriers[index]);
                if(auto recorded = recorder(index, commands); !recorded)
                    return recorded;
            }
            record_barriers(commands, barriers.back());
            return Result<void, GraphicsError>::success();
        };
        if(execution == Execution::IndependentCompute)
            return frames.record_independent(std::move(record));
        return record(frames.get_current_command_buffer());
    }
}
