#include <gtest/gtest.h>
#include "config/config.h"
#include "diagnostics/diagnostics.h"

#include <memory>

class CometTestEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        Comet::Config::Diagnostics diagnostics_config;
        diagnostics_config.log.enable_file_logging = false;
        diagnostics_config.log.level = "warn";
        diagnostics_config.enable_profiler = false;
        m_diagnostics = std::make_unique<Comet::Diagnostics>(diagnostics_config);
    }

    void TearDown() override { m_diagnostics.reset(); }

private:
    std::unique_ptr<Comet::Diagnostics> m_diagnostics;
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    ::testing::AddGlobalTestEnvironment(new CometTestEnvironment);
    return RUN_ALL_TESTS();
}
