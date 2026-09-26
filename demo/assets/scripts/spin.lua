local script = {}

script.properties = {
    speed = 100,
    enabled = true,
}

function script:on_start()
    self.last_score = comet.session_get("demo.score") or 0
end

function script:fixed_update(dt)
    if comet.action_pressed("spin.toggle") then
        self.paused = not self.paused
    end
    if self.parameters.enabled and not self.paused then
        comet.rotate(0, self.parameters.speed * dt, 0)
    end
end

function script:update()
    local score = comet.session_get("demo.score") or 0
    if score > self.last_score then
        comet.translate(0, 0.4 * (score - self.last_score), 0)
        self.last_score = score
    end
end

return script
