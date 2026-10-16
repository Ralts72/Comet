#include "render/passes/skinning_pass.h"
#include "animation/animation_data.h"
#include "graphics/device.h"
#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/shader.h"
#include "graphics/resource/buffer.h"
#include "graphics/creation.h"
#include "render/frame_scheduler.h"
#include "render/resource/mesh.h"
#include "render/scene/render_geometry.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
#include "skinning_comp.h"
#include <array>

namespace Comet {
    struct SkinningPass::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline pipeline;
    };
    struct SkinningPass::Instance {
        std::shared_ptr<Program> program;
        std::shared_ptr<Mesh> source;
        std::shared_ptr<Mesh> output;
        std::shared_ptr<CPUBuffer> palette;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        std::shared_ptr<const AnimationPlayback> playback;
        std::optional<BoundingBox> pose_bounds;
        std::vector<Math::Mat4> matrices;
        bool output_ready = false;
        uint64_t recorded_serial = 0;
        std::string animation_error;
    };
    SkinningPass::SkinningPass(Device& device, uint32_t frame_slots)
        : m_device(device), m_frames(frame_slots) {}
    SkinningPass::~SkinningPass() = default;

    Result<void, GraphicsError> SkinningPass::initialize() {
        if(m_program)
            return Result<void, GraphicsError>::success();
        auto program = std::make_shared<Program>();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t binding = 0; binding < 4; ++binding)
            bindings.add_binding(
                binding, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(m_device, bindings);
        if(!descriptors)
            return Result<void, GraphicsError>::failure(descriptors.error());
        program->descriptors = std::move(descriptors).value();
        const auto set = program->descriptors->get();
        const vk::PushConstantRange constants(
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t));
        const vk::PipelineLayoutCreateInfo layout_info({}, 1, &set, 1, &constants);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            m_device.get(), "Create skinning layout", [&](auto* output) noexcept {
                return m_device.get().createPipelineLayout(&layout_info, nullptr, output);
            });
        if(!layout)
            return Result<void, GraphicsError>::failure(layout.error());
        program->layout = std::move(layout).value();
        auto shader = Shader::create(m_device, "GPU skinning", SKINNING_COMP);
        if(!shader)
            return Result<void, GraphicsError>::failure(shader.error());
        const vk::PipelineShaderStageCreateInfo stage(
            {}, vk::ShaderStageFlagBits::eCompute, shader.value()->get(), "main");
        const vk::ComputePipelineCreateInfo pipeline_info({}, stage, program->layout.get());
        auto pipeline = Graphics::create_handle<vk::Pipeline>(
            m_device.get(), "Create skinning pipeline", [&](auto* output) noexcept {
                return m_device.get().createComputePipelines(
                    {}, 1, &pipeline_info, nullptr, output);
            });
        if(!pipeline)
            return Result<void, GraphicsError>::failure(pipeline.error());
        program->pipeline = std::move(pipeline).value();
        m_program = std::move(program);
        return Result<void, GraphicsError>::success();
    }
    Result<std::shared_ptr<SkinningPass::Instance>, GraphicsError> SkinningPass::create_instance(
        const std::shared_ptr<Mesh>& source) {
        using Created = Result<std::shared_ptr<Instance>, GraphicsError>;
        auto instance = std::make_shared<Instance>();
        instance->source = source;
        instance->program = m_program;
        auto output = Buffer::try_create_gpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Vertex) | BufferUsage::Storage,
            source->m_vertex_buffer->get_size(), false, "skinned vertices");
        if(!output)
            return Created::failure({"Cannot allocate skinned vertices", output.result()});
        instance->output = std::shared_ptr<Mesh>(
            new Mesh(std::move(output).value(), source->m_index_buffer, source->m_ready_completion,
                source->m_local_bounds, source->m_vertex_count, source->m_index_count));
        auto palette =
            Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                source->m_animation->joints.size() * sizeof(Math::Mat4), false, nullptr,
                "skin palette");
        if(!palette)
            return Created::failure({"Cannot allocate skin palette", palette.result()});
        instance->palette = std::move(palette).value();
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::StorageBuffer, 4);
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return Created::failure(pool.error());
        instance->pool = std::move(pool).value();
        auto descriptors = instance->pool->allocate_descriptor_set(*m_program->descriptors, 1);
        if(!descriptors)
            return Created::failure(descriptors.error());
        instance->descriptors = std::move(descriptors).value();
        const std::array buffers{source->m_vertex_buffer, source->m_skin_buffer,
            std::shared_ptr<Buffer>(instance->palette), instance->output->m_vertex_buffer};
        std::array<vk::DescriptorBufferInfo, 4> infos;
        std::array<vk::WriteDescriptorSet, 4> writes;
        for(uint32_t index = 0; index < 4; ++index) {
            infos[index] =
                vk::DescriptorBufferInfo(buffers[index]->get(), 0, buffers[index]->get_size());
            writes[index] = vk::WriteDescriptorSet(instance->descriptors.front().get(), index, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &infos[index]);
        }
        m_device.get().updateDescriptorSets(writes, {});
        return Created::success(std::move(instance));
    }
    Result<void, GraphicsError> SkinningPass::prepare(const RenderWorld& world,
        FrameScheduler& frames, RenderGeometry& geometry,
        std::vector<QueueSemaphoreSubmit>& waits) {
        PROFILE_SCOPE("SkinningPass::prepare");
        m_statistics = {};
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frames.size())
            return Result<void, GraphicsError>::failure({"Invalid skinning frame"});
        auto& cache = m_frames.at(frames.get_current_frame_slot_index());
        const auto is_skinned = [&](std::size_t index) {
            return index < world.render_items.size() && world.render_items[index].mesh
                   && world.render_items[index].mesh->is_skinned();
        };
        m_items.clear();
        m_slots.clear();
        for(std::size_t index = 0; index < world.render_items.size(); ++index) {
            const auto& item = world.render_items[index];
            if(is_skinned(index)) {
                m_items.push_back(item);
                m_slots.push_back(index);
            }
        }
        auto command = frames.get_current_command_buffer().get();
        for(std::size_t slot = 0; slot < m_slots.size(); ++slot) {
            const auto index = m_slots[slot];
            auto& item = m_items[slot];
            if(auto initialized = initialize(); !initialized)
                return initialized;
            auto& instance = cache[index];
            if(!instance || instance->source != item.mesh) {
                auto next = create_instance(item.mesh);
                if(!next)
                    return Result<void, GraphicsError>::failure(next.error());
                instance = std::move(next).value();
            }
            // 未成功提交的录制不能成为复用来源；当前槽已等待上次提交完成。
            if(instance->recorded_serial) {
                instance->output_ready = instance->recorded_serial
                                         == frames.get_current_frame_slot().last_submission_serial;
                instance->recorded_serial = 0;
            }
            bool changed = !instance->output_ready;
            if(!instance->pose_bounds || instance->playback != item.animation) {
                const auto& data = *item.mesh->m_animation;
                std::string error;
                std::optional<AnimationPose> sampled;
                const AnimationPose* pose = nullptr;
                if(item.animation && item.animation->pose
                    && item.animation->pose_source == item.mesh->m_animation) {
                    pose = item.animation->pose.get();
                    error = item.animation->pose_error;
                } else {
                    auto resolved =
                        resolve_animation_pose(data, item.animation.get(), error, false);
                    if(!resolved)
                        return Result<void, GraphicsError>::failure({resolved.error()});
                    sampled = std::move(resolved).value();
                    pose = &*sampled;
                }
                if(error != instance->animation_error) {
                    if(!error.empty())
                        LOG_ERROR("Animation for entity {}: {}", item.entity_id, error);
                    instance->animation_error = std::move(error);
                }
                if(instance->matrices != pose->matrices) {
                    instance->matrices = pose->matrices;
                    changed = true;
                }
                instance->pose_bounds = pose->bounds;
                instance->playback = item.animation;
            }
            const auto& ready = item.mesh->m_ready_completion;
            if(ready.is_valid() && !ready.is_complete())
                merge_semaphore_wait(
                    waits, QueueSemaphoreSubmit(
                               ready, Flags<PipelineStage>(PipelineStage::ComputeShader)));
            if(changed) {
                instance->palette->write(instance->matrices.data());
                m_statistics.palette_upload_bytes += instance->matrices.size() * sizeof(Math::Mat4);
                // 输入上传与上一次顶点读取在 dispatch 前完成；输出在所有绘制前可见。
                const vk::MemoryBarrier before(
                    vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eVertexAttributeRead,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                command.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eVertexInput,
                    vk::PipelineStageFlagBits::eComputeShader, {}, before, {}, {});
                command.bindPipeline(vk::PipelineBindPoint::eCompute, m_program->pipeline.get());
                const auto descriptor = instance->descriptors.front().get();
                command.bindDescriptorSets(
                    vk::PipelineBindPoint::eCompute, m_program->layout.get(), 0, descriptor, {});
                const uint32_t vertices = item.mesh->m_vertex_count;
                command.pushConstants(m_program->layout.get(), vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(vertices), &vertices);
                command.dispatch((vertices + 63) / 64, 1, 1);
                const vk::MemoryBarrier after(
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eVertexAttributeRead);
                command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eVertexInput, {}, after, {}, {});
                instance->output_ready = false;
                instance->recorded_serial = frames.get_current_frame_serial();
                ++m_statistics.dispatches;
            } else
                ++m_statistics.reused_outputs;
            frames.retain_current_frame_resource(instance);
            item.mesh = instance->output;
            instance->output->m_local_bounds = *instance->pose_bounds;
            const auto bounds = transform_box(*instance->pose_bounds, item.model_matrix);
            if(!bounds)
                return Result<void, GraphicsError>::failure({"Invalid skinned world bounds"});
            geometry.replace_skinned(index, item, *bounds);
        }
        std::erase_if(cache, [&](const auto& item) { return !is_skinned(item.first); });
        return Result<void, GraphicsError>::success();
    }
}
