local script = {}

script.properties = {
    speed = 100,
    enabled = true,
}

function script:fixed_update(dt)
    if comet.action_pressed("spin.toggle") then
        self.paused = not self.paused
    end
    if self.parameters.enabled and not self.paused then
        comet.rotate(0, self.parameters.speed * dt, 0)
    end
end

return script
