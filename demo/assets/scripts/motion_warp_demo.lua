-- 两个演员共用重演节奏；位移／对位仍由动画和物理系统处理。
local script = {}
script.properties = {model = {type = "entity"}}
function script:on_start()
    self.origin = {comet.position()}
    self.elapsed = 0
end
function script:fixed_update(dt)
    local previous = self.elapsed
    self.elapsed = self.elapsed + dt
    if previous < 0.8 and self.elapsed >= 0.8 then
        comet.set_animation_parameter(self.parameters.model, "perform", true)
    elseif self.elapsed >= 3.5 then
        comet.self_entity():set_position(table.unpack(self.origin))
        comet.set_animation_parameter(self.parameters.model, "perform", false)
        self.elapsed = 0
    end
end
return script
