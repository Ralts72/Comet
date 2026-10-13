local script = {}

script.properties = {
    camera = {type = "entity"},
    visual = {type = "entity"},
}

function script:on_start()
    self.controls = {
        movement_reference = self.parameters.camera,
        facing = self.parameters.visual,
        run = "character.run",
        crouch = "character.crouch",
    }
    comet.set_input_context("character", true)
    if comet.session_get("character.score") == nil then
        comet.session_set("character.score", 0)
        comet.session_set("character.finished", false)
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

end

function script:on_stop()
    comet.set_input_context("character", false)
end

return script
