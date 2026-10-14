local script = {}

script.properties = {
    panel = {type = "entity"},
    lift = 3.2,
    speed = 2,
}

function script:update(dt)
    if not comet.session_get("character.finished") or not self.parameters.panel:is_valid() then
        return
    end
    if comet.has_rigid_body(comet.self_entity()) then
        comet.remove_rigid_body(comet.self_entity())
    end
    local _, height = self.parameters.panel:position()
    local step = math.min(self.parameters.speed * dt, self.parameters.lift - height)
    if step <= 0 then return end
    self.parameters.panel:translate(0, step, 0)
end

return script
