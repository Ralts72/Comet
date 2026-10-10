local function escape(text)
    return (text:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;")
        :gsub('"', "&quot;"):gsub("'", "&#39;"))
end

local function button(operation, caption, action, binding)
    return '<button id="' .. operation .. '-' .. binding
        .. '" data-attrif-disabled="waiting" data-event-click="command(\''
        .. operation .. '\',\'' .. action .. '\',\'' .. binding .. '\')">'
        .. caption .. '</button>'
end

local function rows(self, ui)
    local actions = ui.input_actions()
    ui.set("has_actions", #actions > 0)
    if #actions == 0 then
        ui.set("action_name", "暂无可配置动作")
        ui.markup("bindings", '<p class="muted">项目没有玩家输入动作。</p>')
        return
    end
    self.state.selected = math.min(self.state.selected, #actions)
    local action = actions[self.state.selected]
    ui.set("action_name", action.name)
    local contents = {}
    if action.disabled then
        contents[#contents + 1] = '<p class="muted">此动作已禁用；恢复全部默认可重新启用。</p>'
    end
    if not action.compatible then
        contents[#contents + 1] = '<p class="muted">覆盖配置与项目动作类型不一致；恢复全部默认后再编辑。</p>'
    end
    for _, binding in ipairs(action.bindings) do
        local label = binding.control
        if label == "" then label = "配置不兼容" end
        if binding.disabled then label = label .. " · 已禁用" end
        contents[#contents + 1] = '<div class="binding"><span class="binding-name">'
            .. escape(label) .. '</span>'
        if not action.disabled and action.compatible then
            if binding.source == "key" then
                contents[#contents + 1] = button("key", "录入按键", action.id, binding.id)
            elseif binding.source == "gamepad_button" then
                contents[#contents + 1] = button("pad", "录入手柄按钮", action.id, binding.id)
            else
                contents[#contents + 1] = '<span class="binding-note">此输入保留项目配置。</span>'
            end
            local caption = "禁用"
            if binding.disabled then caption = "启用" end
            contents[#contents + 1] = button("toggle_binding", caption, action.id, binding.id)
        end
        contents[#contents + 1] = button("restore_binding", "恢复此绑定", action.id, binding.id)
        contents[#contents + 1] = '</div>'
    end
    ui.markup("bindings", table.concat(contents))
end

local sizes = {{960, 720}, {1280, 720}, {1920, 1080}}
local modes = {"windowed", "borderless", "fullscreen"}
local mode_labels = {windowed = "窗口", borderless = "无边框", fullscreen = "全屏"}

local function display_labels(self, ui)
    local state = self.state
    ui.set("display_available", state.display_available)
    if not state.display_available then return end
    ui.set("display_size", tostring(state.display_width) .. " × " .. tostring(state.display_height))
    ui.set("display_mode", mode_labels[state.display_mode])
    local vsync = "关闭"
    if state.display_vsync then vsync = "开启" end
    ui.set("display_vsync", vsync)
    ui.set("display_preview", state.display_preview)
end

local function load_display(self, ui)
    local settings, message = ui.display_settings()
    local state = self.state
    state.display_available = settings ~= nil
    state.display_waiting = false
    ui.set("display_error", message or "")
    ui.set("display_status", "窗口尺寸使用逻辑单位；全屏使用显示器尺寸。")
    if settings then
        state.display_width, state.display_height = settings.width, settings.height
        state.display_mode, state.display_vsync = settings.mode, settings.vsync
        state.display_preview = settings.preview
        if settings.preview then
            ui.set("display_status", "Play 将尺寸用于固定分辨率预览；窗口模式和 VSync 请在独立 App 中设置。")
        end
    end
    display_labels(self, ui)
end

local function sync(self, ui)
    local status = ui.input_status()
    if self.state.open and self.state.revision ~= status.revision then
        rows(self, ui)
        self.state.revision = status.revision
    end
    ui.set("display_waiting", self.state.display_waiting)
    ui.set("waiting", status.waiting or self.state.display_waiting)
    local error_text = self.state.error
    if error_text == "" then error_text = status.error end
    ui.set("error_text", error_text)
    local text = "更改会在应用后保存到当前玩家目录，不修改项目默认配置。"
    if status.waiting then
        text = "正在保存…"
    elseif status.capturing then
        text = "等待输入；Esc 取消本次录入。录入按钮需要先释放。"
    elseif not status.compatible then
        text = "当前覆盖配置不兼容；恢复全部默认后再编辑。"
    elseif status.has_issues then
        text = "部分旧覆盖配置已回退到项目默认；有效草稿仍会保留。"
    end
    ui.set("status_text", text)
    local display, tab_index = "none", "auto"
    if self.state.open then display, tab_index = "block", "none" end
    ui.property("menu", "display", display)
    ui.property("settings", "tab-index", tab_index)
    ui.modal(self.state.open)
end

local function close(self, ui)
    if ui.input_status().waiting then return end
    self.state.open = false
    self.state.error = ""
    self.state.error_only = false
    self.state.revision = -1
    self.state.display_waiting = false
    self.state.display_available = false
    ui.input_end()
    sync(self, ui)
end

local function focus_menu(self, ui)
    if self.state.error_only or #ui.input_actions() == 0 then
        ui.focus("cancel")
    else
        ui.focus_first("bindings")
    end
end

local function open(self, ui)
    if self.state.open or not self.state.available then return end
    local ok, message = ui.input_begin({"Escape", "F1", "F6"}, {"Start"})
    self.state.open = true
    self.state.selected = 1
    self.state.revision = -1
    self.state.error_only = not ok
    self.state.error = message
    load_display(self, ui)
    sync(self, ui)
    focus_menu(self, ui)
end

return {
    model = {
        fps_text = "0 FPS", action_name = "", status_text = "", error_text = "",
        menu_available = false, waiting = false, has_actions = false,
        display_available = false, display_preview = false, display_waiting = false,
        display_size = "", display_mode = "", display_vsync = "",
        display_status = "", display_error = "",
    },
    state = {
        open = false, available = false, error_only = false, selected = 1,
        revision = -1, error = "", display_waiting = false,
        display_available = false, display_width = 960, display_height = 720,
        display_mode = "windowed", display_vsync = false, display_preview = false,
    },
    on_mount = function(self, ui)
        for _, id in ipairs({"hud", "fps", "settings", "notice", "menu", "panel",
            "action-selector", "action-name", "previous", "next", "bindings", "status",
            "error", "footer", "restore", "cancel", "apply", "display",
            "display-size", "display-mode", "display-vsync", "display-apply", "display-restore"}) do
            ui.require_element(id)
        end
        self.state.revision = -1
        display_labels(self, ui)
        ui.set("menu_available", self.state.available)
        sync(self, ui)
        -- 验证隐藏菜单的布局和纹理；on_present 在发布后的首帧恢复项目状态。
        ui.property("menu", "display", "block")
        if self.state.open then focus_menu(self, ui) end
    end,
    on_frame = function(self, ui, frame)
        ui.set("fps_text", tostring(math.floor(math.max(0, math.min(frame.fps, 100000)) + 0.5)) .. " FPS")
        self.state.available = frame.game_available
        ui.set("menu_available", frame.game_available)
        local status = ui.input_status()
        if not status.capturing and ui.pressed("key", "F6") then ui.reload() end
        if not self.state.open then
            if ui.pressed("key", "F1") or ui.pressed("gamepad_button", "Start") then open(self, ui) end
        elseif not status.waiting and not status.capturing then
            if ui.pressed("key", "Escape") or ui.pressed("gamepad_button", "East") then close(self, ui) end
        end
    end,
    on_event = function(self, ui, operation, action, binding)
        if operation == "open" then open(self, ui); return end
        if not self.state.open or ui.input_status().waiting or self.state.display_waiting then return end
        if self.state.error_only and operation ~= "cancel" and not operation:match("^display_") then return end
        if operation:match("^display_") and self.state.display_available then
            local draft = self.state
            if operation == "display_size" then
                local index = 0
                for i, size in ipairs(sizes) do
                    if draft.display_width == size[1] and draft.display_height == size[2] then index = i end
                end
                local size = sizes[index % #sizes + 1]
                draft.display_width, draft.display_height = size[1], size[2]
            elseif operation == "display_mode" and not draft.display_preview then
                for i, mode in ipairs(modes) do
                    if draft.display_mode == mode then draft.display_mode = modes[i % #modes + 1]; break end
                end
            elseif operation == "display_vsync" and not draft.display_preview then
                draft.display_vsync = not draft.display_vsync
            elseif operation == "display_restore" then
                local current = ui.display_settings()
                if current then
                    draft.display_width, draft.display_height = current.defaults.width, current.defaults.height
                    if not draft.display_preview then
                        draft.display_mode, draft.display_vsync = current.defaults.mode, current.defaults.vsync
                    end
                end
            elseif operation == "display_apply" then
                self.state.display_waiting = true
                ui.display_apply(draft.display_width, draft.display_height, draft.display_mode, draft.display_vsync)
            end
            display_labels(self, ui)
        elseif operation == "cancel" then
            close(self, ui)
        elseif operation == "apply" then
            ui.input_apply()
        elseif operation == "restore" then
            ui.input_restore()
        elseif operation == "previous" or operation == "next" then
            local count = #ui.input_actions()
            if count > 0 then
                local step = 1
                if operation == "previous" then step = -1 end
                self.state.selected = (self.state.selected - 1 + step) % count + 1
                self.state.revision = -1
            end
        elseif operation == "key" or operation == "pad" then
            self.state.error = ""
            ui.input_capture(action, binding, operation)
        elseif operation == "toggle_binding" then
            ui.input_toggle_binding(action, binding)
        elseif operation == "restore_binding" then
            ui.input_restore_binding(action, binding)
        end
        sync(self, ui)
    end,
    on_present = function(self, ui)
        sync(self, ui)
        local status = ui.input_status()
        if self.state.open and not status.waiting and not status.capturing and not ui.has_focus("panel") then
            focus_menu(self, ui)
        end
    end,
    on_input_result = function(self, ui, success)
        if success then close(self, ui) else sync(self, ui) end
    end,
    on_display_result = function(self, ui, success, message)
        self.state.display_waiting = false
        ui.set("display_error", message)
        if success then ui.set("display_status", "显示设置已保存；更改在后续帧应用。") end
        sync(self, ui)
    end,
    on_reload_error = function(self, ui, message)
        self.state.error = message
        sync(self, ui)
    end,
    on_deactivate = close,
}
