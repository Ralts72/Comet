#include "runtime/entry.h"

class GameApp final: public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("app init");
    }

    void on_update(Comet::UpdateContext context) override {
        LOG_INFO("app update");
    }

    void on_shutdown() override {
        LOG_INFO("app shutdown");
    }
};

RUN_APP(GameApp)
