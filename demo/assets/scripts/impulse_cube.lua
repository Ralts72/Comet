local script = {}

script.properties = {
    impulse = 3.5,
}

function script:fixed_update()
    if comet.action_pressed("demo.impulse") then
        comet.apply_impulse(0, self.parameters.impulse, 0)
    end
end

return script
