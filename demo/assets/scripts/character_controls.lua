local script = {}
script.properties = {
    camera = {type = "entity"},
    visual = {type = "entity"},
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
end

function script:fixed_update()
    comet.control_character(true, self.controls)
end

function script:on_stop()
    comet.set_input_context("character", false)
end
return script
