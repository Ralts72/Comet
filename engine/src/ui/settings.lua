-- 通用设置交互；项目决定页面、样式、可选面板及画质档位。
local function settings(options)
    options = options or {}
    assert(type(options) == "table", "ui.settings options must be a table")
    for _, section in ipairs({"input", "display", "quality", "audio"}) do
        local value = options[section]
        assert(value == nil or type(value) == "boolean" or type(value) == "table",
            "ui.settings " .. section .. " must be a boolean or options table")
    end
    assert(not options.render_bindings or type(options.render_bindings) == "function",
        "ui.settings render_bindings must be a function")
    local input_enabled = options.input ~= false
    local input_options = type(options.input) == "table" and options.input or {}
    local display_options = type(options.display) == "table" and options.display or {}
    local quality_options = type(options.quality) == "table" and options.quality or {}
    local elements = {open = "settings", menu = "menu", panel = "panel", cancel = "cancel",
        bindings = "bindings", apply = "apply", restore = "restore", confirm = "display-confirm"}
    for role, id in pairs(options.elements or {}) do
        assert(elements[role] ~= nil, "Unknown ui.settings element role: " .. role)
        assert(type(id) == "string" or (role == "open" and id == false),
            "ui.settings element must be an id; only open can be false")
        elements[role] = id
    end
    local shortcuts = {open = "F1", close = "Escape", reload = "F6",
        gamepad_open = "Start", gamepad_close = "East"}
    for role, control in pairs(options.shortcuts or {}) do
        assert(shortcuts[role] ~= nil, "Unknown ui.settings shortcut: " .. role)
        assert(type(control) == "string" or control == false, "Shortcut must be a control or false")
        shortcuts[role] = control
    end
    local reserved_keys = input_options.reserved_keys or {}
    if not input_options.reserved_keys then
        for _, role in ipairs({"open", "close", "reload"}) do
            if shortcuts[role] then reserved_keys[#reserved_keys + 1] = shortcuts[role] end
        end
    end
    local reserved_buttons = input_options.reserved_buttons or {}
    if not input_options.reserved_buttons and shortcuts.gamepad_open then
        reserved_buttons[1] = shortcuts.gamepad_open
    end
    local function pressed(api, source, control)
        return control and api.pressed(source, control)
    end

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
            ui.markup(elements.bindings, '<p class="muted">项目没有玩家输入动作。</p>')
            return
        end
        self.state.selected = math.max(1, math.min(self.state.selected, #actions))
        local action = actions[self.state.selected]
        ui.set("action_name", action.name)
        if options.render_bindings then
            ui.markup(elements.bindings, options.render_bindings(action))
            return
        end
        local contents = {}
        if action.disabled then
            contents[#contents + 1] = '<p class="muted">此动作已禁用；恢复改键默认可重新启用。</p>'
        end
        if not action.compatible then
            contents[#contents + 1] = '<p class="muted">覆盖配置与项目动作类型不一致；恢复改键默认后再编辑。</p>'
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
        ui.markup(elements.bindings, table.concat(contents))
    end

    local sizes = display_options.sizes or {}
    local frame_rates = display_options.frame_rates or {0, 30, 60, 120, 144, 240}
    local modes = {"windowed", "borderless", "fullscreen"}
    local mode_labels = {windowed = "窗口", borderless = "无边框", fullscreen = "全屏"}
    local output_modes = {"sdr", "hdr", "auto"}
    local output_labels = {sdr = "SDR", hdr = "HDR", auto = "自动"}
    local quality_presets = {}
    local quality_preset_order = {}
    for _, preset in ipairs(quality_options.presets or {}) do
        assert(type(preset.id) == "string" and type(preset.name) == "string",
            "ui.settings quality presets need id and name")
        assert(not quality_presets[preset.id], "Duplicate quality preset: " .. preset.id)
        quality_presets[preset.id] = preset
        quality_preset_order[#quality_preset_order + 1] = preset.id
    end

    local function resolve_preset(preset, settings)
        local samples = 1
        for _, supported in ipairs(settings.supported_msaa) do
            if supported <= preset.msaa_samples then samples = math.max(samples, supported) end
        end
        return {msaa_samples = samples,
            max_anisotropy = math.min(preset.max_anisotropy, settings.max_anisotropy_supported),
            render_scale = preset.render_scale, temporal_aa = preset.temporal_aa or false}
    end

    local function quality_draft(state, settings)
        state.quality_msaa = settings.msaa_samples
        state.quality_anisotropy = settings.max_anisotropy
        state.quality_scale = settings.render_scale
        state.quality_temporal = settings.temporal_aa or false
    end

    local function quality_preset_label(state, settings)
        local names = {}
        for _, key in ipairs(quality_preset_order) do
            local preset = quality_presets[key]
            local resolved = resolve_preset(preset, settings)
            if state.quality_msaa == resolved.msaa_samples
                and state.quality_temporal == resolved.temporal_aa
                and math.abs(state.quality_anisotropy - resolved.max_anisotropy) < 0.000001
                and math.abs(state.quality_scale - resolved.render_scale) < 0.000001 then
                names[#names + 1] = preset.name
            end
        end
        if #names == 0 then return "自定义" end
        return table.concat(names, " / ")
    end

    local function display_size(state, width, height)
        state.display_width, state.display_height = tostring(width), tostring(height)
    end

    local function unsigned_integer(text, maximum)
        local digits = text:match("^%s*(%d+)%s*$")
        local value = digits and tonumber(digits)
        if value and value <= maximum then return value end
    end

    local function dimension(text)
        local value = unsigned_integer(text, 2147483647)
        if value and value > 0 then return value end
    end

    local function display_labels(self, ui)
        local state = self.state
        ui.set("display_available", state.display_available)
        if not state.display_available then return end
        ui.set("display_size", tostring(state.display_width) .. " × " .. tostring(state.display_height))
        ui.set("display_width", state.display_width)
        ui.set("display_height", state.display_height)
        ui.set("display_mode", mode_labels[state.display_mode])
        local vsync = "关闭"
        if state.display_vsync then vsync = "开启" end
        ui.set("display_vsync", vsync)
        ui.set("display_preview", state.display_preview)
        ui.set("display_limit", state.display_limit)
        local limit = "自定义"
        local number = unsigned_integer(state.display_limit, 1000)
        if number == 0 then limit = "无上限"
        elseif number then limit = tostring(number) .. " FPS" end
        ui.set("display_frame_rate", limit)
        ui.set("display_output_mode", output_labels[state.display_output_mode])
        ui.set("display_hdr_headroom", state.display_hdr_headroom)
        ui.set("display_hdr_white", state.display_hdr_white)
        ui.set("display_hdr_disabled", state.display_preview or state.display_output_mode == "sdr")
    end

    local function display_confirmation(self, ui, settings)
        self.state.display_confirming = settings.confirmation_pending
        self.state.display_output_pending = settings.output_pending
        ui.set("display_confirming", settings.confirmation_pending)
        ui.set("display_output_pending", settings.output_pending)
        ui.set("display_confirmation_text", string.format("请在 %d 秒内确认保留，否则自动还原。",
            math.ceil(settings.confirmation_seconds)))
    end

    local function load_display(self, ui)
        local settings, message
        if options.display then settings, message = ui.display_settings() end
        local state = self.state
        state.display_available = settings ~= nil
        state.display_waiting = false
        ui.set("display_error", message or "")
        ui.set("display_status", "窗口尺寸使用逻辑单位；全屏使用显示器尺寸。")
        if settings then
            display_size(state, settings.width, settings.height)
            state.display_mode, state.display_vsync = settings.mode, settings.vsync
            state.display_limit = tostring(settings.frame_rate_limit)
            state.display_preview = settings.preview
            state.display_output_mode = settings.output_mode
            state.display_hdr_headroom = settings.hdr_headroom
            state.display_hdr_white = settings.hdr_white_level * 100
            display_confirmation(self, ui, settings)
            if settings.preview then
                ui.set("display_status", "Play 将尺寸用于固定分辨率预览；窗口模式、VSync、帧率上限和 HDR 请在独立 App 中设置。")
            end
        end
        display_labels(self, ui)
    end

    local function display_output_status(self, ui)
        if not self.state.display_available then return end
        local settings = ui.display_settings()
        if not settings then return end
        if self.state.display_confirming and not settings.confirmation_pending then
            load_display(self, ui)
            ui.set("display_status", "显示试用已还原。")
        else
            display_confirmation(self, ui, settings)
        end
        local status = "当前输出：SDR"
        if settings.preview then
            status = "当前输出：编辑器 SDR 预览；保留独立 App 的 HDR 选择。"
        elseif settings.output_pending then
            status = "等待下一帧应用输出设置…"
        elseif settings.hdr_active then
            status = "当前输出：HDR（扩展线性）"
        elseif settings.output_mode ~= "sdr" then
            status = "当前输出：SDR；当前设备或系统未提供 HDR 输出。"
        end
        ui.set("display_output_active", status)
    end

    local function cycle(current, choices)
        for i, value in ipairs(choices) do
            if current == value then return choices[i % #choices + 1] end
        end
        return choices[1] or current
    end

    local function quality_labels(self, ui, settings)
        local state = self.state
        ui.set("quality_available", state.quality_available)
        if not state.quality_available then return end
        ui.set("quality_msaa", state.quality_temporal and "TAA" or tostring(state.quality_msaa) .. "×")
        ui.set("quality_anisotropy", tostring(state.quality_anisotropy) .. "×")
        ui.set("quality_scale", string.format("%.0f%%", state.quality_scale * 100))
        settings = settings or ui.quality_settings()
        if settings then ui.set("quality_preset", quality_preset_label(state, settings)) end
    end

    local function load_quality(self, ui)
        local settings, message
        if options.quality then settings, message = ui.quality_settings() end
        local state = self.state
        state.quality_available = settings ~= nil
        state.quality_waiting = false
        state.quality_error = message or ""
        if settings then quality_draft(state, settings) end
        quality_labels(self, ui, settings)
    end

    local function quality_status(self, ui)
        if not self.state.quality_available then return end
        local settings = ui.quality_settings()
        if not settings then return end
        local active = settings.active
        local aa = active.temporal_aa and "TAA" or "MSAA " .. tostring(active.msaa_samples) .. "×"
        ui.set("quality_active", string.format("当前生效：%s · 各向异性 %.0f× · 渲染比例 %.0f%%",
            aa, active.max_anisotropy, active.render_scale * 100))
        local status = "渲染比例仅调整场景画面，UI 保持输出分辨率。更改应用后保存。"
        if settings.pending then status = "设置已保存，等待下一帧应用…" end
        local message = self.state.quality_error
        if message == "" then message = settings.error end
        ui.set("quality_status", status)
        ui.set("quality_error", message)
    end

    local function audio_labels(self, ui)
        local state = self.state
        ui.set("audio_available", state.audio_available)
        ui.set("audio_master", state.audio_master)
        ui.set("audio_effects", state.audio_effects)
        ui.set("audio_music", state.audio_music)
        ui.set("audio_error", state.audio_error)
    end

    local function audio_draft(state, settings)
        state.audio_master = settings.master_volume * 100
        state.audio_effects = settings.effects_volume * 100
        state.audio_music = settings.music_volume * 100
    end

    local function load_audio(self, ui)
        local settings, message
        if options.audio then settings, message = ui.audio_settings() end
        local state = self.state
        state.audio_available = settings ~= nil
        state.audio_waiting = false
        state.audio_error = message or ""
        if settings then audio_draft(state, settings) end
        audio_labels(self, ui)
    end

    local function audio_status(self, ui)
        if not self.state.audio_available then return end
        local settings = ui.audio_settings()
        if not settings then return end
        local active = settings.active
        ui.set("audio_active", string.format("当前音量：主音量 %.0f%% · 音效 %.0f%% · 音乐 %.0f%%",
            active.master_volume * 100, active.effects_volume * 100, active.music_volume * 100))
    end

    local function applying(state, status)
        return status.waiting or state.display_waiting or state.quality_waiting or state.audio_waiting
    end

    local function sync(self, ui)
        local status = ui.input_status()
        if input_enabled and self.state.open and self.state.revision ~= status.revision then
            rows(self, ui)
            self.state.revision = status.revision
        end
        ui.set("display_waiting", self.state.display_waiting)
        ui.set("waiting", applying(self.state, status) or self.state.display_confirming)
        local error_text = self.state.error
        if error_text == "" then error_text = status.error end
        ui.set("error_text", error_text)
        local text = "更改保存到当前玩家目录，不修改项目默认配置；显示尺寸或模式变化需确认保留。"
        if status.waiting then
            text = "正在保存…"
        elseif status.capturing then
            text = "等待输入；Esc 取消本次录入。录入按钮需要先释放。"
        elseif not status.compatible then
            text = "当前覆盖配置不兼容；恢复改键默认后再编辑。"
        elseif status.has_issues then
            text = "部分旧覆盖配置已回退到项目默认；有效草稿仍会保留。"
        end
        ui.set("status_text", text)
        local display, tab_index = "none", "auto"
        if self.state.open then display, tab_index = "block", "none" end
        ui.property(elements.menu, "display", display)
        if elements.open then ui.property(elements.open, "tab-index", tab_index) end
        ui.modal(self.state.open)
    end

    local function display_decision(self, ui, confirm)
        if self.state.display_waiting then return end
        if confirm and self.state.display_output_pending then return end
        self.state.display_waiting = true
        if confirm then ui.display_confirm() else ui.display_revert() end
        sync(self, ui)
    end

    local function close(self, ui)
        if ui.input_status().waiting then return end
        if self.state.display_confirming then
            display_decision(self, ui, false)
            return
        end
        self.state.open = false
        self.state.error = ""
        self.state.error_only = false
        self.state.revision = -1
        self.state.display_waiting = false
        self.state.display_available = false
        self.state.quality_available = false
        self.state.quality_waiting = false
        self.state.audio_available = false
        self.state.audio_waiting = false
        if input_enabled then ui.input_end() end
        sync(self, ui)
    end

    local function focus_menu(self, ui)
        if self.state.display_confirming then
            ui.focus(elements.confirm)
        elseif not input_enabled or self.state.error_only or #ui.input_actions() == 0 then
            ui.focus(elements.cancel)
        else
            ui.focus_first(elements.bindings)
        end
    end

    local function open(self, ui)
        if self.state.open or not self.state.available then return end
        local ok, message = true, ""
        if input_enabled then ok, message = ui.input_begin(reserved_keys, reserved_buttons) end
        self.state.open = true
        self.state.selected = 1
        self.state.revision = -1
        self.state.error_only = not ok
        self.state.error = message
        load_display(self, ui)
        load_quality(self, ui)
        load_audio(self, ui)
        sync(self, ui)
        focus_menu(self, ui)
    end

    local function audio_event(self, ui, operation, action)
        local draft = self.state
        if operation == "audio_master" or operation == "audio_effects" or operation == "audio_music" then
            local value = tonumber(action)
            if not value or value ~= value or value < 0 or value > 100 then return end
            draft[operation] = value
        elseif operation == "audio_restore" then
            local settings = ui.audio_settings()
            if settings then audio_draft(draft, settings.defaults) end
        elseif operation == "audio_apply" then
            draft.audio_waiting = true
            ui.audio_apply(draft.audio_master / 100, draft.audio_effects / 100, draft.audio_music / 100)
        end
        draft.audio_error = ""
        audio_labels(self, ui)
    end

    local function quality_event(self, ui, operation, action)
        local draft = self.state
        local settings = ui.quality_settings()
        if not settings then return end
        if quality_presets[action] and operation == "quality_preset" then
            quality_draft(draft, resolve_preset(quality_presets[action], settings))
        elseif operation == "quality_msaa" then
            if draft.quality_temporal then
                draft.quality_temporal = false
                draft.quality_msaa = settings.supported_msaa[1]
            elseif draft.quality_msaa == settings.supported_msaa[#settings.supported_msaa] then
                draft.quality_temporal = true
                draft.quality_msaa = 1
            else
                draft.quality_msaa = cycle(draft.quality_msaa, settings.supported_msaa)
            end
        elseif operation == "quality_anisotropy" then
            local choices = {}
            for _, value in ipairs({1, 2, 4, 8, 16}) do
                if value <= settings.max_anisotropy_supported then choices[#choices + 1] = value end
            end
            draft.quality_anisotropy = cycle(draft.quality_anisotropy, choices)
        elseif operation == "quality_scale" then
            draft.quality_scale = cycle(draft.quality_scale,
                quality_options.render_scales or {0.5, 0.75, 1})
        elseif operation == "quality_restore" then
            quality_draft(draft, settings.defaults)
        elseif operation == "quality_apply" then
            draft.quality_error = ""
            draft.quality_waiting = true
            ui.quality_apply(draft.quality_msaa, draft.quality_anisotropy, draft.quality_scale,
                draft.quality_temporal)
        end
        quality_labels(self, ui, settings)
    end

    local function display_event(self, ui, operation, action)
        local draft = self.state
        if operation == "display_width" then
            draft.display_width = action
        elseif operation == "display_height" then
            draft.display_height = action
        elseif operation == "display_size" and #sizes > 0 then
            local index = 0
            for i, size in ipairs(sizes) do
                if tonumber(draft.display_width) == size[1]
                    and tonumber(draft.display_height) == size[2] then
                    index = i
                end
            end
            local size = sizes[index % #sizes + 1]
            display_size(draft, size[1], size[2])
        elseif operation == "display_mode" and not draft.display_preview then
            draft.display_mode = cycle(draft.display_mode, modes)
        elseif operation == "display_vsync" and not draft.display_preview then
            draft.display_vsync = not draft.display_vsync
        elseif operation == "display_limit" and not draft.display_preview then
            draft.display_limit = action
        elseif operation == "display_limit_preset" and not draft.display_preview then
            local limit = unsigned_integer(draft.display_limit, 1000)
            draft.display_limit = tostring(cycle(limit, frame_rates))
        elseif operation == "display_output_mode" and not draft.display_preview then
            draft.display_output_mode = cycle(draft.display_output_mode, output_modes)
        elseif operation == "display_hdr_headroom" or operation == "display_hdr_white" then
            if draft.display_preview or draft.display_output_mode == "sdr" then return end
            local value = tonumber(action)
            local minimum, maximum = 1, 16
            if operation == "display_hdr_white" then minimum, maximum = 50, 200 end
            if not value or value ~= value or value < minimum or value > maximum then return end
            draft[operation] = value
        elseif operation == "display_restore" then
            local current = ui.display_settings()
            if current then
                display_size(draft, current.defaults.width, current.defaults.height)
                if not draft.display_preview then
                    draft.display_mode, draft.display_vsync = current.defaults.mode, current.defaults.vsync
                    draft.display_limit = tostring(current.defaults.frame_rate_limit)
                    draft.display_output_mode = current.defaults.output_mode
                    draft.display_hdr_headroom = current.defaults.hdr_headroom
                    draft.display_hdr_white = current.defaults.hdr_white_level * 100
                end
            end
        elseif operation == "display_apply" then
            local width, height = dimension(draft.display_width), dimension(draft.display_height)
            if not width or not height then
                ui.set("display_error", "宽高必须是 1 到 2147483647 之间的整数。")
                return
            end
            local limit = unsigned_integer(draft.display_limit, 1000)
            if not limit then
                ui.set("display_error", "帧率上限必须是 0 到 1000 之间的整数，0 表示无上限。")
                return
            end
            display_size(draft, width, height)
            self.state.display_waiting = true
            ui.display_apply(width, height, draft.display_mode, draft.display_vsync,
                draft.display_output_mode, draft.display_hdr_headroom, draft.display_hdr_white / 100, limit)
        end
        display_labels(self, ui)
    end

    local controller = {
        model = {
            input_available = input_enabled, action_name = "", status_text = "", error_text = "",
            menu_available = false, waiting = false, has_actions = false,
            display_available = false, display_preview = false, display_waiting = false,
            display_size = "", display_mode = "", display_vsync = "",
            display_width = "960", display_height = "720", display_active_vsync = "",
            display_limit = "0", display_frame_rate = "",
            display_status = "", display_error = "",
            display_confirming = false, display_output_pending = false, display_confirmation_text = "",
            display_output_mode = "SDR", display_hdr_headroom = 4, display_hdr_white = 100,
            display_hdr_disabled = true, display_output_active = "",
            quality_available = false, quality_msaa = "", quality_anisotropy = "", quality_scale = "",
            quality_active = "", quality_status = "", quality_error = "",
            quality_preset = "",
            audio_available = false, audio_master = 100, audio_effects = 100, audio_music = 100,
            audio_active = "", audio_error = "",
        },
        state = {
            open = false, available = false, error_only = false, selected = 1,
            revision = -1, error = "", display_waiting = false,
            display_confirming = false, display_output_pending = false,
            quality_available = false, quality_waiting = false, quality_error = "",
            quality_msaa = 4, quality_anisotropy = 8, quality_scale = 1, quality_temporal = false,
            audio_available = false, audio_waiting = false, audio_error = "",
            audio_master = 100, audio_effects = 100, audio_music = 100,
            display_available = false, display_width = "960", display_height = "720",
            display_mode = "windowed", display_vsync = false, display_preview = false,
            display_limit = "0",
            display_output_mode = "sdr", display_hdr_headroom = 4, display_hdr_white = 100,
        },
        on_mount = function(self, ui)
            ui.set("input_available", input_enabled)
            if not options.display then self.state.display_available = false end
            if not options.quality then self.state.quality_available = false end
            if not options.audio then self.state.audio_available = false end
            local required = {elements.menu, elements.panel, elements.cancel}
            if elements.open then required[#required + 1] = elements.open end
            if input_enabled then
                for _, role in ipairs({"bindings", "apply", "restore"}) do
                    required[#required + 1] = elements[role]
                end
            end
            if options.display then required[#required + 1] = elements.confirm end
            for _, id in ipairs(required) do ui.require_element(id) end
            for _, id in ipairs(options.required_elements or {}) do ui.require_element(id) end
            self.state.revision = -1
            display_labels(self, ui)
            quality_labels(self, ui)
            audio_labels(self, ui)
            ui.set("menu_available", self.state.available)
            sync(self, ui)
            -- 验证隐藏菜单的布局和纹理；on_present 在发布后的首帧恢复项目状态。
            ui.property(elements.menu, "display", "block")
            if self.state.open then focus_menu(self, ui) end
        end,
        on_frame = function(self, ui, frame)
            self.state.available = frame.game_available
            ui.set("menu_available", frame.game_available)
            local active_vsync = "宿主未提供同步呈现状态。"
            if frame.vsync_active ~= nil then
                active_vsync = "当前同步呈现：关闭"
                if frame.vsync_active then active_vsync = "当前同步呈现：开启" end
            elseif self.state.display_preview then
                active_vsync = "Play 使用编辑器的呈现节奏。"
            end
            ui.set("display_active_vsync", active_vsync)
            if self.state.open then
                display_output_status(self, ui)
                quality_status(self, ui)
                audio_status(self, ui)
            end
            local status = ui.input_status()
            if not input_enabled and status.active then ui.input_end() end
            if not status.capturing and pressed(ui, "key", shortcuts.reload) then ui.reload() end
            if not self.state.open then
                if pressed(ui, "key", shortcuts.open)
                    or pressed(ui, "gamepad_button", shortcuts.gamepad_open) then
                    open(self, ui)
                end
            elseif not status.waiting and not status.capturing then
                if pressed(ui, "key", shortcuts.close)
                    or pressed(ui, "gamepad_button", shortcuts.gamepad_close) then
                    close(self, ui)
                end
            end
        end,
        on_event = function(self, ui, operation, action, binding)
            if operation == "open" then open(self, ui); return end
            if not self.state.open or applying(self.state, ui.input_status()) then return end
            if self.state.display_confirming then
                if operation == "display_confirm" then display_decision(self, ui, true)
                elseif operation == "display_revert" or operation == "cancel" then
                    display_decision(self, ui, false)
                end
                return
            end
            if self.state.error_only and operation ~= "cancel"
                and not operation:match("^display_") and not operation:match("^quality_")
                and not operation:match("^audio_") then
                return
            end
            if operation:match("^audio_") then
                if self.state.audio_available then audio_event(self, ui, operation, action) end
            elseif operation:match("^quality_") then
                if self.state.quality_available then quality_event(self, ui, operation, action) end
            elseif operation:match("^display_") then
                if self.state.display_available then display_event(self, ui, operation, action) end
            elseif operation == "cancel" then
                close(self, ui)
            elseif input_enabled and operation == "apply" then
                ui.input_apply()
            elseif input_enabled and operation == "restore" then
                ui.input_restore()
            elseif input_enabled and (operation == "previous" or operation == "next") then
                local count = #ui.input_actions()
                if count > 0 then
                    local step = 1
                    if operation == "previous" then step = -1 end
                    self.state.selected = (self.state.selected - 1 + step) % count + 1
                    self.state.revision = -1
                end
            elseif input_enabled and (operation == "key" or operation == "pad") then
                self.state.error = ""
                ui.input_capture(action, binding, operation)
            elseif input_enabled and operation == "toggle_binding" then
                ui.input_toggle_binding(action, binding)
            elseif input_enabled and operation == "restore_binding" then
                ui.input_restore_binding(action, binding)
            end
            sync(self, ui)
        end,
        on_present = function(self, ui)
            sync(self, ui)
            local status = ui.input_status()
            if self.state.open and not status.waiting and not status.capturing
                and not ui.has_focus(elements.panel) then
                focus_menu(self, ui)
            end
        end,
        on_input_result = function(self, ui, success)
            if success then close(self, ui) else sync(self, ui) end
        end,
        on_display_result = function(self, ui, success, message)
            self.state.display_waiting = false
            ui.set("display_error", message)
            if success then
                load_display(self, ui)
                local message = "显示设置已应用；确认保留后才会保存。"
                if not self.state.display_confirming then message = "显示设置已应用。" end
                ui.set("display_status", message)
            end
            sync(self, ui)
        end,
        on_quality_result = function(self, ui, success, message)
            self.state.quality_waiting = false
            self.state.quality_error = message
            quality_status(self, ui)
            sync(self, ui)
        end,
        on_audio_result = function(self, ui, success, message)
            self.state.audio_waiting = false
            self.state.audio_error = message
            audio_labels(self, ui)
            audio_status(self, ui)
            sync(self, ui)
        end,
        on_reload_error = function(self, ui, message)
            self.state.error = message
            sync(self, ui)
        end,
        on_deactivate = close,
    }

    for _, field in ipairs({"model", "state"}) do
        for name, value in pairs(options[field] or {}) do
            assert(controller[field][name] == nil, "ui.settings field already exists: " .. name)
            local kind = type(value)
            assert(kind == "boolean" or kind == "number" or kind == "string",
                "ui.settings " .. field .. " needs scalar values")
            controller[field][name] = value
        end
    end
    for _, name in ipairs({"on_mount", "on_frame", "on_present", "on_event", "on_input_result",
        "on_display_result", "on_quality_result", "on_audio_result", "on_reload_error",
        "on_deactivate", "on_destroy"}) do
        local hook = options[name]
        if hook then
            assert(type(hook) == "function", "ui.settings " .. name .. " must be a function")
            local base = controller[name]
            controller[name] = function(self, api, ...)
                if base then base(self, api, ...) end
                hook(self, api, ...)
            end
        end
    end
    return controller
end

return {settings = settings}
