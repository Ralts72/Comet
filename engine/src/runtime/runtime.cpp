#include "runtime.h"

namespace Comet {
    void run(Application* app) {
        // try {
        //     std::cout << "[Runtime] Init logger\n";
        //     std::cout << "[Runtime] App starting...\n";
        //
        //     app->OnInit();
        //
        //     const int maxFrames = 3;
        //     for (int frame = 0; frame < maxFrames; ++frame) {
        //         std::cout << "[Runtime] Frame " << frame << "\n";
        //         app->OnUpdate();
        //         std::this_thread::sleep_for(std::chrono::milliseconds(16));
        //     }
        //
        //     app->OnShutdown();
        //     std::cout << "[Runtime] Clean shutdown.\n";
        //
        // } catch (const std::exception& e) {
        //     std::cerr << "[Runtime] Unhandled exception: " << e.what() << "\n";
        // }
    }
}
