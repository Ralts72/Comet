local progress = require("scripts.character_progress")
local script = {events = {
    ["character.next"] = "next_level",
    ["character.return"] = "return_to_first",
    ["character.save"] = "save_progress",
    ["character.continue"] = "continue_progress",
}}

script.properties = {
    level = 1,
    camera = {type = "entity"},
    visual = {type = "entity"},
    finish_gate = {type = "prefab"},
    finish_position = {0, 1.4, -10},
}

function script:on_start()
    comet.session_set("demo.mode", "character")
    self.controls = {
        movement_reference = self.parameters.camera,
        facing = self.parameters.visual,
        run = "character.run",
        crouch = "character.crouch",
    }
    comet.set_input_context("character", true)
    progress.initialize()
    progress.sync(self.parameters.level)
    local gate = comet.session_get("character.gate")
    if self.parameters.finish_gate ~= "" and (not gate or not comet.find_entity(gate)) then
        comet.session_set("character.gate",
            comet.instantiate_prefab(self.parameters.finish_gate, self.parameters.finish_position))
    end
end

function script:fixed_update()
    if comet.action_pressed("demo.restart") then
        comet.restart_scene()
        return
    end
    local _, y = comet.position()
    if y < -8 then
        comet.restart_scene()
        return
    end
    if comet.control_character(not comet.session_get("character.finished"), self.controls) then
        comet.play_one_shot()
    end
end

function script:update()
    local state = comet.character_state()
    comet.session_set("character.motion", state.motion)
    -- 同一帧可能收集多个目标，更新结束前只提交一次完整进度。
    if comet.session_get("character.save_pending") then
        progress.save()
        comet.session_set("character.save_pending", false)
    end
end

function script:on_stop()
    comet.set_input_context("character", false)
end

function script:next_level()
    if self.parameters.level == 1 and comet.session_get("character.finished") then
        if comet.session_get("tour.active") then comet.emit("tour.next")
        else comet.load_scene("scenes/character_second.scene") end
    end
end

function script:return_to_first()
    if self.parameters.level == 2 then
        if comet.session_get("tour.active") then comet.emit("tour.goto", 1)
        else comet.load_scene("scenes/character.scene") end
    end
end

function script:save_progress()
    comet.session_set("character.save_pending", true)
end

function script:continue_progress()
    comet.load_checkpoint()
end

return script
