#include "./runtime/entry.h"

class App final: public Comet::Application {
public:
    void onInit() override {
        std::cout << "App init" << std::endl;
    }

    void onUpdate() override {
        std::cout << "App update" << std::endl;
    }

    void onShutdown() override {
        std::cout << "App shutdown" << std::endl;
    }
};

RUN_APP(App)
