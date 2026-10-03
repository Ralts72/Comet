local demo_score = require("scripts.demo_score")
local script = {}
local palette = {
    {0.2, 0.55, 1, 1},
    {1, 0.5, 0.1, 1},
    {0.75, 0.2, 1, 1},
}

script.properties = {
    speed = 100,
    enabled = true,
    score_color = {type = "color", default = {0.2, 1, 0.25, 1}},
}

script.events = {
    [demo_score.changed_event] = "on_score_changed",
}

function script:on_start()
    self.last_score = demo_score.get()
    self.palette_active = false
    self.palette_index = 1
    comet.set_input_context("palette", false)
end

function script:apply_palette()
    local color = palette[self.palette_index]
    comet.set_material_vector("base_color", color[1], color[2], color[3], color[4])
end

function script:set_palette_active(active)
    self.palette_active = active
    comet.set_input_context("palette", active)
    if active then
        self:apply_palette()
    end
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
        return
    end
    if comet.action_pressed("palette.toggle") then
        self:set_palette_active(not self.palette_active)
        return
    end
    if not self.palette_active then
        return
    end
    if comet.action_pressed("palette.confirm") then
        self:set_palette_active(false)
    elseif comet.action_pressed("palette.reset") then
        self.palette_index = 1
        self:apply_palette()
    elseif comet.action_pressed("palette.next") then
        self.palette_index = self.palette_index % #palette + 1
        self:apply_palette()
    elseif comet.action_pressed("palette.previous") then
        self.palette_index = (self.palette_index - 2) % #palette + 1
        self:apply_palette()
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
