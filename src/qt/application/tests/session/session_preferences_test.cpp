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

#include <qt/application/library/library_session.hpp>
#include <qt/application/session/session_preferences.hpp>
#include <qt/tests/support/atomic_output.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/qt/paths.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <memory>
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

TEST(SessionState, LastOpenedPlaylistIsOptionalAndRoundTripsWithoutChangingLegacySessionFields)
{
    wiola::app::SessionState state;
    const auto legacy = wiola::app::write_session_state(state);

    EXPECT_FALSE(legacy.contains("lastPlaylist"));

    wiola::app::SessionState restored;

    ASSERT_EQ(wiola::app::read_session_state(legacy, restored),
        wiola::app::SessionStateError::none);
    EXPECT_FALSE(restored.documents.last_playlist);

    state.documents.last_playlist = wiola::library::PlaylistId{"last-opened"};

    const auto encoded = wiola::app::write_session_state(state);

    ASSERT_EQ(wiola::app::read_session_state(encoded, restored),
        wiola::app::SessionStateError::none);
    EXPECT_EQ(restored, state);

    for (const QByteArray invalid : {QByteArray{"true"}, QByteArray{"42"}, QByteArray{"\"\""}}) {
        auto malformed = encoded;
        malformed.replace("\"last-opened\"", invalid);
        EXPECT_EQ(wiola::app::read_session_state(malformed, restored),
            wiola::app::SessionStateError::invalid);
        EXPECT_EQ(restored, state);
    }
}

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

TEST(SessionState, MalformedPersistedValuesNeverPartiallyReplaceTheUsableSession)
{
    app::SessionState original;
    original.window = QRect{10, 20, 900, 600};
    original.layout_identity = "custom";
    original.splitters = {
        {"panes", {100, 200}}
    };
    auto& browser = original.documents.browsers[gui::DocumentId::queue()];
    browser.selected = {engine::QueueEntryId{1}};
    browser.current = browser.selected.front();
    const auto valid = QJsonDocument::fromJson(app::write_session_state(original)).object();

    const auto reject = [&](QJsonObject candidate) {
        auto restored = original;
        EXPECT_EQ(app::read_session_state(QJsonDocument{candidate}.toJson(), restored),
            app::SessionStateError::invalid);
        EXPECT_EQ(restored, original);
    };
    const auto root_value = [&](const QString& key, QJsonValue value) {
        auto candidate = valid;
        candidate[key] = value;
        reject(candidate);
    };
    const QJsonObject queue{
        {"queue", true}
    };
    for (const auto& [key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"version", "1"},
             {"version", 1.5},
             {"unknown", true},
             {"open", false},
             {"open", QJsonArray{false}},
             {"open", QJsonArray{QJsonObject{}}},
             {"open", QJsonArray{QJsonObject{{"queue", true}, {"playlist", "saved"}}}},
             {"open", QJsonArray{QJsonObject{{"queue", false}}}},
             {"visible", QJsonObject{{"unknown", "id"}}},
             {"destination", ""},
             {"destination", QString(129, 'a')},
             {"browsers", QJsonArray{false}},
             {"window", false},
             {"window", QJsonArray{1, 2}},
             {"window", QJsonArray{0, 0, -1, 300}},
             {"window", QJsonArray{0, 0, "wide", 300}},
             {"window", QJsonArray{0, 0, 1.5, 300}},
             {"splitters", QJsonArray{false}},
             {"splitters", QJsonArray{QJsonObject{{"id", "panes"}, {"sizes", false}}}},
             {"splitters", QJsonArray{QJsonObject{{"id", "panes"}, {"sizes", QJsonArray{-1}}}}},
             {"splitters",
              QJsonArray{QJsonObject{{"id", "panes"}, {"sizes", QJsonArray{1}}},
                     QJsonObject{{"id", "panes"}, {"sizes", QJsonArray{2}}}}},
             {"layout", QString(65, 'a')},
             {"layout", QString(QChar{0})}
    }) {
        SCOPED_TRACE(key.toStdString());
        root_value(key, value);
    }

    const auto held = valid["browsers"].toArray().first().toObject()["state"].toObject();
    const auto browser_value = [&](QJsonValue state) {
        root_value("browsers",
            QJsonArray{
                QJsonObject{{"document", queue}, {"state", state}}
        });
    };
    browser_value(false);
    for (const auto& [key, value] : std::vector<std::pair<QString, QJsonValue>>{
             {"query",    false                                                                   },
             {"unknown",  true                                                                    },
             {"selected", false                                                                   },
             {"selected", QJsonArray{false}                                                       },
             {"selected", QJsonArray{QJsonObject{{"queue", "0"}}}                                 },
             {"selected", QJsonArray{QJsonObject{{"queue", "01"}}}                                },
             {"selected", QJsonArray{QJsonObject{{"queue", "18446744073709551616"}}}              },
             {"selected", QJsonArray{QJsonObject{{"workspace", ""}, {"media", "song"}}}           },
             {"current",  QJsonObject{{"entry", ""}}                                              },
             {"anchor",   QJsonArray{}                                                            },
             {"sort",     QJsonArray{false}                                                       },
             {"sort",     QJsonArray{QJsonObject{{"unknown", true}}}                              },
             {"sort",
              QJsonArray{
                     QJsonObject{{"field", "title"}, {"direction", "sideways"}, {"tag", ""}}}     },
             {"sort",
              QJsonArray{
                     QJsonObject{{"field", "title"}, {"direction", "ascending"}, {"tag", "mood"}}}},
             {"columns",  QJsonArray{false}                                                       },
             {"columns",  QJsonArray{QJsonObject{{"id", false}}}                                  },
             {"columns",  QJsonArray{QJsonObject{{"id", QJsonObject{}}}}                          },
             {"columns",  QJsonArray{QJsonObject{{"id", QJsonObject{{"standard", "unknown"}}}}}   },
             {"offset",   "zero"                                                                  },
             {"offset",   1000001                                                                 }
    }) {
        SCOPED_TRACE(key.toStdString());
        auto candidate = held;
        candidate[key] = value;
        browser_value(candidate);
    }

    for (const auto& [key, value] :
        std::vector<std::pair<QString, QJsonValue>>{
            {"unknown", "value"                                                            },
            {"title",   12                                                                 },
            {"filters", false                                                              },
            {"filters", QJsonArray{false}                                                  },
            {"filters",
             QJsonArray{QJsonObject{{"key", ""}, {"value", "calm"}, {"mode", "include"}}}  },
            {"filters",
             QJsonArray{QJsonObject{{"key", "mood"}, {"value", "calm"}, {"mode", "other"}}}}
    }) {
        auto query = held["query"].toObject();
        query[key] = value;
        auto candidate = held;
        candidate["query"] = query;
        browser_value(candidate);
    }

    const QJsonObject column{
        {"id",      QJsonObject{{"standard", "title"}}},
        {"width",   100                               },
        {"order",   0                                 },
        {"visible", true                              }
    };
    auto same_id = column;
    same_id["order"] = 1;
    auto same_order = column;
    same_order["id"] = QJsonObject{
        {"standard", "artist"}
    };
    for (const auto& columns : {
             QJsonArray{column, same_id   },
             QJsonArray{column, same_order}
    }) {
        auto candidate = held;
        candidate["columns"] = columns;
        browser_value(candidate);
    }
}

TEST(SessionState, WriterRejectsOversizedCollectionsWithoutProducingPartialJson)
{
    app::SessionState state;
    state.documents.open.assign(129, gui::DocumentId::queue());
    EXPECT_TRUE(app::write_session_state(state).isEmpty());

    state.documents.open.clear();
    auto& browser = state.documents.browsers[gui::DocumentId::queue()];
    browser.view.query.filters.resize(129);
    EXPECT_TRUE(app::write_session_state(state).isEmpty());
}

TEST(SessionPreferences, LegacyWorkspaceAndEmptyNavigationMigrateWithoutInventingAnActiveDocument)
{
    for (const auto& kind : {QString{"workspace"}, QString{"empty"}, QString{"legacy-workspace"},
             QString{"legacy-playlist"}}) {
        QTemporaryDir directory;
        QSettings legacy{directory.filePath("settings.ini"), QSettings::IniFormat};
        legacy.setValue("documents/open_workspaces", QStringList{"", "folder"});
        if (kind == "workspace" || kind == "empty") {
            legacy.setValue("documents/current_kind", kind);
            legacy.setValue("documents/current_id", "folder");
        } else
            legacy.setValue(kind == "legacy-workspace" ? "documents/current_workspace"
                                                       : "documents/current_playlist",
                "folder");
        app::SessionPreferences preferences{legacy};
        if (kind == "empty")
            EXPECT_FALSE(preferences.state().documents.visible);
        else if (kind == "legacy-playlist")
            EXPECT_EQ(preferences.state().documents.visible,
                gui::DocumentId::playlist(library::PlaylistId{"folder"}));
        else
            EXPECT_EQ(preferences.state().documents.visible,
                gui::DocumentId::workspace(library::WorkspaceId{"folder"}));
        EXPECT_EQ(preferences.state().documents.open.back(),
            gui::DocumentId::workspace(library::WorkspaceId{"folder"}));
        EXPECT_EQ(preferences.flush(), app::SessionStateError::none);
    }
}

TEST(SessionPreferences, InvalidPendingStateAndConflictingLibraryOwnershipAreReported)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("settings.ini"), QSettings::IniFormat};
    app::SessionPreferences preferences{settings};
    app::LibrarySession first{platform::qt::QtPathAdapter::from_qt(directory.filePath("one.json")),
        codec::ffmpeg::make_provider()};
    app::LibrarySession second{platform::qt::QtPathAdapter::from_qt(directory.filePath("two.json")),
        codec::ffmpeg::make_provider()};
    const auto destination = first.playlists().create("Destination").id;
    ASSERT_TRUE(first.playlist_destination().set(destination));
    preferences.edit([](auto& state) {
        state.documents.destination = library::PlaylistId{"removed"};
    });
    preferences.bind_library(first);
    EXPECT_FALSE(first.playlist_destination().id());
    EXPECT_FALSE(preferences.state().documents.destination);
    EXPECT_THROW(preferences.bind_library(second), std::logic_error);
    preferences.edit([](auto& state) {
        state.documents.open.assign(129, gui::DocumentId::queue());
    });
    EXPECT_EQ(preferences.flush(), app::SessionStateError::invalid);
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
