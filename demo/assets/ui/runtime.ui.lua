local function escape(text)
    return (text:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;")
        :gsub('"', "&quot;"):gsub("'", "&#39;"))
end
local function journey_frame(self, api, frame)
    local active = frame.game_available and api.session_get("tour.active") == true
    api.set("tour_visible", active)
    api.set("tour_collection", active and api.session_get("tour.collection") == true)
    if not active then self.state.journey_open = false; return end
    -- 章节菜单先处理返回，避免同一按键又打开设置菜单。
    if self.state.journey_open and self.state.open and api.pressed("key", "Escape") then
        self:on_event(api, "cancel")
        self.state.journey_open = false
    end
    local number = math.floor(api.session_get("tour.number") or 1)
    local total = math.floor(api.session_get("tour.total") or 1)
    local completed = api.session_get("tour.completed") or ""
    local visited = api.session_get("tour.visited") or ""
    local _, count = completed:gsub("1", "")
    api.set("tour_section", api.session_get("tour.section") or "")
    api.set("tour_title", api.session_get("tour.title") or "")
    api.set("tour_description", api.session_get("tour.description") or "")
    api.set("tour_task", api.session_get("tour.task") or "")
    api.set("tour_progress", string.format("第 %d / %d 关 · 已完成 %d 关", number, total, count))
    api.set("tour_done", completed:sub(number, number) == "1")
    api.set("tour_save_status", api.session_get("tour.save_status") or "")
    api.set("tour_has_reference", api.session_get("tour.has_reference") == true)
    api.set("tour_reference_label", api.session_get("tour.reference") and "返回角色视角 / V" or "参考镜头 / V")
    local quality = api.session_get("tour.needs_deferred") and api.quality_settings() or nil
    api.set("tour_needs_deferred", quality ~= nil and quality.active.render_path ~= "deferred")
    if self.state.open then
        self.state.journey_open = false
    elseif api.pressed("key", "F2") then
        self.state.journey_open = not self.state.journey_open
    elseif self.state.journey_open and (api.pressed("key", "Escape") or api.pressed("gamepad_button", "East")) then
        self.state.journey_open = false
    elseif not self.state.journey_open then
        if api.pressed("key", "PageDown") then api.emit("tour.next")
        elseif api.pressed("key", "PageUp") then api.emit("tour.previous")
        elseif api.session_get("tour.has_reference") and api.pressed("key", "V") then
            api.emit("tour.reference")
        end
    end
    local entries = api.session_get("tour.entries") or ""
    local signature = tostring(number) .. completed .. visited .. entries
    if signature ~= self.state.journey_catalog then
        local rows, section, index = {}, "", 0
        for line in entries:gmatch("[^\n]+") do
            index = index + 1
            local group, title = line:match("^(.-)\t(.*)$")
            group, title = group or "旅程", title or line
            if group ~= section then
                rows[#rows + 1] = '<div class="chapter-section">' .. escape(group) .. '</div>'
                section = group
            end
            local style = index == number and "chapter selected" or "chapter"
            local badge = "未到达"
            if completed:sub(index, index) == "1" then badge = "已完成"
            elseif visited:sub(index, index) == "1" then badge = "已到访" end
            rows[#rows + 1] = string.format('<button id="tour-chapter-%d" class="%s" data-event-click="command(\'tour_goto\', \'%d\')">%02d · %s <span>%s</span></button>', index, style, index, index, escape(title), badge)
        end
        api.markup("tour-chapters", table.concat(rows))
        self.state.journey_catalog = signature
    end
end
local function journey_present(self, api)
    local active = self.state.available and api.session_get("tour.active") == true
    local open = active and self.state.journey_open and not self.state.open
    api.property("tour-menu", "display", open and "block" or "none")
    api.set("tour_actions", active and not self.state.open and not open)
    api.modal(self.state.open or open)
    if open and not api.has_focus("tour-panel") then api.focus_first("tour-panel") end
end
local function journey_event(self, api, operation, argument)
    if self.state.open or api.session_get("tour.active") ~= true then return end
    if operation == "tour_menu" then self.state.journey_open = not self.state.journey_open
    elseif operation == "tour_close" then self.state.journey_open = false
    elseif operation == "tour_goto" then
        api.emit("tour.goto", tonumber(argument)); self.state.journey_open = false
    elseif operation == "tour_deferred" then
        local quality = api.quality_settings()
        if quality then
            api.quality_apply(1, quality.max_anisotropy, quality.render_scale, true, "deferred")
        end
    else
        local events = {tour_next = "tour.next", tour_previous = "tour.previous",
            tour_complete = "tour.complete", tour_save = "tour.save",
            tour_continue = "tour.continue", tour_reference = "tour.reference"}
        if events[operation] then
            api.emit(events[operation])
            if operation == "tour_continue" then self.state.journey_open = false end
        end
    end
end

-- Engine 管理设置草稿、录入、应用和焦点；页面与游戏策略留在项目中。
local motions = {idle = "站立", walk = "行走", run = "跑步", crouch = "蹲伏", jump = "跳跃", fall = "下落"}
return ui.settings {
    shortcuts = {open = "Escape", open_secondary = "F1"},
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
        combat_game = false, combat_health = "", combat_status = "", combat_target = "",
        tour_visible = false, tour_collection = false, tour_actions = false, tour_section = "", tour_title = "",
        tour_description = "", tour_task = "", tour_progress = "", tour_done = false,
        tour_has_reference = false, tour_reference_label = "", tour_needs_deferred = false,
        tour_save_status = ""},
    state = {journey_open = false, journey_catalog = ""},
    required_elements = {"hud", "fps", "notice", "tour-guide", "tour-menu", "tour-chapters"},
    on_deactivate = function(self) self.state.journey_open = false end,
    on_frame = function(self, api, frame)
        journey_frame(self, api, frame)
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
        api.set("character_game", available and api.session_get("tour.active") ~= true)
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
        journey_present(self, api)
        api.set("course_actions", self.state.available and not self.state.open
            and api.session_get("demo.mode") ~= "combat"
            and api.session_get("tour.active") ~= true
            and type(api.session_get("character.score")) == "number")
    end,
    on_event = function(self, api, operation, argument)
        journey_event(self, api, operation, argument)
        if self.state.open then return end
        local events = {course_next = "character.next", course_return = "character.return",
            course_save = "character.save", course_continue = "character.continue"}
        if events[operation] then api.emit(events[operation]) end
    end,
}
