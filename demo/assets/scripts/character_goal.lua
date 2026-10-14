local progress = require("scripts.character_progress")
local script = {}

script.properties = {
    player = {type = "entity"},
    beacon = {type = "entity"},
    level = 1,
    goal = 1,
}

function script:on_start()
    progress.initialize()
    self.collected = not comet.has_rigid_body(comet.self_entity())
        or progress.collected(self.parameters.level, self.parameters.goal)
end

function script:update()
    if self.collected then comet.destroy_entity(comet.self_entity()) end
end

function script:on_trigger_enter(other)
    if self.collected or other ~= self.parameters.player then
        return
    end
    self.collected = true
    comet.remove_rigid_body(comet.self_entity())
    comet.play_one_shot()
    progress.collect(self.parameters.level, self.parameters.goal)
    if self.parameters.beacon:is_valid() then
        self.parameters.beacon:translate(0, 0.6, 0)
    end
    if comet.session_get("character.finished") then
        comet.log("挑战完成！可通过 HUD 进入下一关或返回第一关。")
    end
    comet.destroy_entity(comet.self_entity())
end

return script
