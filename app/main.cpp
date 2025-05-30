#include "./runtime/entry.h"

class App final: public Comet::Application {
public:
    void onInit() override {}
    void onUpdate() override {}
    void onShutdown() override {}
};

RUN_APP(App)
