#include "fixture_files.hpp"

#include <qt/widgets/appearance/theme_manager.hpp>

#include <gtest/gtest.h>

namespace {

using namespace wiola;
using namespace wiola::testing::compatibility;

TEST(CompatibilityTheme, ReadsIndependentlySpecifiedSemanticColors)
{
    gui::ThemeState state;

    ASSERT_EQ(gui::read_theme_json(fixture("theme/current-v1.json"), state), gui::ThemeError::none);
    EXPECT_EQ(state.selected, gui::ThemeChoice::custom);
    EXPECT_EQ(state.custom.window, QColor("#ffffff"));
    EXPECT_EQ(state.custom.text, QColor("#000000"));
    EXPECT_EQ(state.custom.accent, QColor("#000080"));
    EXPECT_TRUE(gui::theme_contrast_error(state.custom).isEmpty());
}

TEST(CompatibilityTheme, RejectedDefinitionsDoNotReplaceAcceptedColors)
{
    gui::ThemeState state;

    ASSERT_EQ(gui::read_theme_json(fixture("theme/current-v1.json"), state), gui::ThemeError::none);

    const auto accepted = state;

    for (const auto& [path, error] : {
             std::pair{"theme/invalid-v1.json", gui::ThemeError::invalid            },
             std::pair{"theme/future.json",     gui::ThemeError::unsupported_version},
             std::pair{"malformed.json",        gui::ThemeError::invalid            }
    }) {
        EXPECT_EQ(gui::read_theme_json(fixture(path), state), error);
        EXPECT_EQ(state, accepted);
    }
}

} // namespace
