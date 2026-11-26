#include "fixture_files.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <limits>
#include <qt/application/session/session_preferences.hpp>
#include <qt/paths.hpp>
#include <wiola/qt/storage/settings.hpp>

namespace {

using namespace wiola;
using namespace wiola::testing::compatibility;

TEST(CompatibilitySettings, ReadsLegacyDefaultsUnicodePathsAndDuplicateQueueEntries)
{
    QSettings settings{fixture_path("settings/legacy.ini"), QSettings::IniFormat};
    platform::qt::QtSettingsAdapter adapter{settings};
    app::VolumeScale volume{.full_position = 160};
    audio::EqualizerConfig equalizer;
    equalizer.layout.count = 2;
    const auto player = adapter.read(volume, equalizer);
    EXPECT_EQ(player.volume_position, 160);
    EXPECT_TRUE(player.boosted);
    EXPECT_FALSE(player.equalizer_enabled);
    ASSERT_EQ(player.band_gains.size(), 2U);
    EXPECT_FLOAT_EQ(player.band_gains[0], 3.5F);
    EXPECT_FLOAT_EQ(player.band_gains[1], 0.0F);
    const auto queue = adapter.read_queue();
    ASSERT_EQ(queue.tracks.size(), 3U);
    EXPECT_EQ(queue.tracks[0], queue.tracks[1]);
    EXPECT_EQ(platform::qt::QtPathAdapter::to_qt(queue.tracks[0]),
        QString::fromUtf8("Music/楽曲.wav"));
    EXPECT_EQ(queue.repeat, engine::RepeatMode::all);
    EXPECT_TRUE(queue.shuffled);
    EXPECT_FALSE(queue.has_occurrence_ids);
    EXPECT_FALSE(queue.selected_occurrence);
}

TEST(CompatibilitySettings, KeepsFullWidthIdsAndRejectsAmbiguousDuplicateTokens)
{
    QSettings valid{fixture_path("settings/occurrences.ini"), QSettings::IniFormat};
    const auto queue = platform::qt::QtSettingsAdapter{valid}.read_queue();
    ASSERT_EQ(queue.occurrence_ids.size(), 3U);
    EXPECT_EQ(queue.occurrence_ids[0], std::numeric_limits<std::uint64_t>::max() - 1);
    EXPECT_EQ(queue.selected_occurrence, 7U);
    QSettings invalid{fixture_path("settings/invalid-occurrences.ini"), QSettings::IniFormat};
    const auto ambiguous = platform::qt::QtSettingsAdapter{invalid}.read_queue();
    EXPECT_TRUE(ambiguous.has_occurrence_ids);
    EXPECT_TRUE(ambiguous.occurrence_ids.empty());
    EXPECT_FALSE(ambiguous.selected_occurrence);
}

TEST(CompatibilitySettings, MigratesLegacyDocumentsWithoutTouchingTheInputCorpus)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("settings.ini");
    const auto legacy = fixture("settings/legacy.ini");
    overwrite(path, legacy);
    QSettings settings{path, QSettings::IniFormat};
    {
        app::SessionPreferences preferences{settings};
        EXPECT_EQ(preferences.state().documents.visible,
            gui::DocumentId::playlist(library::PlaylistId{"saved"}));
        EXPECT_EQ(preferences.state().documents.destination, library::PlaylistId{"saved"});
        ASSERT_EQ(preferences.state().documents.open.size(), 3U);
        ASSERT_EQ(preferences.flush(), app::SessionStateError::none);
    }
    app::SessionPreferences restored{settings};
    EXPECT_EQ(restored.state().documents.destination, library::PlaylistId{"saved"});
    EXPECT_EQ(fixture("settings/legacy.ini"), legacy);
}

} // namespace
