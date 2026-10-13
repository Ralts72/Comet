local script = {}

script.properties = {
    player = {type = "entity"},
    beacon = {type = "entity"},
    target_count = 3,
}

function script:on_start()
    self.collected = not comet.has_rigid_body(comet.self_entity())
end

function script:on_trigger_enter(other)
    if self.collected or other ~= self.parameters.player then
        return
    end
    self.collected = true
    comet.remove_rigid_body(comet.self_entity())
    comet.play_one_shot()
    local score = (comet.session_get("character.score") or 0) + 1
    comet.session_set("character.score", score)
    if self.parameters.beacon:is_valid() then
        self.parameters.beacon:translate(0, 0.6, 0)
    end
    if score >= self.parameters.target_count then
        comet.session_set("character.finished", true)
        comet.log("角色挑战完成：已收集全部目标。按 R / Start 重新开始。")
    end
    comet.destroy_entity(comet.self_entity())
end

return script
