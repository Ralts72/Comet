local script = {events = {
    ["combat.strike"] = "strike",
    ["combat.recover"] = "recover",
    ["combat.damage"] = "damage",
}}

script.properties = {
    player = true,
    slot = "player",
    label = "练习者",
    target = {type = "entity"},
    camera = {type = "entity"},
    visual = {type = "entity"},
    model = {type = "entity"},
    effects = {type = "entity"},
    stats = {type = "data"},
}

function script:on_start()
    self.tuning = assert(comet.data_get(self.parameters.stats,
        self.parameters.player and "player" or "guard"), "Missing combat tuning")
    self.health = self.tuning.health
    self.cooldown = 0
    self.attacking = false
    self.health_key = "combat.health." .. self.parameters.slot
    comet.session_set(self.health_key, self.health)
    if self.parameters.player then
        self.controls = {movement_reference = self.parameters.camera,
            facing = self.parameters.visual, run = "character.run", crouch = "character.crouch"}
        comet.set_input_context("character", true)
        comet.set_input_context("combat", true)
        comet.session_set("demo.mode", "combat")
        comet.session_set("combat.maximum", self.health)
        comet.session_set("combat.total", self.tuning.opponents)
        comet.session_set("combat.defeated", 0)
        comet.session_set("combat.target", "靠近守卫，朝向目标后攻击")
    end
end

function script:begin_attack()
    self.attacking = true
    self.hit_done = false
    self.cooldown = self.tuning.cooldown
    comet.set_animation_parameter(self.parameters.model, "attacking", true)
end

function script:strike()
    -- 动画事件只记下意图；命中查询在下一固定步读取真实物理状态。
    if self.attacking and not self.hit_done then self.strike_pending = true end
end

function script:recover()
    self.recover_pending = true
end

function script:apply_strike()
    self.hit_done = true
    local visual = self.parameters.visual
    local x, y, z = visual:transform_point(0, 0, 0)
    local fx, _, fz = visual:transform_point(0, 0, -1)
    fx, fz = fx - x, fz - z
    local length = math.sqrt(fx * fx + fz * fz)
    fx, fz = fx / length, fz / length
    local range = self.tuning.range
    local center = {x + fx * range * 0.5, y + 0.9, z + fz * range * 0.5}
    local layers = self.parameters.player and 4 or 2
    local candidates = comet.overlap_sphere(center, range * 0.65, {layers = layers})
    for _, target in ipairs(candidates) do
        local tx, ty, tz = target:transform_point(0, 0, 0)
        local dx, dz = tx - x, tz - z
        local distance = math.sqrt(dx * dx + dz * dz)
        local facing = distance < 0.001 or (dx * fx + dz * fz) / distance > 0.35
        local blocked = distance > 0.001
            and comet.raycast({x, y + 1, z}, {dx, ty - y, dz}, {layers = 1})
        if distance <= range and facing and not blocked then
            comet.emit("combat.damage", self.tuning.damage, target)
        end
    end
end

function script:damage(amount)
    assert(type(amount) == "number" and amount > 0, "Combat damage must be positive")
    if self.health <= 0 then return end
    self.health = math.max(0, self.health - amount)
    comet.session_set(self.health_key, self.health)
    comet.emit_particles(self.tuning.hit_particles, self.parameters.effects)
    comet.play_one_shot(self.parameters.effects)
    comet.emit("combat.flash", nil, self.parameters.model)
    if not self.parameters.player then
        comet.session_set("combat.target", self.parameters.label .. "：" .. math.floor(self.health)
            .. " / " .. math.floor(self.tuning.health))
    end
    if self.health == 0 then
        self.attacking = false
        comet.set_animation_parameter(self.parameters.model, "attacking", false)
        comet.emit("combat.defeat", nil, self.parameters.model)
        if not self.parameters.player then
            comet.session_set("combat.defeated", comet.session_get("combat.defeated") + 1)
            if comet.session_get("tour.active")
                and comet.session_get("combat.defeated") >= comet.session_get("combat.total") then
                comet.emit("tour.complete")
            end
            self.remove_after = 1.2
        end
    end
end

function script:update_player()
    local won = comet.session_get("combat.defeated") >= self.tuning.opponents
    comet.control_character(self.health > 0 and not self.attacking and not won, self.controls)
    if self.health > 0 and not won and not self.attacking and self.cooldown == 0
        and comet.action_pressed("character.attack") then self:begin_attack() end
    local _, y = comet.position()
    if y < -8 then comet.restart_scene() end
end

function script:update_guard(dt)
    if self.health <= 0 then
        comet.move_character(0, 0, false)
        self.remove_after = self.remove_after - dt
        if self.remove_after <= 0 then comet.destroy_entity(comet.self_entity()) end
        return
    end
    local target = self.parameters.target
    if not target:is_valid() or comet.session_get("combat.health.player") <= 0 then
        comet.move_character(0, 0, false)
        return
    end
    local x, _, z = comet.position()
    local tx, ty, tz = target:transform_point(0, 0, 0)
    local dx, dz = tx - x, tz - z
    if not self.attacking and dx * dx + dz * dz >= (self.tuning.range * 0.8)^2 then
        comet.navigate_to(tx, ty, tz, true)
        return
    end
    comet.move_character(0, 0, false)
    if self.attacking then return end
    comet.face_movement(self.parameters.visual, dx, dz)
    if self.cooldown == 0 then self:begin_attack() end
end

function script:fixed_update(dt)
    if self.parameters.player and comet.action_pressed("demo.restart") then
        comet.restart_scene()
        return
    end
    self.cooldown = math.max(0, self.cooldown - dt)
    if self.health > 0 and self.strike_pending then self:apply_strike() end
    self.strike_pending = false
    if self.recover_pending then
        self.attacking = false
        self.recover_pending = false
        comet.set_animation_parameter(self.parameters.model, "attacking", false)
    end
    if self.parameters.player then
        self:update_player()
    else
        self:update_guard(dt)
    end
end

function script:on_stop()
    if self.parameters.player then
        comet.set_input_context("character", false)
        comet.set_input_context("combat", false)
    end
end

return script
