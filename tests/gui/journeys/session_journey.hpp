#pragma once
#include <QEventLoop>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QTimer>
#include <codec/provider.hpp>
#include <desktop/player_application.hpp>
#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <gtest/gtest.h>
#include <gui/documents/now_playing_document.hpp>
#include <gui/documents/saved_playlist_document.hpp>
#include <gui/documents/workspace_document.hpp>
#include <gui/shell/document_host.hpp>
#include <gui/shell/main_window.hpp>
#include <gui/tracks/track_browser.hpp>
#include <library/location.hpp>
#include <platform/qt/qt_path_adapter.hpp>

namespace wiola::testing {

inline void restore_session_journey()
{
    using namespace wiola;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto music = platform::qt::QtPathAdapter::from_qt(directory.filePath("music"));
    std::filesystem::create_directory(music);
    const auto source = platform::qt::QtPathAdapter::from_qt(
        platform::qt::QtPathAdapter::to_qt(music) + QString::fromUtf8("/音楽 — source.wav")
    );
    const auto generated = wiola::testing::write_wav("wiola_typed_session_duplicate.wav");
    std::filesystem::copy_file(generated, source);
    std::filesystem::remove(generated);
    const auto missing = platform::qt::QtPathAdapter::from_qt(directory.filePath("gone.wav"));
    const auto library_file =
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    library::PlaylistId saved, destination;
    const library::WorkspaceId folder{"saved-folder"};
    const library::PlaylistEntryId chosen{"second-occurrence"};
    const library::MediaId unavailable{"unavailable"};
    const auto config = [&] {
        app::PlayerRuntimeConfig value;
        value.provider = std::make_shared<codec::LocalMediaProvider>();
        value.output = [](audio::Source&) {
            return std::make_unique<wiola::testing::FakeOutput>();
        };
        value.library_file = library_file;
        return value;
    };
    {
        app::PlayerApplication player{config()};
        {
            gui::MainWindow window{player.playback(), player.library(), player.preferences()};
            destination = player.library().playlists().create("Destination");
            saved = player.library().playlists().create("Visible saved list");
            ASSERT_TRUE(saved.valid());
            library::RepositoryTransaction transaction{player.library().repository(),
                player.library().persistence()};
            library::TrackRecord record;
            record.id = library::media_id_for(source);
            record.locations = {source};
            record.overrides.title = "Repeated";
            record.overrides.artist = "Artist";
            record.overrides.album = "Album";
            record.tags = {
                {.id = library::TagId{"mood"}, .key = "Mood", .value = "Calm"}
            };
            ASSERT_TRUE(player.library().repository().put_track(record));
            library::TrackRecord absent;
            absent.id = unavailable;
            absent.locations = {missing};
            absent.missing = true;
            ASSERT_TRUE(player.library().repository().put_track(absent));
            auto playlist = *player.library().repository().playlist(saved);
            playlist.entries = {
                {library::PlaylistEntryId{"first-occurrence"},   record.id  },
                {chosen,                                         record.id  },
                {library::PlaylistEntryId{"missing-occurrence"}, unavailable}
            };
            ASSERT_TRUE(player.library().repository().put_playlist(playlist));
            ASSERT_TRUE(transaction.commit());
            ASSERT_TRUE(player.library().active_playlist().set(destination));
            player.playback().add({source, missing, source});
            ASSERT_TRUE(player.playback().play_track(2));
            window.refresh();
            auto* host =
                static_cast<gui::DocumentHost*>(window.findChild<QTabWidget*>("documentTabs"));
            ASSERT_NE(host, nullptr);
            auto& queue = host->queue().browser().table();
            queue.selectRow(2);
            QEventLoop loop;
            bool complete{};
            QObject::connect(&player.library(), &app::LibrarySession::scan_batch, &loop,
                [&](const auto& batch) {
                    if (batch.workspace == folder &&
                        batch.status == app::WorkspaceScanStatus::complete) {
                        complete = true;
                        loop.quit();
                    }
                });
            {
                library::RepositoryTransaction workspace_transaction{player.library().repository(),
                    player.library().persistence()};
                library::WorkspaceRecord workspace;
                workspace.id = folder;
                workspace.root = music;
                ASSERT_TRUE(player.library().repository().put_workspace(workspace));
                ASSERT_TRUE(workspace_transaction.commit());
            }
            ASSERT_TRUE(host->open_workspace(folder));
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
            ASSERT_TRUE(complete);
            ASSERT_TRUE(host->open_playlist(saved));
            auto* view = host->registry().playlist(saved);
            ASSERT_NE(view, nullptr);
            auto state = view->browser().view_state();
            state.query = {.title = "Repeated",
                .artist = "Artist",
                .album = "Album",
                .tags = "Calm"};
            state.sort_keys = {
                {.field = gui::TrackSortField::title,
                 .direction = gui::TrackSortDirection::descending}
            };
            view->browser().set_view_state(state);
            auto& table = view->browser().table();
            ASSERT_EQ(table.model()->rowCount(), 2);
            table.setColumnWidth(gui::StandardTrackColumn::artist, 211);
            table.selectRow(1);
            EXPECT_EQ(table.chosen_rows(), (std::vector<gui::TrackRowId>{chosen}));
            EXPECT_EQ(host->registry().active(), gui::DocumentId::playlist(saved));
            EXPECT_EQ(player.library().active_playlist().id(), destination);
            window.resize(1100, 750);
            window.close();
            EXPECT_EQ(player.preferences().ui_session().state().window, window.geometry());
        }
        ASSERT_TRUE(player.shutdown());
    }
    {
        app::PlayerApplication player{config()};
        EXPECT_EQ(player.preferences().missing_tracks(), 1);
        ASSERT_EQ(player.playback().playlist().tracks().size(), 2);
        EXPECT_EQ(player.playback().playlist().position(), 1);
        EXPECT_EQ(player.library().active_playlist().id(), destination);
        {
            gui::MainWindow window{player.playback(), player.library(), player.preferences()};
            auto* host =
                static_cast<gui::DocumentHost*>(window.findChild<QTabWidget*>("documentTabs"));
            ASSERT_NE(host, nullptr);
            EXPECT_EQ(host->registry().active(), gui::DocumentId::playlist(saved));
            EXPECT_NE(host->registry().workspace(folder), nullptr);
            auto* view = host->registry().playlist(saved);
            ASSERT_NE(view, nullptr);
            auto& table = view->browser().table();
            EXPECT_EQ(table.chosen_rows(), (std::vector<gui::TrackRowId>{chosen}));
            EXPECT_EQ(table.columnWidth(gui::StandardTrackColumn::artist), 211);
            EXPECT_EQ(view->browser().view_state().query.title, "repeated");
            EXPECT_EQ(view->browser().view_state().query.artist, "artist");
            EXPECT_EQ(view->browser().view_state().sort_keys.front().direction,
                gui::TrackSortDirection::descending);
            const auto queue = host->queue().browser().table().chosen_rows();
            ASSERT_EQ(queue.size(), 1);
            EXPECT_EQ(queue.front(),
                gui::TrackRowId{player.playback().playlist().tracks()[1].id()});
            EXPECT_EQ(player.library().active_playlist().id(), destination);
            ASSERT_NE(player.library().repository().track(unavailable), nullptr);
            EXPECT_TRUE(player.library().repository().track(unavailable)->missing);
        }
        ASSERT_TRUE(player.shutdown());
    }
}

} // namespace wiola::testing
