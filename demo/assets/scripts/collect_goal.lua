local demo_score = require("scripts.demo_score")
local script = {}

script.properties = {
    player = {type = "entity"},
}

function script:on_start()
    self.collected = not comet.has_rigid_body(comet.self_entity())
    self.collect_time = 0
end

function script:on_trigger_enter(other)
    if self.collected or not self.parameters.player:is_valid()
        or other ~= self.parameters.player then
        return
    end

    self.collected = true
    comet.remove_rigid_body(comet.self_entity())
    local score = demo_score.add(1)
    comet.set_input_context("gameplay", false)
    comet.play_one_shot()
    local x, y, z = comet.position()
    comet.create_entity("Collected_Goal_" .. score, {
        mesh_source = comet.self_entity(),
        translation = {x, y + 0.8, z},
        scale = {0.15, 0.15, 0.15},
    })
    comet.log("Goal collected; score=" .. score)
end

function script:update(dt)
    if not self.collected then
        return
    end

    self.collect_time = self.collect_time + dt
    comet.translate(0, 1.5 * dt, 0)
    comet.rotate(0, 360 * dt, 0)
    if self.collect_time >= 0.5 then
        comet.destroy_entity(comet.self_entity())
    end
end

return script
