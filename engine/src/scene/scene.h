#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "common/export.h"
#include "common/result.h"
#include "scene/entity.h"
#include "scene/property.h"
#include "scene/scene_settings.h"
#include "scene/transform_interpolation.h"
#include <entt.hpp>

namespace Comet {
    class SceneSerializer;
    class Prefab;
    class ComponentRegistry;
    class SceneRuntime;
    class PhysicsSystem;
    class ScriptSystem;
    class ParticleSystem;
    class MaterialParameterValidator;
    struct MaterialOverrides;
    struct ModelData;
    struct AnimationData;
    struct AnimationPose;

    struct AnimationPlayback;

    class COMET_API Scene {
    public:
        struct EntityCreation {
            TransformComponent transform;
            std::optional<MeshRendererComponent> mesh_renderer;
        };
        struct ContactEvent {
            enum class Kind { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit } kind;
            Entity first;
            Entity second;
        };
        struct Event {
            std::string name;
            std::optional<ParameterValue> value;
            EntityUuid target;
        };

        Scene();

        ~Scene() = default;

        Scene(const Scene&) = delete;

        Scene& operator=(const Scene&) = delete;

        Scene(Scene&&) noexcept = delete;

        Scene& operator=(Scene&&) noexcept = delete;

        Entity create_entity(const std::string& name = "Entity");

        [[nodiscard]] Entity create_entity_with_uuid(
            EntityUuid uuid, const std::string& name = "Entity");

        void destroy_entity(Entity entity);
        [[nodiscard]] Result<Entity> instantiate_model(const ModelData& model,
            const std::string& name = "Model", Math::Vec3 position = {},
            std::optional<uint32_t> model_scene = std::nullopt);

        // 仅活动 Runtime 可请求；结构变更在当前启动／更新阶段结束后统一提交。
        [[nodiscard]] std::optional<EntityUuid> request_create_entity(
            std::string_view name = "Entity", const EntityCreation& creation = {});
        [[nodiscard]] std::optional<EntityUuid> request_instantiate_prefab(
            std::shared_ptr<const Prefab> prefab,
            std::shared_ptr<const ComponentRegistry> components,
            std::optional<Math::Vec3> position = std::nullopt);
        [[nodiscard]] bool request_destroy_entity(Entity entity);
        // 仅移除刚体，保留碰撞体配置和其他组件；无刚体或重复请求幂等成功。
        [[nodiscard]] bool request_remove_rigid_body(Entity entity);
        [[nodiscard]] bool request_particle_burst(Entity entity, uint32_t count);
        [[nodiscard]] bool request_particle_clear(Entity entity);
        [[nodiscard]] const std::shared_ptr<const ParticleSnapshot>& get_particles() const {
            return m_particles;
        }
        // Main 发布 Runtime／编辑预览的瞬时数据，不进入场景保存或克隆。
        void set_particles(std::shared_ptr<const ParticleSnapshot> particles);

        // 场景通知不序列化；交付期间发出的新通知留至下一次有效更新。
        [[nodiscard]] bool emit_event(std::string_view name,
            std::optional<ParameterValue> value = std::nullopt, Entity target = {});

        // 仅当前 Runtime 的实体材质覆盖；快照不修改共享资产，不序列化。
        [[nodiscard]] Result<void> set_material_scalar(Entity entity, std::string_view name,
            float value, const MaterialParameterValidator& materials);
        [[nodiscard]] Result<void> set_material_vector(Entity entity, std::string_view name,
            Math::Vec4 value, const MaterialParameterValidator& materials);
        [[nodiscard]] std::shared_ptr<const MaterialOverrides> get_material_overrides(
            Entity entity);

        [[nodiscard]] bool set_parent(Entity child, Entity parent);

        [[nodiscard]] bool clear_parent(Entity child);

        [[nodiscard]] Entity get_parent(Entity entity);

        [[nodiscard]] std::vector<Entity> get_children(Entity entity);

        [[nodiscard]] std::vector<Entity> get_root_entities();

        // 同步脏节点；之后可直接读取只读 WorldTransformComponent，返回重算节点数。
        std::size_t update_world_transforms();

        // 即时查询：仅同步该实体的脏祖先链，不刷新无关分支。
        [[nodiscard]] const Math::Mat4& get_world_matrix(Entity entity);
        // 本地变换所依赖的世界基底，包含可选骨骼挂点；供编辑反算本地偏移。
        [[nodiscard]] Math::Mat4 get_parent_matrix(Entity entity);
        // 只读呈现姿态；脚本、物理和保存继续使用真实 Transform／WorldTransform。
        [[nodiscard]] const WorldTransformComponent& get_render_transform(Entity entity);

        [[nodiscard]] Entity find_entity(EntityId id);

        [[nodiscard]] Entity find_entity(EntityUuid uuid);

        [[nodiscard]] std::vector<Entity> get_entities();

        // 只遍历匹配组件的实体，不保证顺序；回调内不增删实体或组件。
        template<typename... Components, typename Function> void each(Function&& function) {
            auto view = m_registry.view<QueryComponent<Components>...>();
            view.each([&](const entt::entity handle, auto&... components) {
                function(Entity(handle, this), components...);
            });
        }

        template<typename Component> [[nodiscard]] std::size_t component_count() const {
            const auto* storage = m_registry.storage<std::remove_const_t<Component>>();
            return storage ? storage->size() : 0;
        }

        [[nodiscard]] std::shared_ptr<const AnimationPlayback> get_animation_playback(
            Entity entity) const;
        // 呈现快照共用插值后的骨骼；玩法继续读取上面的真实姿态。
        [[nodiscard]] std::shared_ptr<const AnimationPlayback> get_render_animation_playback(
            Entity entity);
        // Main 发布瞬时姿态输入；不进入内容序列化，Runtime 开始／停止时清理。
        void set_animation_playback(
            Entity entity, std::shared_ptr<const AnimationPlayback> playback);
        // 宿主解析 CPU 动画数据；Scene 缓存挂点和固定步呈现所需的姿态。
        void set_animation_source(Entity entity, std::shared_ptr<const AnimationData> source);
        struct AnimationStatistics {
            uint64_t pose_resolutions = 0;
            uint64_t ik_pose_reuses = 0;
            uint64_t presentation_interpolations = 0;
        };
        // 场景生命期内的 Main 姿态求值次数；不影响播放与呈现时钟。
        [[nodiscard]] AnimationStatistics get_animation_statistics() const {
            return m_animation_statistics;
        }

        [[nodiscard]] bool is_valid(Entity entity) const;

        [[nodiscard]] std::size_t entity_count() const;

        // 仅用于进程内区分场景实例，不写入场景文件。
        [[nodiscard]] uint64_t get_lifetime() const noexcept { return m_lifetime; }
        [[nodiscard]] uint64_t get_render_revision() const noexcept { return m_render_revision; }

        // 当前固定步的通知，仅在 post_fixed_update 阶段借用；阶段结束即清空。
        [[nodiscard]] const std::vector<ContactEvent>& get_contact_events() const {
            return m_contact_events;
        }

        [[nodiscard]] const SceneEnvironment& get_environment() const { return m_environment; }
        [[nodiscard]] bool set_environment(const SceneEnvironment& environment);
        [[nodiscard]] const PostProcessSettings& get_post_process() const { return m_post_process; }
        [[nodiscard]] bool set_post_process(const PostProcessSettings& settings);

    private:
        friend class Prefab;
        friend class Entity;
        friend class SceneSerializer;
        friend class SceneRuntime;
        friend class PhysicsSystem;
        friend class ScriptSystem;
        friend class ParticleSystem;
        friend class ComponentRegistry;
        friend class SceneExtractor;
        friend class TransformInterpolation;

        [[nodiscard]] Result<Entity> instantiate_model_content(const ModelData& model,
            const std::string& name, Math::Vec3 position, std::optional<uint32_t> model_scene,
            AssetHandle identity_source);

        static constexpr std::size_t MAX_ENTITY_REQUESTS = 1024;
        static constexpr std::size_t MAX_EVENTS = 1024;

        struct EntityRequest {
            enum class Type { Create, InstantiatePrefab, Destroy, RemoveRigidBody } type;
            EntityUuid uuid;
            EntityId id = INVALID_ENTITY_ID;
            std::string name;
            EntityCreation creation;
            std::shared_ptr<const Prefab> prefab;
            std::shared_ptr<const ComponentRegistry> components;
            std::optional<Math::Vec3> position;
        };

        [[nodiscard]] bool can_queue_entity_creation(std::size_t count) const;
        [[nodiscard]] EntityUuid reserve_entity_uuid() const;
        [[nodiscard]] bool begin_runtime();
        [[nodiscard]] bool commit_entity_requests();
        [[nodiscard]] std::vector<Event> take_events();
        void end_runtime() noexcept;
        void clear_runtime_state() noexcept;
        [[nodiscard]] bool append_contact_event(ContactEvent event);
        void clear_contact_events() noexcept;
        void clear_material_overrides(entt::registry& registry, entt::entity entity);
        [[nodiscard]] Result<void> set_material_parameter(Entity entity, std::string_view name,
            std::variant<float, Math::Vec4> value, const MaterialParameterValidator& materials);

        template<typename Component>
        using QueryComponent = std::conditional_t<is_scene_read_only_component_v<Component>,
            const Component, Component>;

        [[nodiscard]] bool has_cycle(Entity child, Entity parent);
        void remove_child_index(EntityId parent, entt::entity child);
        void mark_transform_dirty(entt::entity handle);
        void reset_transform_interpolation(entt::entity handle);
        void mark_physics_dirty(const entt::entity handle) {
            if(m_runtime_active
                && m_registry.any_of<RigidBodyComponent, CharacterControllerComponent>(handle)
                && !m_dirty_physics_entities.contains(handle))
                m_dirty_physics_entities.push(handle);
        }
        void update_world_transform(entt::entity handle);
        void mark_attachments_dirty(entt::entity parent);
        void apply_parent_transform(entt::entity child, const WorldTransformComponent& parent,
            Math::Mat4& model, Math::Mat4& pose, bool presented = false);
        std::size_t sync_transform_chain(entt::entity handle);
        void mark_render_dirty() { ++m_render_revision; }
        void mark_render_entity_changed(entt::entity handle);
        void record_render_change(entt::entity handle);

        template<typename Component>
        void component_changed(entt::entity handle, bool structure_changed) {
            if constexpr(std::is_same_v<Component, TransformComponent>
                         || std::is_same_v<Component, BoneAttachmentComponent>)
                mark_transform_dirty(handle);
            else if constexpr(is_scene_render_component_v<Component>)
                mark_render_entity_changed(handle);

            if constexpr(std::is_same_v<Component, TransformComponent>
                         || is_scene_physics_component_v<Component>)
                mark_physics_dirty(handle);

            if constexpr(is_scene_joint_component_v<Component>)
                m_physics_joints_changed = true;

            if constexpr(std::is_same_v<Component, ParticleEmitterComponent>
                         || std::is_same_v<Component, TrailComponent>)
                ++m_particle_revision;

            if(structure_changed) {
                if constexpr(std::is_same_v<Component, TransformComponent>)
                    reset_transform_interpolation(handle);
                if constexpr(std::is_same_v<Component, TransformComponent>
                             || std::is_same_v<Component, MeshRendererComponent>)
                    m_render_structure_changed = true;
                if constexpr(std::is_same_v<Component, RigidBodyComponent>
                             || std::is_same_v<Component, CharacterControllerComponent>)
                    m_physics_structure_changed = true;
            }
        }

        struct RenderChange {
            entt::entity handle;
            EntityId id;
        };

        struct RenderChanges {
            uint64_t revision;
            uint64_t base_revision;
            bool full_update;
            bool structure_changed;
            std::span<const RenderChange> entities;
        };
        [[nodiscard]] RenderChanges render_changes() const;
        void acknowledge_render_changes(uint64_t revision, std::size_t next_limit);
        struct PhysicsChanges {
            bool structure_changed;
            bool joints_changed;
            std::span<const entt::entity> entities;
        };
        [[nodiscard]] PhysicsChanges physics_changes() const;
        void acknowledge_physics_changes();

        const uint64_t m_lifetime;
        struct ParticleRequest {
            EntityId entity;
            ParticleEmitterComponent settings;
            Math::Mat4 pose;
            uint32_t count;
            bool clear = false;
        };
        std::vector<ParticleRequest> m_particle_requests;
        std::shared_ptr<const ParticleSnapshot> m_particles;
        uint64_t m_particle_revision = 0;
        uint64_t m_render_revision = 1;
        uint64_t m_transform_revision = 0;
        TransformInterpolation m_transform_interpolation;
        uint64_t m_render_changes_base = 0;
        std::size_t m_render_change_limit = 0;
        bool m_render_full_update = true;
        bool m_render_structure_changed = false;
        std::vector<RenderChange> m_render_changes;
        entt::sparse_set m_dirty_render_entities;
        EntityId m_next_entity_id = 1;
        std::vector<EntityRequest> m_entity_requests;
        std::vector<ContactEvent> m_contact_events;
        std::vector<Event> m_events;
        bool m_runtime_active = false;
        bool m_physics_structure_changed = false;
        bool m_physics_joints_changed = false;
        entt::sparse_set m_dirty_physics_entities;
        SceneEnvironment m_environment;
        PostProcessSettings m_post_process;
        std::unordered_map<entt::entity, std::shared_ptr<const MaterialOverrides>>
            m_material_overrides;
        std::unordered_map<entt::entity, std::shared_ptr<const AnimationPlayback>>
            m_animation_playbacks;
        struct AnimationSource {
            std::shared_ptr<const AnimationData> data;
            std::shared_ptr<const AnimationPose> bind_pose;
            std::shared_ptr<const AnimationPose> previous_pose;
            std::shared_ptr<const AnimationPlayback> presented;
        };
        std::unordered_map<entt::entity, AnimationSource> m_animation_sources;
        AnimationStatistics m_animation_statistics;
        entt::registry m_registry;
        std::unordered_map<EntityId, entt::entity> m_entities_by_id;
        std::unordered_map<EntityUuid, entt::entity> m_entities_by_uuid;
        std::unordered_map<EntityId, std::vector<entt::entity>> m_children_by_parent;
        entt::sparse_set m_dirty_transforms;
        std::vector<entt::entity> m_transform_work;
    };

    template<typename T, typename... Args>
        requires(!is_scene_managed_component_v<T>)
    decltype(auto) Entity::add_component(Args&&... args) {
        auto& component = m_scene->m_registry.emplace<T>(m_handle, std::forward<Args>(args)...);
        m_scene->component_changed<T>(m_handle, true);
        if constexpr(is_scene_read_only_component_v<T>)
            return std::as_const(component);
        else
            return component;
    }

    template<typename T>
        requires(is_scene_tracked_component_v<T>)
    bool Entity::try_set_component(const T& component) const {
        if(!has_component<T>())
            return false;
        auto& current = m_scene->m_registry.get<T>(m_handle);
        if(current == component)
            return true;
        if constexpr(std::is_same_v<T, MeshRendererComponent>) {
            if(current.material != component.material)
                m_scene->clear_material_overrides(m_scene->m_registry, m_handle);
        }
        current = component;
        m_scene->component_changed<T>(m_handle, false);
        return true;
    }

    template<typename T, typename Function>
        requires(is_scene_tracked_component_v<T>)
    void Entity::edit_component(Function&& edit) const {
        require_component_write(has_component<T>());
        auto candidate = get_component<T>();
        std::forward<Function>(edit)(candidate);
        require_component_write(try_set_component(candidate));
    }

    template<typename T>
        requires(!is_scene_read_only_component_v<T>)
    T& Entity::get_component() {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename Function> bool Entity::try_edit_transform(Function&& edit) const {
        if(!has_component<TransformComponent>())
            return false;
        auto candidate = get_component<TransformComponent>();
        std::forward<Function>(edit)(candidate);
        return try_set_transform(candidate);
    }

    template<typename Function> void Entity::edit_transform(Function&& edit) const {
        require_transform_write(try_edit_transform(std::forward<Function>(edit)));
    }

    template<typename T> const T& Entity::get_component() const {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T> bool Entity::has_component() const {
        return static_cast<bool>(*this) && m_scene->m_registry.all_of<T>(m_handle);
    }

    template<typename T>
        requires(!is_scene_managed_component_v<T>)
    void Entity::remove_component() const {
        if(has_component<T>())
            m_scene->component_changed<T>(m_handle, true);
        m_scene->m_registry.remove<T>(m_handle);
    }
}
