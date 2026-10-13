local script = {}

function script:on_start()
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
    if comet.session_get("character.finished") then
        comet.move_character(0, 0, false)
        return
    end
    local jump = comet.action_pressed("character.jump")
    if jump and comet.character_grounded() then
        comet.play_one_shot()
    end
    comet.move_character(comet.action_value("character.move_x"),
        comet.action_value("character.move_z"), jump)
end

function script:on_stop()
    comet.set_input_context("character", false)
end

return script
