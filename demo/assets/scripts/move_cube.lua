local script = {}

script.properties = {
    speed = 1.5,
}

function script:fixed_update(dt)
    local direction = comet.action_value("demo.move_x")
    if direction ~= 0 then
        comet.translate(direction * self.parameters.speed * dt, 0, 0)
    end
end

return script
