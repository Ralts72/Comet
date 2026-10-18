#include "scene/component_registration.h"
#include "scene/component_changes.h"

namespace Comet {
    namespace {
        template<typename Joint> std::vector<PropertyDescriptor> joint_properties() {
            auto properties = std::vector<PropertyDescriptor>{
                make_property_descriptor<&Joint::enabled>("enabled", "Enabled"),
                make_property_descriptor<&Joint::connected_body>(
                    "connected_body", "Connected Body"),
                make_property_descriptor<&Joint::anchor>("anchor", "Local Anchor"),
                make_property_descriptor<&Joint::connected_anchor>(
                    "connected_anchor", "Connected Anchor"),
                make_property_descriptor<&Joint::break_force>("break_force", "Break Force (N)",
                    {.required = false,
                        .numeric = {.speed = 1, .minimum = 0, .enforce_bounds = true}})};
            if constexpr(!std::is_same_v<Joint, PointJointComponent>)
                properties.push_back(make_property_descriptor<&Joint::break_torque>("break_torque",
                    "Break Torque (N.m)",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));
            return properties;
        }

        template<typename Joint> std::vector<PropertyDescriptor> axis_joint_properties() {
            auto properties = joint_properties<Joint>();
            properties.push_back(make_property_descriptor<&Joint::axis>("axis", "Local Axis"));
            properties.push_back(make_property_descriptor<&Joint::connected_axis>(
                "connected_axis", "Connected Axis"));
            return properties;
        }

        template<typename Joint>
        void append_joint_controls(std::vector<PropertyDescriptor>& properties,
            PropertyDescriptor target, PropertyDescriptor speed, PropertyDescriptor capacity) {
            properties.insert(properties.end(),
                {make_property_descriptor<&Joint::limits_frequency>("limits_frequency",
                     "Limit Frequency (Hz)",
                     {.required = false,
                         .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor<&Joint::limits_damping>("limits_damping",
                        "Limit Damping Ratio",
                        {.required = false,
                            .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}}),
                    make_property_descriptor<&Joint::motor_enabled>(
                        "motor_enabled", "Motor Enabled", {.required = false}),
                    make_enum_property_descriptor<&Joint::motor_mode>("motor_mode", "Motor Mode",
                        {{JointMotorMode::Velocity, {"velocity", "Velocity"}},
                            {JointMotorMode::Position, {"position", "Position"}}},
                        {.required = false}),
                    std::move(target), std::move(speed), std::move(capacity),
                    make_property_descriptor<&Joint::motor_frequency>("motor_frequency",
                        "Motor Frequency (Hz)",
                        {.required = false,
                            .numeric = {.speed = 0.1f, .minimum = 0.001f, .enforce_bounds = true}}),
                    make_property_descriptor<&Joint::motor_damping>("motor_damping",
                        "Motor Damping Ratio",
                        {.required = false,
                            .numeric = {.speed = 0.05f, .minimum = 0, .enforce_bounds = true}})});
        }
    }

    void ComponentRegistration::physics(ComponentRegistry& registry) {
        add(registry, describe_component<RigidBodyComponent>("rigid_body", "Rigid Body")
                          .enum_field(COMET_FIELD(RigidBodyComponent, motion), "Motion",
                              {{BodyMotion::Static, {"static", "Static"}},
                                  {BodyMotion::Dynamic, {"dynamic", "Dynamic"}},
                                  {BodyMotion::Kinematic, {"kinematic", "Kinematic"}}})
                          .enum_field(COMET_FIELD(RigidBodyComponent, mass_mode), "Mass Mode",
                              {{BodyMassMode::Mass, {"mass", "Mass"}},
                                  {BodyMassMode::Density, {"density", "Density"}}},
                              {.required = false})
                          .field(COMET_FIELD(RigidBodyComponent, mass), "Mass (kg)",
                              {.required = false,
                                  .numeric = {.speed = 0.1f,
                                      .minimum = RigidBodyComponent::MIN_MASS,
                                      .enforce_bounds = true}})
                          .field(COMET_FIELD(RigidBodyComponent, density), "Density (kg/m³)",
                              {.required = false,
                                  .numeric = {.speed = 1.0f,
                                      .minimum = RigidBodyComponent::MIN_DENSITY,
                                      .enforce_bounds = true}})
                          .field(COMET_FIELD(RigidBodyComponent, continuous_collision),
                              "Continuous Collision (CCD)", {.required = false})
                          .build());

        add(registry,
            describe_component<CharacterControllerComponent>(
                "character_controller", "Character Controller")
                .field(COMET_FIELD(CharacterControllerComponent, enabled), "Enabled")
                .field(COMET_FIELD(CharacterControllerComponent, radius), "Capsule Radius",
                    {.numeric = {.speed = 0.01f, .minimum = 0.01f, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, height), "Capsule Height",
                    {.numeric = {.speed = 0.05f, .minimum = 0.03f, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, move_speed), "Move Speed",
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, run_speed), "Run Speed",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, crouch_speed), "Crouch Speed",
                    {.required = false,
                        .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, crouch_height_ratio),
                    "Crouch Height Ratio",
                    {.required = false,
                        .numeric = {.speed = 0.01f,
                            .minimum = 0.1f,
                            .maximum = 1,
                            .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, air_control), "Air Control",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, push_force), "Push Force (N)",
                    {.required = false,
                        .numeric = {.speed = 1, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, jump_speed), "Jump Speed",
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, max_slope_angle),
                    "Maximum Slope (degrees)",
                    {.numeric = {.speed = 1, .minimum = 1, .maximum = 89, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, step_height), "Step Height",
                    {.numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, ground_snap_distance),
                    "Ground Snap Distance",
                    {.numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}})
                .field(COMET_FIELD(CharacterControllerComponent, collision_layer),
                    "Collision Layer", {.required = false, .bitmask = true})
                .field(COMET_FIELD(CharacterControllerComponent, collision_mask), "Collision Mask",
                    {.required = false, .bitmask = true})
                .field(COMET_FIELD(CharacterControllerComponent, gravity_scale), "Gravity Scale",
                    {.numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}})
                .build());

        add(registry,
            describe_component<ColliderComponent>("collider", "Collider")
                .enum_field(COMET_FIELD(ColliderComponent, shape), "Shape",
                    {{ColliderShape::Box, {"box", "Box"}},
                        {ColliderShape::Sphere, {"sphere", "Sphere"}},
                        {ColliderShape::Capsule, {"capsule", "Capsule"}},
                        {ColliderShape::ConvexHull, {"convex_hull", "Convex Hull"}},
                        {ColliderShape::TriangleMesh, {"triangle_mesh", "Static Triangle Mesh"}}})
                .field(COMET_FIELD(ColliderComponent, mesh), "Collision Mesh",
                    {.required = false, .asset_type = AssetType::Mesh, .asset_cpu_only = true})
                .field(COMET_FIELD(ColliderComponent, collision_layer), "Collision Layer",
                    {.required = false, .bitmask = true})
                .field(COMET_FIELD(ColliderComponent, collision_mask), "Collision Mask",
                    {.required = false, .bitmask = true})
                .field(COMET_FIELD(ColliderComponent, half_extents), "Box Half Extents",
                    {.numeric = {.speed = 0.05f, .minimum = 0.001f}})
                .field(COMET_FIELD(ColliderComponent, radius), "Radius",
                    {.numeric = {.speed = 0.05f, .minimum = 0.001f}})
                .field(COMET_FIELD(ColliderComponent, height), "Capsule Height",
                    {.required = false, .numeric = {.speed = 0.05f, .minimum = 0.001f}})
                .field(COMET_FIELD(ColliderComponent, is_trigger), "Trigger", {.required = false})
                .field(COMET_FIELD(ColliderComponent, friction), "Friction",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .field(COMET_FIELD(ColliderComponent, restitution), "Restitution",
                    {.required = false,
                        .numeric =
                            {.speed = 0.01f, .minimum = 0, .maximum = 1, .enforce_bounds = true}})
                .build());

        add(registry, describe_component<PointJointComponent>("point_joint", "Point Joint")
                          .fields(joint_properties<PointJointComponent>())
                          .build());

        auto hinge_properties = axis_joint_properties<HingeJointComponent>();

        hinge_properties.insert(hinge_properties.end(),
            {make_property_descriptor<&HingeJointComponent::limits_enabled>(
                 "limits_enabled", "Angle Limits", {.required = false}),
                make_property_descriptor<&HingeJointComponent::reference_axis>(
                    "reference_axis", "Local Reference", {.required = false}),
                make_property_descriptor<&HingeJointComponent::connected_reference_axis>(
                    "connected_reference_axis", "Connected Reference", {.required = false}),
                make_property_descriptor<&HingeJointComponent::min_angle>("min_angle",
                    "Minimum Angle",
                    {.required = false,
                        .numeric =
                            {.speed = 1, .minimum = -180, .maximum = 0, .enforce_bounds = true}}),
                make_property_descriptor<&HingeJointComponent::max_angle>("max_angle",
                    "Maximum Angle",
                    {.required = false,
                        .numeric = {
                            .speed = 1, .minimum = 0, .maximum = 180, .enforce_bounds = true}})});

        append_joint_controls<HingeJointComponent>(hinge_properties,
            make_property_descriptor<&HingeJointComponent::motor_target>("motor_target",
                "Target Angle",
                {.required = false,
                    .numeric =
                        {.speed = 1, .minimum = -180, .maximum = 180, .enforce_bounds = true}}),
            make_property_descriptor<&HingeJointComponent::motor_speed>(
                "motor_speed", "Target Speed", {.required = false, .numeric = {.speed = 1}}),
            make_property_descriptor<&HingeJointComponent::motor_max_torque>("motor_max_torque",
                "Maximum Torque",
                {.required = false,
                    .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));

        add(registry, describe_component<HingeJointComponent>("hinge_joint", "Hinge Joint")
                          .fields(std::move(hinge_properties))
                          .build());

        auto slider_properties = axis_joint_properties<SliderJointComponent>();

        slider_properties.insert(slider_properties.end(),
            {make_property_descriptor<&SliderJointComponent::reference_axis>(
                 "reference_axis", "Local Orientation", {.required = false}),
                make_property_descriptor<&SliderJointComponent::connected_reference_axis>(
                    "connected_reference_axis", "Connected Orientation", {.required = false}),
                make_property_descriptor<&SliderJointComponent::limits_enabled>(
                    "limits_enabled", "Travel Limits", {.required = false}),
                make_property_descriptor<&SliderJointComponent::min_distance>("min_distance",
                    "Minimum Distance",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .maximum = 0, .enforce_bounds = true}}),
                make_property_descriptor<&SliderJointComponent::max_distance>("max_distance",
                    "Maximum Distance",
                    {.required = false,
                        .numeric = {.speed = 0.01f, .minimum = 0, .enforce_bounds = true}})});

        append_joint_controls<SliderJointComponent>(slider_properties,
            make_property_descriptor<&SliderJointComponent::motor_target>("motor_target",
                "Target Distance", {.required = false, .numeric = {.speed = 0.01f}}),
            make_property_descriptor<&SliderJointComponent::motor_speed>(
                "motor_speed", "Target Speed", {.required = false, .numeric = {.speed = 0.01f}}),
            make_property_descriptor<&SliderJointComponent::motor_max_force>("motor_max_force",
                "Maximum Force",
                {.required = false,
                    .numeric = {.speed = 0.1f, .minimum = 0, .enforce_bounds = true}}));

        add(registry, describe_component<SliderJointComponent>("slider_joint", "Slider Joint")
                          .fields(std::move(slider_properties))
                          .build());
        registry.set_reference_filter("collider", [](const void* data, AssetReferenceScope scope) {
            const auto& value = *static_cast<const ColliderComponent*>(data);
            return scope == AssetReferenceScope::All || value.uses_mesh();
        });
    }
    void ComponentChangeAccess::physics(Scene& scene, Entity entity, bool) {
        scene.mark_physics_dirty(entity.m_handle);
        scene.mark_render_dirty(); // GPU 粒子也读取碰撞形状。
    }
    void ComponentChangeAccess::body(Scene& scene, Entity entity, bool structure) {
        physics(scene, entity, structure);
        if(structure)
            scene.m_physics_structure_changed = true;
    }
    void ComponentChangeAccess::joint(Scene& scene, Entity entity, bool structure) {
        physics(scene, entity, structure);
        scene.m_physics_joints_changed = true;
    }

}
