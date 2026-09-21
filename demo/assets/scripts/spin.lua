return {
    properties = {
        speed = 100,
        enabled = true,
    },

    fixed_update = function(self, dt)
        if self.parameters.enabled then
            comet.rotate(0, self.parameters.speed * dt, 0)
        end
    end,
}
