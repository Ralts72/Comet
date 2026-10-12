-- Engine 管理设置草稿、录入、应用和焦点；页面与游戏策略留在项目中。
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
    model = {fps_text = "0 FPS"},
    required_elements = {"hud", "fps", "notice"},
    on_frame = function(self, api, frame)
        local fps = math.floor(math.max(0, math.min(frame.fps, 100000)) + 0.5)
        api.set("fps_text", tostring(fps) .. " FPS")
    end,
}
