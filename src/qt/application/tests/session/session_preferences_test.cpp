/**
 * @file
 * @brief Unit tests for the queue a run leaves behind.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <media/codecs/ffmpeg/composition.hpp>
#include <memory>
#include <qt/application/library/library_session.hpp>
#include <qt/application/session/session_preferences.hpp>
#include <qt/paths.hpp>
#include <qt/tests/support/atomic_output.hpp>
#include <stdexcept>

namespace {

using namespace wiola;

QByteArray contents(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void write(const QString& path, QByteArrayView bytes)
{
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes.data(), bytes.size()), bytes.size());
}

using FaultOutput = wiola::testing::FaultAtomicOutput;

TEST(SessionState, RoundTripsDistinctDocumentsOccurrencesAndBrowserPreferencesWithoutPrecisionLoss)
{
    app::SessionState original;
    const auto playlist = gui::DocumentId::playlist(library::PlaylistId{"playlist"}),
               workspace = gui::DocumentId::workspace(library::WorkspaceId{"folder"});
    original.documents.open = {gui::DocumentId::queue(), playlist, workspace};
    original.documents.visible = workspace;
    original.documents.destination = library::PlaylistId{"destination"};
    original.window = QRect{-100, 20, 1200, 800};
    original.splitters = {
        {"documentArea", {180, 620, 220}}
    };
    original.layout_identity = "custom";
    gui::BrowserViewState browser;
    browser.view.query = {
        .title = "Song",
        .artist = "Artist",
        .album = "Album",
        .tags = "Calm",
        .filters = {{.key = "mood", .value = "calm", .mode = library::TagFilterMode::include},
                    {.key = "source", .value = "live", .mode = library::TagFilterMode::exclude}}
    };
    browser.view.sort_keys = {
        {.field = gui::TrackSortField::custom_tag,
         .direction = gui::TrackSortDirection::descending,
         .tag_key = "mood"}
    };
    browser.columns = {
        {gui::StandardTrackColumn::title, {211, 0, true} },
        {gui::CustomTagColumn{"mood"},    {233, 1, false}}
    };
    browser.selected = {engine::QueueEntryId{UINT64_MAX - 1}, engine::QueueEntryId{88}};
    browser.current = browser.selected.front();
    browser.anchor = browser.selected.back();
    browser.anchor_offset = -5;
    original.documents.browsers.emplace(gui::DocumentId::queue(), browser);
    browser.selected = {library::PlaylistEntryId{"first"}, library::PlaylistEntryId{"second"}};
    browser.current = browser.selected.back();
    browser.anchor = browser.selected.front();
    original.documents.browsers.emplace(playlist, browser);
    browser.selected = {
        gui::WorkspaceTrackId{library::WorkspaceId{"folder"},
                              library::MediaId{"local:music/Unicode-楽曲.flac"}}
    };
    browser.current = browser.selected.front();
    browser.anchor = browser.current;
    original.documents.browsers.emplace(workspace, browser);
    const auto bytes = app::write_session_state(original);
    ASSERT_FALSE(bytes.isEmpty());
    app::SessionState restored;
    ASSERT_EQ(app::read_session_state(bytes, restored), app::SessionStateError::none);
    EXPECT_EQ(restored, original);
    const auto before = restored;
    EXPECT_EQ(app::read_session_state(R"({"version":99})", restored),
        app::SessionStateError::unsupported_version);
    EXPECT_EQ(restored, before);
    EXPECT_EQ(app::read_session_state(QByteArray(2 * 1024 * 1024 + 1, ' '), restored),
        app::SessionStateError::too_large);
    EXPECT_EQ(restored, before);
    original.documents.browsers.begin()->second.selected.push_back(
        original.documents.browsers.begin()->second.selected.front()
    );
    EXPECT_TRUE(app::write_session_state(original).isEmpty());
}

TEST(SessionPreferences, DurableMigrationRetainsLegacyKeysUntilTheWriteSucceeds)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    settings.setValue("documents/open_playlists", QStringList{"saved"});
    settings.setValue("documents/current_kind", "playlist");
    settings.setValue("documents/current_id", "saved");
    settings.setValue("playlists/active", "destination");
    settings.sync();
    bool fail = true;
    app::SessionPreferences preferences{settings,
        [&](const auto& path) { return std::make_unique<FaultOutput>(path, fail); }};
    EXPECT_EQ(preferences.state().documents.visible,
        gui::DocumentId::playlist(library::PlaylistId{"saved"}));
    EXPECT_EQ(preferences.state().documents.destination, library::PlaylistId{"destination"});
    EXPECT_EQ(preferences.flush(), app::SessionStateError::io);
    settings.sync();
    EXPECT_TRUE(settings.contains("documents/current_id"));
    EXPECT_TRUE(settings.contains("playlists/active"));
    fail = false;
    ASSERT_EQ(preferences.flush(), app::SessionStateError::none);
    settings.sync();
    EXPECT_FALSE(settings.contains("documents/current_id"));
    EXPECT_FALSE(settings.contains("playlists/active"));
    app::SessionPreferences restored{settings};
    EXPECT_EQ(restored.state(), preferences.state());
}

TEST(SessionPreferences, FutureSchemaIsPreservedAndReentrantEditsStayPendingForAnotherFlush)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    bool fail{}, armed{};
    app::SessionPreferences* owner{};
    app::SessionPreferences preferences{settings,
        [&](const auto& path) {
            return std::make_unique<FaultOutput>(path, fail, [&] {
                if (armed) {
                    armed = false;
                    owner->edit([](auto& state) { state.window = QRect{4, 5, 900, 600}; });
                    EXPECT_EQ(owner->flush(), app::SessionStateError::busy);
                }
            });
        }};
    owner = &preferences;
    preferences.edit([](auto& state) { state.layout_identity = "classic"; });
    armed = true;
    ASSERT_EQ(preferences.flush(), app::SessionStateError::none);
    ASSERT_EQ(preferences.flush(), app::SessionStateError::none);
    app::SessionPreferences restored{settings};
    EXPECT_EQ(restored.state().window, QRect(4, 5, 900, 600));
    const QByteArray future = R"({"version":99,"data":"unchanged"})";
    write(directory.filePath("session.json"), future);
    app::SessionPreferences newer{settings};
    newer.edit([](auto& state) { state.layout_identity = "custom"; });
    EXPECT_EQ(newer.flush(), app::SessionStateError::unsupported_version);
    EXPECT_EQ(contents(directory.filePath("session.json")), future);
}

TEST(SessionPreferences, DestinationRestoresAndTracksLibraryChangesWithoutAnyWindow)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    app::LibrarySession library{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")),
        wiola::codec::ffmpeg::make_provider()};
    const auto first = library.playlists().create("First").id,
               second = library.playlists().create("Second").id;
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    app::SessionPreferences preferences{settings};
    preferences.edit([&](auto& state) { state.documents.destination = first; });
    preferences.bind_library(library);
    EXPECT_EQ(library.playlist_destination().id(), first);
    preferences.bind_library(library);
    ASSERT_TRUE(library.playlist_destination().set(second));
    EXPECT_EQ(preferences.state().documents.destination, second);
    ASSERT_TRUE(library.playlists().remove(second));
    EXPECT_FALSE(preferences.state().documents.destination);
    EXPECT_NE(library.repository().playlist(first), nullptr);
    EXPECT_EQ(preferences.flush(), app::SessionStateError::none);
}

TEST(SessionPreferences, QueueRestorationUsesTheExplicitOccurrenceMapAndDropsUnknownEntries)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    app::SessionPreferences preferences{settings};
    preferences.edit([](auto& state) {
        auto& browser = state.documents.browsers[gui::DocumentId::queue()];
        browser.selected = {engine::QueueEntryId{11}, engine::QueueEntryId{12},
            engine::QueueEntryId{13}};
        browser.current = engine::QueueEntryId{13};
        browser.anchor = engine::QueueEntryId{12};
    });
    preferences.remap_queue({
        {engine::QueueEntryId{11}, engine::QueueEntryId{91}},
        {engine::QueueEntryId{13}, engine::QueueEntryId{92}}
    });
    const auto& browser = preferences.state().documents.browsers.at(gui::DocumentId::queue());
    EXPECT_EQ(browser.selected,
        (std::vector<gui::TrackRowId>{engine::QueueEntryId{91}, engine::QueueEntryId{92}}));
    EXPECT_EQ(browser.current, gui::TrackRowId{engine::QueueEntryId{92}});
    EXPECT_FALSE(browser.anchor);
}

} // namespace

TEST(SessionPreferences, EditCallbackMayReleaseThePreferenceOwner)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    auto preferences = std::make_unique<app::SessionPreferences>(settings);
    preferences->edit([&](auto& state) {
        state.layout_identity = "classic";
        preferences.reset();
    });
    EXPECT_FALSE(preferences);
}

TEST(SessionPreferences, SaveFailureObserverMayReleaseThePreferenceOwner)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    bool fail = true;
    auto preferences = std::make_unique<app::SessionPreferences>(settings,
        [&](const auto& path) { return std::make_unique<FaultOutput>(path, fail); });
    QObject receiver;
    QObject::connect(preferences.get(), &app::SessionPreferences::save_failed, &receiver,
        [&](auto) { preferences.reset(); });
    preferences->edit([](auto& state) { state.layout_identity = "classic"; });
    EXPECT_EQ(preferences->flush(), app::SessionStateError::io);
    EXPECT_FALSE(preferences);
}

TEST(SessionPreferences, AStaleEditCannotOverwriteAnAcceptedReentrantEdit)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    app::SessionPreferences preferences{settings};
    EXPECT_THROW(preferences.edit([&](auto& state) {
        state.layout_identity = "classic";
        preferences.edit([](auto& newer) { newer.layout_identity = "custom"; });
    }),
        std::logic_error);
    EXPECT_EQ(preferences.state().layout_identity, "custom");
    ASSERT_EQ(preferences.flush(), app::SessionStateError::none);
    app::SessionPreferences restored{settings};
    EXPECT_EQ(restored.state().layout_identity, "custom");
}

int main(int argc, char** argv)
{
    QCoreApplication application{argc, argv};
    QTemporaryDir isolated;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
