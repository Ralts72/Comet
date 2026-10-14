#pragma once

#include "common/result.h"
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace CometEditor {
    struct AppExportRequest {
        std::filesystem::path project;
        std::filesystem::path destination;
        std::vector<std::filesystem::path> scenes;
    };

    // 在后台调用；使用参数数组启动打包工具，不经过 shell。
    Comet::Result<void> export_app(const AppExportRequest& request,
        const std::function<void(std::string_view)>& on_output = {});

    class AppExportTask {
    public:
        void start(AppExportRequest request);
        std::string take_output();
        std::optional<Comet::Result<void>> take_result();

    private:
        std::mutex m_mutex;
        std::string m_output;
        // 最先析构，等待工作结束后再释放回调使用的状态。
        std::future<Comet::Result<void>> m_future;
    };
}
