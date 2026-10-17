local script = {events = {
    footstep = "footstep", ["combat.strike"] = "strike", ["combat.recover"] = "recover",
    ["combat.flash"] = "flash", ["combat.defeat"] = "defeat",
}}
script.properties = {actor = {type = "entity"}}

function script:on_start()
    self.base = {comet.self_entity():scale()}
    self.pulse = 0
end

function script:footstep()
    local entity = comet.self_entity()
    if comet.get_animation_parameter(entity, "speed") > 0.2
        and comet.get_animation_parameter(entity, "grounded") then comet.play_one_shot() end
end

function script:strike() comet.emit("combat.strike", nil, self.parameters.actor) end
function script:recover() comet.emit("combat.recover", nil, self.parameters.actor) end
function script:flash() self.pulse = 0.2 end
function script:defeat() self.defeated = 0 end

function script:update(dt)
    self.pulse = math.max(0, self.pulse - dt)
    local scale = 1 + self.pulse * 0.35
    if self.defeated then
        self.defeated = self.defeated + dt
        scale = math.max(0.001, 1 - self.defeated / 0.4)
    end
    comet.self_entity():set_scale(self.base[1] * scale, self.base[2] * scale, self.base[3] * scale)
end

return script
