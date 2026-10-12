local script = {}

script.properties = {
    impulse = 3.5,
    impulse_offset = 0,
    thrust_force = 15,
    spin_torque = 0.5,
    max_rise_speed = 4,
    max_spin_speed = 3,
}

function script:fixed_update()
    if comet.action_pressed("demo.impulse") then
        local x, y, z = comet.position()
        comet.apply_impulse_at(0, self.parameters.impulse, 0,
            x + self.parameters.impulse_offset, y, z)
    end
    if not comet.action_down("demo.thrust") then
        return
    end

    local _, rise_speed = comet.linear_velocity()
    local _, spin_speed = comet.angular_velocity()
    if rise_speed < self.parameters.max_rise_speed then
        comet.apply_force(0, self.parameters.thrust_force, 0)
    end
    if spin_speed < self.parameters.max_spin_speed then
        comet.apply_torque(0, self.parameters.spin_torque, 0)
    end
end

return script
