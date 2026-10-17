#include "render/passes/gpu_particle_simulation.h"
#include "graphics/device.h"
#include "graphics/creation.h"
#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "particle_simulation_comp.h"
#include "particle_sort_comp.h"
#include <array>
#include <bit>
#include <cmath>

namespace Comet {
    namespace {
        struct Spawn {
            Math::Mat4 pose;
            Math::Vec4 timing;
            std::array<uint32_t, 4> range;
        };
        struct Parameters {
            Math::Mat4 view;
            Math::Vec4 acceleration_lifetime, start_color, end_color, sizes_speed_cone;
            Math::Vec4 atlas, animation;
            std::array<uint32_t, 4> counts;
        };
        static_assert(sizeof(Spawn) == 96 && sizeof(Parameters) == 176);
    }
    struct GpuParticleSimulation::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline simulate, sort;
    };
    struct GpuParticleSimulation::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<CPUBuffer> spawns, parameters;
        std::shared_ptr<Buffer> instances, indirect;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        uint32_t capacity = 0;
    };
    GpuParticleSimulation::GpuParticleSimulation(Device& device, uint32_t slots)
        : m_device(device), m_frames(slots) {}
    GpuParticleSimulation::~GpuParticleSimulation() = default;
    void GpuParticleSimulation::reset() {
        for(auto& frame : m_frames)
            frame.clear();
        m_outputs.clear();
    }
    Result<void, GraphicsError> GpuParticleSimulation::initialize() {
        using Initialized = Result<void, GraphicsError>;
        if(m_program)
            return Initialized::success();
        auto next = std::make_shared<Program>();
        DescriptorSetLayoutBindings bindings;
        for(uint32_t i = 0; i < 4; ++i)
            bindings.add_binding(
                i, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
        auto descriptors = DescriptorSetLayout::create(m_device, bindings);
        if(!descriptors)
            return Initialized::failure(descriptors.error());
        next->descriptors = std::move(descriptors).value();
        const auto set = next->descriptors->get();
        const vk::PushConstantRange constants(vk::ShaderStageFlagBits::eCompute, 0, 8);
        const vk::PipelineLayoutCreateInfo layout_info({}, 1, &set, 1, &constants);
        auto layout = Graphics::create_handle<vk::PipelineLayout>(
            m_device.get(), "Create particle simulation layout", [&](auto* out) noexcept {
                return m_device.get().createPipelineLayout(&layout_info, nullptr, out);
            });
        if(!layout)
            return Initialized::failure(layout.error());
        next->layout = std::move(layout).value();
        const auto compile = [&](std::string_view name, const auto& code,
                                 vk::UniquePipeline& output) -> Initialized {
            auto shader = Shader::create(m_device, std::string(name), code);
            if(!shader)
                return Initialized::failure(shader.error());
            const vk::PipelineShaderStageCreateInfo stage(
                {}, vk::ShaderStageFlagBits::eCompute, shader.value()->get(), "main");
            const vk::ComputePipelineCreateInfo info({}, stage, next->layout.get());
            auto pipeline = Graphics::create_handle<vk::Pipeline>(
                m_device.get(), name, [&](auto* out) noexcept {
                    return m_device.get().createComputePipelines({}, 1, &info, nullptr, out);
                });
            if(!pipeline)
                return Initialized::failure(pipeline.error());
            output = std::move(pipeline).value();
            return Initialized::success();
        };
        if(auto result =
                compile("GPU particle simulation", PARTICLE_SIMULATION_COMP, next->simulate);
            !result)
            return result;
        if(auto result = compile("GPU particle depth sort", PARTICLE_SORT_COMP, next->sort);
            !result)
            return result;
        m_program = std::move(next);
        return Initialized::success();
    }
    Result<std::shared_ptr<GpuParticleSimulation::Resources>, GraphicsError> GpuParticleSimulation::
        allocate(uint32_t count, size_t batches) {
        using Allocated = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto next = std::make_shared<Resources>();
        next->program = m_program;
        next->capacity = std::bit_ceil(count);
        auto spawns =
            Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                std::bit_ceil(batches * sizeof(Spawn)), true, nullptr, "GPU particle births");
        auto parameters =
            Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                sizeof(Parameters), true, nullptr, "GPU particle parameters");
        auto instances = Buffer::try_create_gpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Vertex | BufferUsage::CopySrc,
            next->capacity * sizeof(ParticleInstance), true, "GPU particle instances");
        auto indirect = Buffer::try_create_gpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Indirect | BufferUsage::CopySrc,
            sizeof(vk::DrawIndirectCommand), true, "GPU particle draw");
        if(!spawns)
            return Allocated::failure({"Cannot allocate GPU particle births", spawns.result()});
        if(!parameters)
            return Allocated::failure(
                {"Cannot allocate GPU particle parameters", parameters.result()});
        if(!instances)
            return Allocated::failure(
                {"Cannot allocate GPU particle instances", instances.result()});
        if(!indirect)
            return Allocated::failure({"Cannot allocate GPU particle draw", indirect.result()});
        next->spawns = std::move(spawns).value();
        next->parameters = std::move(parameters).value();
        next->instances = std::move(instances).value();
        next->indirect = std::move(indirect).value();
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::StorageBuffer, 4);
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return Allocated::failure(pool.error());
        next->pool = std::move(pool).value();
        auto descriptors = next->pool->allocate_descriptor_set(*m_program->descriptors, 1);
        if(!descriptors)
            return Allocated::failure(descriptors.error());
        next->descriptors = std::move(descriptors).value();
        const std::array<std::shared_ptr<Buffer>, 4> buffers{
            next->spawns, next->parameters, next->instances, next->indirect};
        std::array<vk::DescriptorBufferInfo, 4> infos;
        std::array<vk::WriteDescriptorSet, 4> writes;
        for(uint32_t i = 0; i < buffers.size(); ++i) {
            infos[i] = vk::DescriptorBufferInfo(buffers[i]->get(), 0, buffers[i]->get_size());
            writes[i] = vk::WriteDescriptorSet(next->descriptors.front().get(), i, 0, 1,
                vk::DescriptorType::eStorageBuffer, nullptr, &infos[i]);
        }
        m_device.get().updateDescriptorSets(writes, {});
        return Allocated::success(std::move(next));
    }
    Result<void, GraphicsError> GpuParticleSimulation::prepare(FrameScheduler& frames,
        const ViewProjectMatrix& camera, std::span<const GpuParticleEmitter> emitters) {
        using Prepared = Result<void, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frames.size())
            return Prepared::failure({"Invalid GPU particle frame"});
        m_outputs.clear();
        if(emitters.empty()) {
            reset();
            return Prepared::success();
        }
        if(auto result = initialize(); !result)
            return result;
        auto& cache = m_frames.at(frames.get_current_frame_slot_index());
        cache.resize(emitters.size());
        const auto command = frames.get_current_command_buffer().get();
        uint32_t total = 0;
        std::vector<Spawn> births;
        for(size_t i = 0; i < emitters.size(); ++i) {
            const auto& emitter = emitters[i];
            if(!emitter.settings.validate()
                || emitter.settings.simulation != ParticleSimulation::GPU || !emitter.count
                || emitter.count > emitter.settings.capacity
                || emitter.count > ParticleSnapshot::MAX_GPU_PARTICLES - total
                || emitter.spawns.empty())
                return Prepared::failure({"Invalid GPU particle snapshot"});
            total += emitter.count;
            births.clear();
            births.reserve(emitter.spawns.size());
            uint32_t offset = 0;
            for(const auto& spawn : emitter.spawns) {
                if(!spawn.count || spawn.count > emitter.count - offset
                    || !std::isfinite(spawn.time) || !std::isfinite(spawn.spacing)
                    || spawn.spacing < 0 || !std::isfinite(emitter.time))
                    return Prepared::failure({"Invalid GPU particle birth range"});
                births.push_back(
                    {spawn.pose, {float(emitter.time - spawn.time), float(spawn.spacing), 0, 0},
                        {offset, spawn.count, uint32_t(spawn.first), 0}});
                offset += spawn.count;
            }
            if(offset != emitter.count)
                return Prepared::failure({"GPU particle births do not cover the emitter"});
            auto& resources = cache[i];
            if(!resources || resources->capacity < std::bit_ceil(emitter.count)
                || resources->spawns->get_size() < births.size() * sizeof(Spawn)) {
                auto next = allocate(emitter.count, births.size());
                if(!next)
                    return Prepared::failure(next.error());
                resources = std::move(next).value();
            }
            const auto& settings = emitter.settings;
            const uint32_t padded = std::bit_ceil(emitter.count);
            const Parameters parameters{camera.view,
                Math::Vec4(settings.acceleration, settings.lifetime), settings.start_color,
                settings.end_color,
                {settings.start_size, settings.end_size, settings.speed,
                    std::cos(glm::radians(settings.spread))},
                {float(settings.atlas_columns), float(settings.atlas_rows),
                    float(settings.frame_count), 0},
                {settings.frame_rate, settings.loop_animation ? 1.f : 0.f, 0, 0},
                {uint32_t(births.size()), emitter.count, padded, settings.seed}};
            resources->spawns->write(births.data(), births.size() * sizeof(Spawn));
            resources->parameters->write(&parameters);
            // 槽已等待完成；显式覆盖上一轮绘制／复制的读取，再发布本轮 compute 输出。
            const vk::MemoryBarrier before(
                vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndirectCommandRead
                    | vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderWrite
                    | vk::AccessFlagBits::eShaderRead,
                vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eVertexInput
                                        | vk::PipelineStageFlagBits::eDrawIndirect
                                        | vk::PipelineStageFlagBits::eTransfer
                                        | vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader, {}, before, {}, {});
            command.bindPipeline(vk::PipelineBindPoint::eCompute, m_program->simulate.get());
            command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_program->layout.get(), 0,
                resources->descriptors.front().get(), {});
            command.dispatch((padded + 63) / 64, 1, 1);
            if(settings.blend == ParticleBlend::Alpha) {
                command.bindPipeline(vk::PipelineBindPoint::eCompute, m_program->sort.get());
                const vk::MemoryBarrier between(vk::AccessFlagBits::eShaderWrite,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                for(uint32_t stage = 2; stage <= padded; stage *= 2)
                    for(uint32_t stride = stage / 2; stride; stride /= 2) {
                        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader, {}, between, {}, {});
                        const std::array constants{stage, stride};
                        command.pushConstants(m_program->layout.get(),
                            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants),
                            constants.data());
                        command.dispatch((padded + 63) / 64, 1, 1);
                    }
            }
            const vk::MemoryBarrier after(
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eVertexAttributeRead
                                                      | vk::AccessFlagBits::eIndirectCommandRead);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eDrawIndirect,
                {}, after, {}, {});
            frames.retain_current_frame_resource(resources);
            m_outputs.push_back({resources->instances, resources->indirect, emitter.count});
        }
        return Prepared::success();
    }
}
