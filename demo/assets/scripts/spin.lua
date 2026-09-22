return {
    properties = {
        speed = 100,
        enabled = true,
    },

    fixed_update = function(self, dt)
        if comet.action_pressed("spin.toggle") then
            self.paused = not self.paused
        end
        if self.parameters.enabled and not self.paused then
            comet.rotate(0, self.parameters.speed * dt, 0)
        end
    end,
}
