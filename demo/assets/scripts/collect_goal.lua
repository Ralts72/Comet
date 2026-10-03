local demo_score = require("scripts.demo_score")
local script = {}

script.properties = {
    player = {type = "entity"},
}

function script:on_trigger_enter(other)
    if self.collected or not self.parameters.player:is_valid()
        or other ~= self.parameters.player then
        return
    end

    self.collected = true
    local score = demo_score.add(1)
    comet.set_input_context("gameplay", false)
    comet.play_one_shot()
    local x, y, z = comet.position()
    comet.create_entity("Collected_Goal_" .. score, {
        mesh_source = comet.self_entity(),
        translation = {x, y + 0.8, z},
        scale = {0.15, 0.15, 0.15},
    })
    comet.destroy_entity(comet.self_entity())
end

return script
