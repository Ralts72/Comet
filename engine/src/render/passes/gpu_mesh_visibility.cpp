#include "render/passes/gpu_mesh_visibility.h"
#include "render/frame_scheduler.h"
#include "render/render_graph.h"
#include "render/scene/render_types.h"
#include "graphics/device.h"
#include "graphics/creation.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/buffer.h"
#include "mesh_visibility_comp.h"
#include <array>
#include <bit>

namespace Comet {
    struct GpuMeshVisibility::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
    };
    struct GpuMeshVisibility::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<Buffer> source, output;
        std::shared_ptr<CPUBuffer> commands;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
    };
    GpuMeshVisibility::GpuMeshVisibility(Device& device, uint32_t slots)
        : m_device(device), m_frames(slots) {}
    GpuMeshVisibility::~GpuMeshVisibility() = default;
    Result<void, GraphicsError> GpuMeshVisibility::initialize() {
        using R = Result<void, GraphicsError>;
        if(m_program)
            return R::success();
        auto next = std::make_shared<Program>();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t i = 0; i < 3; ++i)
            bindings.add_binding(
                i, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(m_device, bindings);
        if(!descriptors)
            return R::failure(descriptors.error());
        next->descriptors = std::move(descriptors).value();
        const auto set = next->descriptors->get();
        const vk::PushConstantRange constants(vk::ShaderStageFlagBits::eCompute, 0, 112);
        const vk::PipelineLayoutCreateInfo layout_info({}, 1, &set, 1, &constants);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            m_device.get(), "mesh cull layout", [&](auto* out) noexcept {
                return m_device.get().createPipelineLayout(&layout_info, nullptr, out);
            });
        if(!layout)
            return R::failure(layout.error());
        next->layout = std::move(layout).value();
        auto shader = Shader::create(m_device, "mesh visibility", MESH_VISIBILITY_COMP);
        if(!shader)
            return R::failure(shader.error());
        const vk::PipelineShaderStageCreateInfo stage(
            {}, vk::ShaderStageFlagBits::eCompute, shader.value()->get(), "main");
        const vk::ComputePipelineCreateInfo info({}, stage, next->layout.get());
        auto pipeline = Graphics::create_handle<vk::Pipeline>(
            m_device.get(), "mesh cull pipeline", [&](auto* out) noexcept {
                return m_device.get().createComputePipelines({}, 1, &info, nullptr, out);
            });
        if(!pipeline)
            return R::failure(pipeline.error());
        next->pipeline = std::move(pipeline).value();
        m_program = std::move(next);
        return R::success();
    }
    Result<void, GraphicsError> GpuMeshVisibility::prepare(FrameScheduler& frames,
        const Math::Mat4& clip, const std::shared_ptr<Buffer>& source,
        std::span<const Batch> batches, uint32_t count) {
        using R = Result<void, GraphicsError>;
        if(!frames.is_recording_frame() || !source || !count || batches.empty()
            || !Math::is_finite(clip) || source->get_size() < size_t(count) * sizeof(PushConstant))
            return R::failure({"Invalid mesh visibility input"});
        if(auto ready = initialize(); !ready)
            return ready;
        auto& resources = m_frames.at(frames.get_current_frame_slot_index());
        const auto bytes = size_t(count) * sizeof(PushConstant),
                   command_bytes = batches.size() * sizeof(vk::DrawIndexedIndirectCommand);
        if(!resources || resources->output->get_size() < bytes
            || resources->commands->get_size() < command_bytes) {
            auto next = std::make_shared<Resources>();
            next->program = m_program;
            auto output = Buffer::try_create_gpu_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Vertex
                    | BufferUsage::CopySrc,
                std::bit_ceil(bytes), true, "visible mesh instances");
            auto commands = Buffer::try_create_cpu_buffer(m_device,
                Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Indirect
                    | BufferUsage::CopySrc,
                std::bit_ceil(command_bytes), true, nullptr, "mesh indirect commands");
            if(!output)
                return R::failure(output.error());
            if(!commands)
                return R::failure(commands.error());
            next->output = std::move(output).value();
            next->commands = std::move(commands).value();
            DescriptorPoolSizes sizes;
            sizes.add_pool_size(DescriptorType::StorageBuffer, 3);
            auto pool = DescriptorPool::create(m_device, 1, sizes);
            if(!pool)
                return R::failure(pool.error());
            next->pool = std::move(pool).value();
            auto sets = next->pool->allocate_descriptor_set(*m_program->descriptors, 1);
            if(!sets)
                return R::failure(sets.error());
            next->descriptors = std::move(sets).value();
            resources = std::move(next);
        }
        resources->source = source;
        std::array<vk::DescriptorBufferInfo, 3> infos{
            vk::DescriptorBufferInfo(source->get(), 0, bytes),
            vk::DescriptorBufferInfo(resources->output->get(), 0, bytes),
            vk::DescriptorBufferInfo(resources->commands->get(), 0, command_bytes)};
        std::array<vk::WriteDescriptorSet, 3> writes;
        for(uint32_t i = 0; i < 3; ++i)
            writes[i] = {resources->descriptors[0].get(), i, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]};
        m_device.get().updateDescriptorSets(writes, {});
        std::vector<vk::DrawIndexedIndirectCommand> commands;
        for(const auto& batch : batches) {
            if(batch.first > count || batch.count > count - batch.first || !batch.bounds.is_valid())
                return R::failure({"Invalid mesh visibility batch"});
            commands.emplace_back(batch.elements, 0, 0, 0, 0);
        }
        resources->commands->write(commands.data(), command_bytes);
        RenderGraph graph;
        const auto host = resolve_resource_state(ResourceUsage::HostWrite).value();
        const auto input = graph.import_buffer("mesh instances", {host, 0, bytes});
        const auto output = graph.import_buffer("visible instances", {{}, 0, bytes});
        const auto indirect = graph.import_buffer("mesh commands", {host, 0, command_bytes});
        graph.add_pass(
            {"mesh frustum culling", {{input, ResourceUsage::StorageRead,
                                          Flags<PipelineStage>(PipelineStage::ComputeShader)},
                                         {output, ResourceUsage::StorageWrite,
                                             Flags<PipelineStage>(PipelineStage::ComputeShader)},
                                         {indirect, ResourceUsage::StorageReadWrite,
                                             Flags<PipelineStage>(PipelineStage::ComputeShader)}}});
        graph.export_resource({output, ResourceUsage::VertexBuffer, {}});
        graph.export_resource({indirect, ResourceUsage::IndirectBuffer, {}});
        const auto plan = graph.compile();
        if(!plan)
            return R::failure({plan.error()});
        const std::array<RenderGraph::Binding, 3> bound{
            source, resources->output, std::shared_ptr<Buffer>(resources->commands)};
        auto recorded = plan.value().record(
            frames, bound,
            [program = m_program, resources, clip,
                batches = std::vector<Batch>(batches.begin(), batches.end())](
                auto, CommandBuffer& command) {
                auto native = command.get();
                native.bindPipeline(vk::PipelineBindPoint::eCompute, program->pipeline.get());
                const auto set = resources->descriptors[0].get();
                native.bindDescriptorSets(
                    vk::PipelineBindPoint::eCompute, program->layout.get(), 0, set, {});
                for(uint32_t i = 0; i < batches.size(); ++i) {
                    const auto& batch = batches[i];
                    struct Constants {
                        Math::Mat4 clip;
                        Math::Vec4 minimum, maximum;
                        std::array<uint32_t, 4> range;
                    };
                    static_assert(sizeof(Constants) == 112);
                    const Constants constants{clip, Math::Vec4(batch.bounds.minimum, 0),
                        Math::Vec4(batch.bounds.maximum, 0), {batch.first, batch.count, i, 0}};
                    native.pushConstants(program->layout.get(), vk::ShaderStageFlagBits::eCompute,
                        0, sizeof(constants), &constants);
                    native.dispatch((batch.count + 63) / 64, 1, 1);
                }
                return R::success();
            },
            RenderGraph::Execution::IndependentCompute);
        frames.retain_current_frame_resource(resources);
        return recorded;
    }
    std::shared_ptr<Buffer> GpuMeshVisibility::instances(uint32_t slot) const {
        return m_frames.at(slot)->output;
    }
    std::shared_ptr<Buffer> GpuMeshVisibility::commands(uint32_t slot) const {
        return m_frames.at(slot)->commands;
    }
}
