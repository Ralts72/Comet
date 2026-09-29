local script = {}

script.properties = {
    speed = 100,
    enabled = true,
    score_color = {type = "color", default = {0.2, 1, 0.25, 1}},
}

script.events = {
    ["demo.score_changed"] = "on_score_changed",
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
    if comet.action_pressed("demo.restart") then
        comet.restart_scene()
    end
end

function script:on_score_changed(score)
    if score > self.last_score then
        comet.translate(0, 0.4 * (score - self.last_score), 0)
        local color = self.parameters.score_color
        comet.set_material_vector("base_color", color[1], color[2], color[3], color[4])
        self.last_score = score
    end
end

return script
