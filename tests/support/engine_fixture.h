#pragma once

#include "config/config.h"
#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/render_context.h"
#include "render/renderer.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <algorithm>
#include <memory>
#include <sstream>

namespace Comet::Tests {
    class EngineTest: public testing::Test {
    protected:
        void SetUp() override {
            m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(m_sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            engine = std::make_unique<Engine>(config);
        }

        void TearDown() override {
            if(engine)
                engine->get_renderer().get_render_context().wait_idle();
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), m_sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
        }

        std::unique_ptr<Engine> engine;
        std::ostringstream messages;

    private:
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
    };
}
