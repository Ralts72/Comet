local script = {}

script.properties = {
    amplitude = 1.5,
    speed = 1,
}

function script:on_start()
    self.base_x = comet.position()
    self.time = 0
end

function script:fixed_update(dt)
    self.time = self.time + dt
    local x = comet.position()
    local target = self.base_x + math.sin(self.time * self.parameters.speed)
        * self.parameters.amplitude
    comet.translate(target - x, 0, 0)
end

return script
