#include "./runtime/entry.h"

class GameApp final: public Comet::Application {
public:
    void on_init() override {}

    void on_update() override {}

    void on_shutdown() override {}
};

RUN_APP(GameApp)
