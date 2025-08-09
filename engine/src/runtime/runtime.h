#pragma once

namespace Comet {
    class Application {
    public:
        virtual ~Application() = default;

        virtual void on_init() = 0;

        virtual void on_update() = 0;

        virtual void on_shutdown() = 0;
    };

    void run(Application* app) {
	}
}

