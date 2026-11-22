// Exercise the console implementation: the host preview never calls Piglet.
#include <gtest/gtest.h>
#include "platform/ps4/display_egl.cpp"

namespace {
bool configuration_result = false;
ScePglConfig last_configuration{};
int configuration_calls = 0;
}

extern "C" bool scePigletSetConfigurationVSH(const ScePglConfig *config)
{
    ++configuration_calls;
    last_configuration = *config;
    return configuration_result;
}

TEST(PigletConfiguration, TrueMeansSuccess)
{
    piglet_configured = false; configuration_calls = 0;
    configuration_result = true;
    EXPECT_TRUE(configure_piglet(1920, 1080));
    EXPECT_EQ(last_configuration.size, 0x88u);
    EXPECT_EQ(last_configuration.flags, 0xe8u);
    EXPECT_EQ(last_configuration.dbg_pos_cmd_0x40, 1920u);
    EXPECT_EQ(last_configuration.dbg_pos_cmd_0x44, 1080u);
    EXPECT_TRUE(configure_piglet(1920, 1080));
    EXPECT_EQ(configuration_calls, 1);
}

TEST(PigletConfiguration, FalseStopsDisplayInitialization)
{
    piglet_configured = false;
    configuration_result = false;
    EXPECT_FALSE(configure_piglet(1280, 720));
}
