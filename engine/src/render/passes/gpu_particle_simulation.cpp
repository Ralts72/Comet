#include "render/passes/gpu_particle_simulation.h"
#include "render/resource/vector_field.h"
#include "render/resource/texture.h"
#include "graphics/device.h"
#include "graphics/creation.h"
#include "graphics/command/command_buffer.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/resource/buffer.h"
#include "render/frame_scheduler.h"
#include "particle_simulation_comp.h"
#include "particle_sort_comp.h"
#include "particle_dynamics_comp.h"
#include "particle_initialize_comp.h"
#include "graphics/resource/image_view.h"
#include <algorithm>
#include <unordered_map>
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
        struct DynamicParameters {
            Parameters base;
            Math::Mat4 view_projection, inverse_view_projection;
            Math::Vec4 timing, dynamics;
            std::array<uint32_t, 4> history;
            Math::Vec4 wind, vortex_center, vortex_axis;
            Math::Vec4 field_minimum, field_size, field_dimensions;
            std::array<uint32_t, 4> world;
            std::array<ParticleWorldCollider, ParticleWorldCollider::MAX_COLLIDERS> colliders;
        };
        static_assert(sizeof(Spawn) == 96 && sizeof(Parameters) == 176);
        static_assert(sizeof(ParticleWorldCollider) == 96);
        static_assert(sizeof(DynamicParameters) == 352 + 64 + 48 + 64 * 96);
    }
    struct GpuParticleSimulation::Program {
        std::shared_ptr<DescriptorSetLayout> descriptors;
        vk::UniquePipelineLayout layout;
        vk::UniquePipeline simulate, sort, initialize;
    };
    struct GpuParticleSimulation::Resources {
        std::shared_ptr<Program> program;
        std::shared_ptr<CPUBuffer> spawns, parameters;
        std::shared_ptr<Buffer> instances, indirect, alternate, previous;
        std::shared_ptr<ImageView> depth;
        std::shared_ptr<const VectorField> field;
        std::unique_ptr<DescriptorPool> pool;
        std::vector<DescriptorSet> descriptors;
        uint32_t capacity = 0;
    };
    struct GpuParticleSimulation::Dynamics {
        struct History {
            std::shared_ptr<Buffer> state;
            ParticleEmitterComponent settings;
            std::shared_ptr<const VectorField> field;
            double time;
            uint64_t serial;
            uint32_t count;
        };
        std::unordered_map<uint64_t, History> history;
        uint64_t lifetime = 0;
        std::vector<Spawn> births;
    };
    GpuParticleSimulation::GpuParticleSimulation(Device& device, uint32_t slots)
        : m_device(device), m_dynamics(std::make_unique<Dynamics>()), m_frames(slots) {}
    GpuParticleSimulation::~GpuParticleSimulation() = default;
    void GpuParticleSimulation::reset() {
        for(auto& frame : m_frames)
            frame.clear();
        m_outputs.clear();
        m_dynamics->history.clear();
    }
    Result<void, GraphicsError> GpuParticleSimulation::initialize(bool dynamic, bool sorted) {
        using Initialized = Result<void, GraphicsError>;
        auto& program = dynamic ? m_dynamic_program : m_program;
        if(program && (!sorted || program->sort))
            return Initialized::success();
        auto next = program ? program : std::make_shared<Program>();
        if(!program) {
            DescriptorSetLayoutBindings bindings;
            for(uint32_t i = 0; i < 4; ++i)
                bindings.add_binding(
                    i, DescriptorType::StorageBuffer, Flags<ShaderStage>(ShaderStage::Compute));
            if(dynamic)
                for(uint32_t binding : {4u, 5u})
                    bindings.add_binding(binding, DescriptorType::SampledImage,
                        Flags<ShaderStage>(ShaderStage::Compute));
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
        }
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
        const auto code = dynamic ? std::span<const uint32_t>(PARTICLE_DYNAMICS_COMP)
                                  : std::span<const uint32_t>(PARTICLE_SIMULATION_COMP);
        if(!next->simulate)
            if(auto result = compile("GPU particle simulation", code, next->simulate); !result)
                return result;
        if(dynamic && !next->initialize)
            if(auto result =
                    compile("GPU particle initialize", PARTICLE_INITIALIZE_COMP, next->initialize);
                !result)
                return result;
        if(sorted && !next->sort)
            if(auto result = compile("GPU particle depth sort", PARTICLE_SORT_COMP, next->sort);
                !result)
                return result;
        program = std::move(next);
        return Initialized::success();
    }
    Result<std::shared_ptr<GpuParticleSimulation::Resources>, GraphicsError> GpuParticleSimulation::
        allocate(uint32_t count, size_t batches, bool dynamic) {
        using Allocated = Result<std::shared_ptr<Resources>, GraphicsError>;
        auto next = std::make_shared<Resources>();
        next->program = dynamic ? m_dynamic_program : m_program;
        next->capacity = std::bit_ceil(count);
        auto spawns =
            Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                std::bit_ceil(batches * sizeof(Spawn) + (dynamic ? sizeof(DynamicParameters) : 0)),
                true, nullptr, "GPU particle births");
        if(!spawns)
            return Allocated::failure({"Cannot allocate GPU particle births", spawns.result()});
        next->spawns = std::move(spawns).value();
        if(dynamic)
            next->parameters = next->spawns;
        else {
            auto parameters =
                Buffer::try_create_cpu_buffer(m_device, Flags<BufferUsage>(BufferUsage::Storage),
                    sizeof(Parameters), true, nullptr, "GPU particle parameters");
            if(!parameters)
                return Allocated::failure(
                    {"Cannot allocate GPU particle parameters", parameters.result()});
            next->parameters = std::move(parameters).value();
        }
        auto instances = Buffer::try_create_gpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Vertex | BufferUsage::CopySrc,
            next->capacity * sizeof(ParticleInstance), true, "GPU particle instances");
        auto indirect = Buffer::try_create_gpu_buffer(m_device,
            Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Indirect | BufferUsage::CopySrc,
            sizeof(vk::DrawIndirectCommand) + (dynamic ? next->capacity * 48 : 0), true,
            "GPU particle draw/state");
        if(!instances)
            return Allocated::failure(
                {"Cannot allocate GPU particle instances", instances.result()});
        if(!indirect)
            return Allocated::failure({"Cannot allocate GPU particle draw", indirect.result()});
        next->instances = std::move(instances).value();
        next->indirect = std::move(indirect).value();
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::StorageBuffer, 4);
        if(dynamic)
            sizes.add_pool_size(DescriptorType::SampledImage, 2);
        auto pool = DescriptorPool::create(m_device, 1, sizes);
        if(!pool)
            return Allocated::failure(pool.error());
        next->pool = std::move(pool).value();
        auto descriptors = next->pool->allocate_descriptor_set(*next->program->descriptors, 1);
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
        const ViewProjectMatrix& camera, std::span<const GpuParticleEmitter> emitters, bool sorted,
        const std::shared_ptr<ImageView>& depth, uint64_t scene_lifetime,
        std::span<const ParticleWorldCollider> colliders,
        std::span<const std::shared_ptr<const VectorField>> fields) {
        using Prepared = Result<void, GraphicsError>;
        if(!fields.empty() && fields.size() != emitters.size())
            return Prepared::failure({"Vector field bindings do not match GPU emitters"});
        if(colliders.size() > ParticleWorldCollider::MAX_COLLIDERS)
            return Prepared::failure({"Too many GPU world colliders"});
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frames.size())
            return Prepared::failure({"Invalid GPU particle frame"});
        if(m_dynamics->lifetime != scene_lifetime) {
            reset();
            m_dynamics->lifetime = scene_lifetime;
        }
        m_outputs.clear();
        const auto serial = frames.get_current_frame_serial();
        if(emitters.empty()) {
            reset();
            return Prepared::success();
        }
        auto& cache = m_frames.at(frames.get_current_frame_slot_index());
        cache.resize(emitters.size());
        const auto command = frames.get_current_command_buffer().get();
        uint32_t total = 0;
        auto& births = m_dynamics->births;
        for(size_t i = 0; i < emitters.size(); ++i) {
            const auto& emitter = emitters[i];
            if(!emitter.settings.validate()
                || emitter.settings.simulation != ParticleSimulation::GPU || !emitter.count
                || emitter.count > emitter.settings.capacity
                || emitter.count > ParticleSnapshot::MAX_GPU_PARTICLES - total
                || emitter.spawns.empty())
                return Prepared::failure({"Invalid GPU particle snapshot"});
            const auto field = fields.empty() ? nullptr : fields[i];
            if(emitter.settings.vector_field && (!field || !field->texture))
                return Prepared::failure({"GPU particle vector field is unresolved"});
            const bool dynamic = emitter.settings.gpu_dynamics;
            if(dynamic && !depth)
                return Prepared::failure({"GPU dynamics requires view depth"});
            const bool needs_sort = sorted && emitter.settings.blend == ParticleBlend::Alpha;
            if(auto result = initialize(dynamic, needs_sort); !result)
                return result;
            const auto& program = dynamic ? m_dynamic_program : m_program;
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
                        {offset, spawn.count, uint32_t(spawn.first), uint32_t(spawn.first >> 32)}});
                offset += spawn.count;
            }
            if(offset != emitter.count)
                return Prepared::failure({"GPU particle births do not cover the emitter"});
            auto& resources = cache[i];
            if(!resources || resources->program != program
                || resources->capacity < std::bit_ceil(emitter.count)
                || resources->spawns->get_size()
                       < births.size() * sizeof(Spawn)
                             + (dynamic ? sizeof(DynamicParameters) : 0)) {
                auto next = allocate(emitter.count, births.size(), dynamic);
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
            if(dynamic) {
                const uint64_t id = emitter.id ? emitter.id : i + 1;
                auto found = m_dynamics->history.find(id);
                if(found != m_dynamics->history.end()) {
                    auto previous_settings = found->second.settings;
                    previous_settings.enabled = settings.enabled;
                    if(found->second.serial + 1 != serial || previous_settings != settings
                        || found->second.time > emitter.time || found->second.field != field)
                        found = m_dynamics->history.end();
                }
                auto previous = resources->indirect;
                uint32_t previous_count = 0;
                double elapsed = 0;
                if(found != m_dynamics->history.end()) {
                    previous = found->second.state;
                    previous_count = found->second.count;
                    elapsed = emitter.time - found->second.time;
                    if(previous == resources->indirect) {
                        if(!resources->alternate) {
                            auto buffer = Buffer::try_create_gpu_buffer(m_device,
                                Flags<BufferUsage>(BufferUsage::Storage) | BufferUsage::Indirect
                                    | BufferUsage::CopySrc,
                                resources->indirect->get_size(), true,
                                "GPU particle alternate state");
                            if(!buffer)
                                return Prepared::failure(
                                    {"Cannot allocate alternate particle state", buffer.result()});
                            resources->alternate = std::move(buffer).value();
                        }
                        std::swap(resources->indirect, resources->alternate);
                    }
                }
                resources->previous = previous;
                resources->depth = depth;
                resources->field = field;
                const auto vp = camera.projection * camera.view;
                DynamicParameters extra{parameters, vp, Math::inverse(vp),
                    {float(emitter.time), float(elapsed), settings.collision_radius,
                        settings.restitution},
                    {settings.turbulence, settings.turbulence_frequency, settings.friction, 0},
                    {previous_count, static_cast<uint32_t>(settings.collision), 0, 0}};
                extra.wind = Math::Vec4(settings.wind, settings.vortex_strength);
                extra.vortex_center = Math::Vec4(settings.vortex_center, settings.vortex_radius);
                extra.vortex_axis = Math::Vec4(glm::normalize(settings.vortex_axis), 0);
                if(field) {
                    extra.field_minimum =
                        Math::Vec4(field->data.bounds.minimum, settings.vector_field_strength);
                    extra.field_size = Math::Vec4(field->data.bounds.size(), 1);
                    extra.field_dimensions = Math::Vec4(Math::Vec3(field->data.dimensions), 0);
                }
                extra.world = {uint32_t(colliders.size()), settings.collision_layers,
                    settings.gpu_world_collision ? 1u : 0u, 0};
                std::ranges::copy(colliders, extra.colliders.begin());
                resources->spawns->write(&extra, sizeof(extra));
                resources->spawns->write(
                    births.data(), births.size() * sizeof(Spawn), sizeof(extra));
                const std::array<std::shared_ptr<Buffer>, 4> buffers{
                    resources->spawns, previous, resources->instances, resources->indirect};
                std::array<vk::DescriptorBufferInfo, 4> infos;
                std::array<vk::WriteDescriptorSet, 6> writes;
                for(uint32_t binding = 0; binding < buffers.size(); ++binding) {
                    infos[binding] = vk::DescriptorBufferInfo(
                        buffers[binding]->get(), 0, buffers[binding]->get_size());
                    writes[binding] =
                        vk::WriteDescriptorSet(resources->descriptors.front().get(), binding, 0, 1,
                            vk::DescriptorType::eStorageBuffer, nullptr, &infos[binding]);
                }
                const vk::DescriptorImageInfo image(
                    {}, depth->get(), vk::ImageLayout::eShaderReadOnlyOptimal);
                writes[4] = vk::WriteDescriptorSet(resources->descriptors.front().get(), 4, 0, 1,
                    vk::DescriptorType::eSampledImage, &image);
                const vk::DescriptorImageInfo field_image({},
                    field ? field->texture->get_image_view()->get() : depth->get(),
                    vk::ImageLayout::eShaderReadOnlyOptimal);
                writes[5] = vk::WriteDescriptorSet(resources->descriptors.front().get(), 5, 0, 1,
                    vk::DescriptorType::eSampledImage, &field_image);
                m_device.get().updateDescriptorSets(writes, {});
                m_dynamics->history.insert_or_assign(
                    id, Dynamics::History{resources->indirect, settings, field, emitter.time,
                            serial, emitter.count});
            } else {
                resources->spawns->write(births.data(), births.size() * sizeof(Spawn));
                resources->parameters->write(&parameters);
            }
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
            command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, program->layout.get(), 0,
                resources->descriptors.front().get(), {});
            if(dynamic) {
                command.bindPipeline(vk::PipelineBindPoint::eCompute, program->initialize.get());
                command.dispatch((padded + 63) / 64, 1, 1);
                const vk::MemoryBarrier initialized(vk::AccessFlagBits::eShaderWrite,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eComputeShader, {}, initialized, {}, {});
            }
            command.bindPipeline(vk::PipelineBindPoint::eCompute, program->simulate.get());
            command.dispatch((padded + 63) / 64, 1, 1);
            if(needs_sort) {
                command.bindPipeline(vk::PipelineBindPoint::eCompute, program->sort.get());
                const vk::MemoryBarrier between(vk::AccessFlagBits::eShaderWrite,
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                for(uint32_t stage = 2; stage <= padded; stage *= 2)
                    for(uint32_t stride = stage / 2; stride; stride /= 2) {
                        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eComputeShader, {}, between, {}, {});
                        const std::array constants{stage, stride};
                        command.pushConstants(program->layout.get(),
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
        std::erase_if(m_dynamics->history,
            [serial](const auto& entry) { return entry.second.serial != serial; });
        return Prepared::success();
    }
}
