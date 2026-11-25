#include "fixture_files.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <limits>
#include <qt/application/session/session_preferences.hpp>

namespace {

using namespace wiola;
using namespace wiola::testing::compatibility;

TEST(CompatibilitySession, ReadsDocumentIdentityQueryAndFullWidthOccurrenceTokens)
{
    app::SessionState state;
    ASSERT_EQ(app::read_session_state(fixture("session/current-v1.json"), state),
        app::SessionStateError::none);
    ASSERT_EQ(state.open.size(), 3U);
    EXPECT_EQ(state.open[1], gui::DocumentId::playlist(library::PlaylistId{"saved"}));
    EXPECT_EQ(state.visible, state.open[1]);
    EXPECT_EQ(state.destination, library::PlaylistId{"saved"});
    EXPECT_EQ(state.window, QRect(-100, 20, 1200, 800));
    const auto& browser = state.browsers.at(gui::DocumentId::queue());
    EXPECT_EQ(browser.view.query.artist, "Бах");
    ASSERT_EQ(browser.view.query.filters.size(), 1U);
    EXPECT_EQ(browser.view.query.filters[0].value, "Calm");
    ASSERT_EQ(browser.selected.size(), 2U);
    EXPECT_EQ(std::get<engine::QueueEntryId>(browser.selected[0]).value,
        std::numeric_limits<std::uint64_t>::max() - 1);
    EXPECT_EQ(browser.current, gui::TrackRowId{engine::QueueEntryId{7}});
    EXPECT_EQ(browser.anchor_offset, -5);
    ASSERT_EQ(browser.view.sort_keys.size(), 1U);
    EXPECT_EQ(browser.view.sort_keys[0].tag_key, "mood");
}

TEST(CompatibilitySession, RejectedInputsKeepAcceptedState)
{
    app::SessionState state;
    ASSERT_EQ(app::read_session_state(fixture("session/current-v1.json"), state),
        app::SessionStateError::none);
    const auto before = state;
    for (const auto& [path, error] : {
             std::pair{"session/invalid-v1.json", app::SessionStateError::invalid            },
             std::pair{"session/future.json",     app::SessionStateError::unsupported_version},
             std::pair{"malformed.json",          app::SessionStateError::invalid            }
    }) {
        EXPECT_EQ(app::read_session_state(fixture(path), state), error);
        EXPECT_EQ(state, before);
    }
}

TEST(CompatibilitySession, FutureStateSurvivesEditFlushAndDestructionByteForByte)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("session.json");
    const auto future = fixture("session/future.json");
    overwrite(path, future);
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    {
        app::SessionPreferences preferences{settings};
        EXPECT_EQ(preferences.restore_error(), app::SessionStateError::unsupported_version);
        preferences.edit([](auto& state) { state.layout_identity = "custom"; });
        EXPECT_EQ(preferences.flush(), app::SessionStateError::unsupported_version);
    }
    EXPECT_EQ(contents(path), future);
}

} // namespace
