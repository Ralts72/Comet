local progress = require("scripts.character_progress")
local script = {}

script.properties = {
    player = {type = "entity"},
    beacon = {type = "entity"},
    level = 1,
    goal = 1,
    collection = {type = "data"},
}

function script:on_start()
    progress.initialize()
    self.reward = assert(comet.data_get(self.parameters.collection, "goal"), "Missing goal reward")
    self.feedback = assert(comet.data_get(self.reward.feedback, "goal"), "Missing goal feedback")
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
    comet.emit_particles(self.reward.particles)
    progress.collect(self.parameters.level, self.parameters.goal)
    if self.parameters.beacon:is_valid() then
        local offset = self.feedback.beacon.translation
        self.parameters.beacon:translate(offset[1], offset[2], offset[3])
    end
    if comet.session_get("character.finished") then
        comet.log("挑战完成！可通过 HUD 进入下一关或返回第一关。")
    end
    comet.destroy_entity(comet.self_entity())
end

return script
