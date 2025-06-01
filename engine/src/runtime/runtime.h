#pragma once

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        virtual void onInit() = 0;

        virtual void onUpdate() = 0;

        virtual void onShutdown() = 0;
    };

    void run(Application* app);
}

