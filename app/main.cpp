#include "runtime/entry.h"
#include "common/log_system/log_system.h"

class GameApp final: public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("app init");
    }

    void on_update() override {
        LOG_INFO("app update");
    }

    void on_shutdown() override {
        LOG_INFO("app shutdown");
    }
};

RUN_APP(GameApp)
