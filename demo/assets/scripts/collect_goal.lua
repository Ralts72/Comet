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
    comet.play_one_shot()
    comet.create_entity("Collected_Goal_" .. score)
    comet.destroy_entity(comet.self_entity())
end

return script
