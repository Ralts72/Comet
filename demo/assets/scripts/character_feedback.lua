local script = {events = {footstep = "footstep"}}

function script:footstep(foot)
    local entity = comet.self_entity()
    if comet.get_animation_parameter(entity, "speed") > 0.2
        and comet.get_animation_parameter(entity, "grounded") then
        comet.play_one_shot()
    end
end

return script
