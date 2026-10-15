local script = {}
script.properties = {
    player = {type = "entity"},
    patrol_a = {-6, 0, 0},
    patrol_b = {6, 0, 0},
    detect_distance = 6,
    forget_distance = 11,
    wait_seconds = 1,
}

function script:on_start()
    self.state = "patrol"
    self.destination = self.parameters.patrol_b
    self.wait = 0
end

function script:fixed_update(dt)
    local x, y, z = comet.position()
    local player = self.parameters.player
    local px, py, pz, distance
    if player:is_valid() then
        px, py, pz = player:position()
        distance = math.sqrt((px - x)^2 + (pz - z)^2)
    end
    if distance and distance < self.parameters.detect_distance and math.abs(py - y) < 2 then
        self.state = "chase"
        self.wait = 0
    elseif self.state == "chase" and (not distance or distance > self.parameters.forget_distance) then
        self.state = "return"
        self.destination = self.parameters.patrol_a
    end
    if self.wait > 0 then
        self.wait = self.wait - dt
        return
    end
    local target = self.destination
    if self.state == "chase" then target = {px, py, pz} end
    local status = comet.navigate_to(target[1], target[2], target[3], self.state == "chase")
    if status == "arrived" and self.state ~= "chase" then
        self.state = "patrol"
        if self.destination == self.parameters.patrol_a then
            self.destination = self.parameters.patrol_b
        else
            self.destination = self.parameters.patrol_a
        end
        self.wait = self.parameters.wait_seconds
    end
    -- 无路径、局部阻塞或预算耗尽时停下等待；下一固定步由引擎重试。
end
return script
