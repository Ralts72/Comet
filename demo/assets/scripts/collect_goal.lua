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
    local score = (comet.session_get("demo.score") or 0) + 1
    comet.session_set("demo.score", score)
    comet.emit("demo.score_changed", score)
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
