-- Engine 管理设置草稿、录入、应用和焦点；页面与游戏策略留在项目中。
local motions = {idle = "站立", walk = "行走", run = "跑步", crouch = "蹲伏", jump = "跳跃", fall = "下落"}
return ui.settings {
    display = {sizes = {{960, 720}, {1280, 720}, {1920, 1080}}},
    audio = true,
    quality = {
        presets = {
            {id = "performance", name = "性能", msaa_samples = 1,
                max_anisotropy = 2, render_scale = 0.75},
            {id = "balanced", name = "均衡", msaa_samples = 2,
                max_anisotropy = 4, render_scale = 1},
            {id = "quality", name = "质量", msaa_samples = 4,
                max_anisotropy = 8, render_scale = 1},
        },
    },
    model = {fps_text = "0 FPS", character_game = false, character_status = "",
        next_level = false, return_level = false, course_actions = false,
        combat_game = false, combat_health = "", combat_status = "", combat_target = ""},
    required_elements = {"hud", "fps", "notice"},
    on_frame = function(self, api, frame)
        local fps = math.floor(math.max(0, math.min(frame.fps, 100000)) + 0.5)
        api.set("fps_text", tostring(fps) .. " FPS")
        local score = api.session_get("character.score")
        local combat = frame.game_available and api.session_get("demo.mode") == "combat"
        api.set("combat_game", combat)
        if combat then
            local health = math.floor(api.session_get("combat.health.player") or 0)
            local maximum = math.floor(api.session_get("combat.maximum") or 0)
            local defeated = math.floor(api.session_get("combat.defeated") or 0)
            local total = math.floor(api.session_get("combat.total") or 0)
            api.set("combat_health", "生命：" .. health .. " / " .. maximum)
            local status = "击败守卫：" .. defeated .. " / " .. total
            if health <= 0 then status = "你已倒下，按 R / Start 重新挑战"
            elseif defeated >= total then status = "挑战完成！按 R / Start 重新挑战" end
            api.set("combat_status", status)
            api.set("combat_target", api.session_get("combat.target") or "")
        end
        local available = not combat and frame.game_available and type(score) == "number"
        local level = api.session_get("character.level") or 1
        api.set("character_game", available)
        api.set("next_level", available and level == 1 and api.session_get("character.finished") == true)
        api.set("return_level", available and level == 2)
        if type(score) == "number" then
            local status = "第 " .. tostring(math.floor(level)) .. " 关 · 收集目标：" .. tostring(math.floor(score)) .. " / 3"
            local motion = api.session_get("character.motion")
            if motions[motion] then
                status = status .. " · " .. motions[motion]
            end
            if api.session_get("character.finished") then
                status = "挑战完成！可进入第二关"
                if level == 2 then status = "两关挑战完成！可返回第一关" end
            end
            api.set("character_status", status)
        end
    end,
    on_present = function(self, api)
        api.set("course_actions", self.state.available and not self.state.open
            and api.session_get("demo.mode") ~= "combat"
            and type(api.session_get("character.score")) == "number")
    end,
    on_event = function(self, api, operation)
        if self.state.open then return end
        local events = {course_next = "character.next", course_return = "character.return",
            course_save = "character.save", course_continue = "character.continue"}
        if events[operation] then api.emit(events[operation]) end
    end,
}
