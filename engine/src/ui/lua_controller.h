#pragma once

#include "common/result.h"
#include <RmlUi/Core/Variant.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

struct lua_State;

namespace Comet::Ui::Detail {
    // 受控项目 UI VM；不持有文档、输入事务或宿主对象。
    class LuaController final {
    public:
        using Functions = std::vector<std::pair<const char*, int>>;
        using Callback = std::function<int(lua_State*, int)>;
        using Model = std::map<std::string, Rml::Variant>;
        struct FrameInfo {
            float fps;
            bool game_available;
            bool focused;
            std::optional<bool> vsync_active;
        };
        LuaController(Functions functions, Callback callback);
        ~LuaController();
        Result<void> load(const std::filesystem::path& path);
        Result<void> call(const char* method, const Rml::VariantList& arguments = {});
        Result<void> frame(FrameInfo info);
        Result<void> copy_state(const LuaController& previous);
        Model& model();
        static bool scalar(lua_State* state, int index, Rml::Variant& result);

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
