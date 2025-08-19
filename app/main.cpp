#include "runtime/entry.h"

class GameApp final: public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("app init");
        set_settings({1000,800, "Comet Game"});
    }

    void on_update(Comet::UpdateContext context) override {
        LOG_INFO("delta time is {}", context.deltaTime);
        LOG_DEBUG("fps is {}", context.fps);
        LOG_INFO("app update");
    }

    void on_shutdown() override {
        LOG_INFO("app shutdown");
    }
};
RUN_APP(GameApp)
