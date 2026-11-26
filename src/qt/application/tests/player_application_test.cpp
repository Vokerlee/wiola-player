/**
 * @file
 * @brief Application ownership tests for widget-independent playback pumping.
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

#include <qt/application/player_application.hpp>
#include <wiola/library/records.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>

#include <qt/widgets/shell/main_window.hpp>
#include <wiola/media/local/resource.hpp>

#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <app/library/local_identity.hpp>
#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <qt/widgets/shell/saved_playlist_manager.hpp>
#include <wiola/media/local/fingerprint.hpp>
#include <wiola/qt/paths.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

template<typename Predicate>
bool pump_until(Predicate&& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    QCoreApplication::processEvents();

    return predicate();
}

class PlayerApplication : public testing::Test {
protected:
    static void SetUpTestSuite()
    {
        settings_directory = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(settings_directory->isValid());

        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory->path());
    }

    static void TearDownTestSuite() { settings_directory.reset(); }

    void SetUp() override
    {
        QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
        settings.clear();
        for (const auto& name : {"layout.json", "layout.json.bak", "session.json",
                 "session.json.bak", "theme.json", "theme.json.bak"})
            QFile::remove(QFileInfo{settings.fileName()}.dir().filePath(name));
        settings.sync();
    }

    void TearDown() override
    {
        for (const std::filesystem::path& track : tracks)
            std::filesystem::remove(track);
    }

    std::filesystem::path track(const char* name)
    {
        tracks.push_back(wiola::testing::write_wav(name));

        return tracks.back();
    }

    static wiola::app::PlayerRuntimeConfig runtime()
    {
        wiola::app::PlayerRuntimeConfig config;
        config.provider = wiola::codec::ffmpeg::make_provider();
        config.output = wiola::testing::passive_output;
        return config;
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
    std::vector<std::filesystem::path> tracks;
};

TEST_F(PlayerApplication, KeepsPumpingAfterTheWindowThatStartedALoadIsDestroyed)
{
    wiola::app::PlayerApplication player{runtime()};
    const std::filesystem::path source = track("wiola_application_load.wav");

    {
        wiola::gui::MainWindow window{player.playback(), player.library(), player.preferences()};
        player.observe(
            window, [&window](const auto& presentation) { window.update_after_pump(presentation); },
            [&window] { window.refresh(); }
        );

        window.open({source});

        EXPECT_EQ(player.playback().track(), wiola::media::ResourceReference{});
        EXPECT_FALSE(player.playback().loaded());
    }

    ASSERT_TRUE(pump_until([&player] { return player.playback().loaded(); }));

    EXPECT_EQ(player.playback().track(), wiola::media::local_resource(source));
    EXPECT_EQ(player.playback().open_result(), wiola::engine::OpenResult::opened);
    EXPECT_FALSE(player.playback().playing());
    const auto media = wiola::app::media_id_for(source);
    ASSERT_TRUE(pump_until([&] { return player.library().repository().track(media) != nullptr; }));
    EXPECT_EQ(player.library().search().size(), 1);
    wiola::gui::MainWindow replacement{player.playback(), player.library(), player.preferences()};
    EXPECT_NE(player.library().repository().track(media), nullptr);
}

TEST_F(PlayerApplication, RecreatingAWindowKeepsTheSameLibraryAndActiveDestination)
{
    wiola::app::PlayerApplication player{runtime()};
    auto& library = player.library();
    const auto playlist = library.playlists().create("Retained").id;
    ASSERT_TRUE(playlist.valid());
    ASSERT_TRUE(library.playlist_destination().set(playlist));
    wiola::library::TrackRecord record;
    record.id = {"retained-media"};
    record.embedded.title = "Only in memory";
    record.metadata_revision = 17;
    {
        wiola::library::RepositoryTransaction edit{library.repository(), library.persistence()};
        ASSERT_TRUE(library.repository().put_track(record));
        ASSERT_TRUE(edit.commit());
    }
    const auto revision = library.repository().change_revision();
    {
        wiola::gui::MainWindow first{player.playback(), library, player.preferences()};
        EXPECT_EQ(library.repository().playlists().size(), 1);
    }
    wiola::gui::MainWindow replacement{player.playback(), library, player.preferences()};
    EXPECT_EQ(library.playlist_destination().id(), playlist);
    ASSERT_NE(library.repository().track(record.id), nullptr);
    EXPECT_EQ(library.repository().track(record.id)->metadata_revision, 17);
    EXPECT_EQ(library.repository().change_revision(), revision);
    wiola::library::SearchQuery query;
    query.title = "Only in memory";
    EXPECT_EQ(library.search().query(query), std::vector{record.id});
}

TEST_F(PlayerApplication, RecreatingAWindowDoesNotRestoreOrDuplicateTheQueueAgain)
{
    wiola::app::PlayerApplication player{runtime()};
    const auto source = track("wiola_runtime_recreate.wav");
    player.playback().add({wiola::media::local_resource(source),
        wiola::media::local_resource(source)});
    ASSERT_TRUE(pump_until([&] { return player.playback().loaded(); }));
    const auto& before = player.playback().playlist().tracks();
    const auto first = before[0].id();
    const auto second = before[1].id();
    for (int count = 0; count < 2; ++count) {
        wiola::gui::MainWindow window{player.playback(), player.library(), player.preferences()};
        ASSERT_EQ(player.playback().playlist().tracks().size(), 2);
        EXPECT_EQ(player.playback().playlist().tracks()[0].id(), first);
        EXPECT_EQ(player.playback().playlist().tracks()[1].id(), second);
    }
}

TEST_F(PlayerApplication, ObserverLifetimeAndShutdownDoNotLeaveLiveCallbacksOrCommands)
{
    wiola::app::PlayerApplication player{runtime()};
    int calls = 0;
    {
        QObject view;
        player.observe(view, [&](const auto&) { ++calls; }, [] { });
        ASSERT_TRUE(pump_until([&] { return calls != 0; }));
    }
    const int last = calls;
    EXPECT_TRUE(player.shutdown());
    QCoreApplication::processEvents();
    EXPECT_EQ(calls, last);
    EXPECT_EQ(player.playback().open({wiola::media::local_resource("after-quit.wav")}).acceptance,
        wiola::engine::CommandResult::unavailable);
    EXPECT_FALSE(player.library().playlists().create("After quit").id.valid());
    EXPECT_TRUE(player.shutdown());
}

TEST_F(PlayerApplication, ReentrantShutdownReportsBusyUntilTheOuterFlushCompletes)
{
    QTemporaryDir directory;
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = wiola::testing::passive_output;
    config.library_file =
        wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    wiola::app::PlayerApplication player{std::move(config)};
    bool observed = false;
    bool nested = true;
    QObject::connect(&player.library(), &wiola::app::LibrarySession::library_save_finished, &player,
        [&](auto, bool) {
            if (!std::exchange(observed, true))
                nested = player.shutdown();
        });
    const auto created = player.library().playlists().create("Shutdown boundary");
    ASSERT_TRUE(created.id.valid());

    EXPECT_TRUE(player.shutdown());
    EXPECT_TRUE(observed);
    EXPECT_FALSE(nested);
    ASSERT_TRUE(created.commit.receipt);
    EXPECT_TRUE(created.commit.receipt->durable());
    EXPECT_TRUE(player.shutdown());
}

TEST_F(PlayerApplication, CompletionObserverFailureDoesNotPreventAnExplicitShutdownRetry)
{
    QTemporaryDir directory;
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = wiola::testing::passive_output;
    config.library_file =
        wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    wiola::app::PlayerApplication player{std::move(config)};
    const auto connection =
        QObject::connect(&player.library(), &wiola::app::LibrarySession::library_save_finished,
            &player, [](auto, bool) { throw std::logic_error{"completion observer failed"}; });
    const auto created = player.library().playlists().create("Retained after observer failure");
    ASSERT_TRUE(created.id.valid());

    EXPECT_THROW(static_cast<void>(player.shutdown()), std::logic_error);
    QObject::disconnect(connection);
    EXPECT_TRUE(player.shutdown());
    ASSERT_TRUE(created.commit.receipt);
    EXPECT_TRUE(created.commit.receipt->durable());
    EXPECT_NE(player.library().repository().playlist(created.id), nullptr);
}

TEST_F(PlayerApplication, ReportsFailedFlushAndAllowsAnExplicitRetry)
{
    QTemporaryDir destination;
    ASSERT_TRUE(destination.isValid());
    const auto blocker = destination.path() + "/blocked";
    QFile file{blocker};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = [](wiola::audio::Source&) {
        return std::unique_ptr<wiola::audio::Output>{};
    };
    config.library_file = wiola::platform::qt::QtPathAdapter::from_qt(blocker + "/library.json");
    wiola::app::PlayerApplication player{std::move(config)};
    EXPECT_FALSE(player.shutdown());
    ASSERT_TRUE(QFile::remove(blocker));
    EXPECT_TRUE(player.shutdown());
}

TEST_F(PlayerApplication, WorkspaceRelocationPreservesSavedReferencesAndCanonicalMedia)
{
    wiola::app::PlayerApplication player{runtime()};
    QTemporaryDir workspace_directory;
    ASSERT_TRUE(workspace_directory.isValid());
    const auto old_name = workspace_directory.path() + "/old.wav";
    const auto new_name = workspace_directory.path() + "/new.wav";
    const auto source = track("wiola_workspace_identity.wav");
    ASSERT_TRUE(QFile::copy(wiola::platform::qt::QtPathAdapter::to_qt(source), old_name));
    auto& library = player.library();
    wiola::library::WorkspaceId workspace;
    {
        wiola::library::RepositoryTransaction edit{library.repository(), library.persistence()};
        workspace = library.repository().create_workspace(
            wiola::platform::qt::QtPathAdapter::from_qt(workspace_directory.path())
        );
        ASSERT_TRUE(edit.commit());
    }
    std::uint64_t completed = 0;
    QObject receiver;
    QObject::connect(&library, &wiola::app::LibrarySession::scan_batch, &receiver,
        [&](const auto& batch) {
            if (batch.status == wiola::app::WorkspaceScanStatus::complete)
                completed = batch.generation;
        });
    const auto first = library.scan_workspace(workspace, {".wav"});
    ASSERT_TRUE(pump_until([&] { return completed == first; }));
    ASSERT_EQ(library.repository().tracks().size(), 1);
    const auto media = library.repository().tracks().front().id;
    const auto playlist = library.playlists().create("Saved").id;
    {
        wiola::library::RepositoryTransaction edit{library.repository(), library.persistence()};
        ASSERT_TRUE(library.repository().append_track(playlist, media));
        ASSERT_TRUE(edit.commit());
    }
    ASSERT_TRUE(QFile::rename(old_name, new_name));
    const auto second = library.scan_workspace(workspace, {".wav"});
    ASSERT_TRUE(pump_until([&] { return completed == second; }));
    EXPECT_EQ(library.repository().tracks().size(), 1);
    EXPECT_EQ(library.media_at(wiola::platform::qt::QtPathAdapter::from_qt(new_name)), media);
    ASSERT_NE(library.repository().track(media), nullptr);
    EXPECT_EQ(wiola::library::preferred_location(*library.repository().track(media)),
        wiola::platform::qt::QtPathAdapter::from_qt(new_name));
    EXPECT_EQ(library.repository().playlist(playlist)->entries.front().media, media);
}

TEST_F(PlayerApplication, ExplicitGuiFileWriteReopensQualifiedMediaAndRestoresPlayingIntent)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = wiola::platform::qt::QtPathAdapter::from_qt(
        directory.filePath(QString::fromUtf8("музыка.mp3"))
    );
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", path);
    const auto original_fingerprint = wiola::media::fingerprint_media(path);
    ASSERT_TRUE(original_fingerprint);
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.library_file =
        wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    config.output = [](wiola::audio::Source&) {
        return std::make_unique<wiola::testing::FakeOutput>();
    };
    wiola::app::PlayerApplication player{config};
    wiola::gui::MainWindow window{player.playback(), player.library(), player.preferences()};
    player.observe(
        window, [&](const auto& value) { window.update_after_pump(value); },
        [&] { window.refresh(); }
    );
    window.open({path});
    const auto media = wiola::app::media_id_for(path);
    ASSERT_TRUE(pump_until([&] {
        return player.playback().loaded() && player.library().repository().track(media);
    }));
    ASSERT_EQ(player.playback().play(), wiola::engine::CommandResult::accepted);
    window.refresh();
    std::vector<wiola::app::MetadataWriteStage> stages;
    QObject::connect(&player.library(), &wiola::app::LibrarySession::file_write_outcome, &window,
        [&](const auto& value) {
            EXPECT_EQ(QThread::currentThread(), window.thread());
            stages.push_back(value.stage);
        });
    auto* title = window.findChild<QLineEdit*>("standardTitle");
    ASSERT_NE(title, nullptr);
    title->setText("Written through the GUI");
    auto* save = window.findChild<QPushButton*>("saveMetadataToFile");
    ASSERT_TRUE(save->isEnabled());
    save->click();
    ASSERT_TRUE(pump_until([&] {
        return std::ranges::find(stages, wiola::app::MetadataWriteStage::complete) !=
            stages.end() ||
            std::ranges::find(stages, wiola::app::MetadataWriteStage::failed) != stages.end();
    }));
    ASSERT_NE(std::ranges::find(stages, wiola::app::MetadataWriteStage::complete), stages.end())
        << window.findChild<QLabel*>("metadataFileStatus")->text().toStdString();
    ASSERT_TRUE(pump_until([&] {
        return player.playback().loaded() && player.playback().playing();
    }));
    EXPECT_NE(std::ranges::find(stages, wiola::app::MetadataWriteStage::remuxing), stages.end());
    EXPECT_NE(std::ranges::find(stages, wiola::app::MetadataWriteStage::validating), stages.end());
    EXPECT_NE(std::ranges::find(stages, wiola::app::MetadataWriteStage::reloading), stages.end());
    auto asset = config.provider->open(wiola::media::local_resource(path));
    ASSERT_TRUE(asset);
    auto metadata = (*asset)->read_metadata();
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->track.title, "Written through the GUI");
    const auto* record = player.library().repository().track(media);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->embedded.title, "Written through the GUI");
    EXPECT_EQ(record->overrides.title, "Written through the GUI");
    EXPECT_EQ(record->content_revision, 1);
    EXPECT_EQ(record->fingerprint, wiola::media::fingerprint_media(path));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path{
        path.string() + ".wiola-backup." + original_fingerprint->substr(8, 16) + ".1"}));
    EXPECT_EQ(window.findChild<QLabel*>("metadataFileStatus")->text(),
        "Saved embedded metadata to file");
    EXPECT_TRUE(player.shutdown());
}

TEST_F(PlayerApplication, UnsupportedGuiFileWriteRetainsApplicationOverrideAndOriginalBytes)
{
    QTemporaryDir directory;
    const auto source = track("wiola-readonly-format.wav");
    const auto before = wiola::media::fingerprint_media(source);
    ASSERT_TRUE(before);
    wiola::app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = [](wiola::audio::Source&) {
        return std::make_unique<wiola::testing::FakeOutput>();
    };
    config.library_file =
        wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    wiola::app::PlayerApplication player{config};
    wiola::gui::MainWindow window{player.playback(), player.library(), player.preferences()};
    player.observe(
        window, [&](const auto& value) { window.update_after_pump(value); },
        [&] { window.refresh(); }
    );
    window.open({source});
    const auto media = wiola::app::media_id_for(source);
    ASSERT_TRUE(pump_until([&] {
        return player.playback().loaded() && player.library().repository().track(media);
    }));
    window.refresh();
    std::optional<wiola::app::MetadataWriteResult> terminal;
    QObject::connect(&player.library(), &wiola::app::LibrarySession::file_write_outcome, &window,
        [&](const auto& value) {
            if (value.stage == wiola::app::MetadataWriteStage::failed)
                terminal = value;
        });
    window.findChild<QLineEdit*>("standardTitle")->setText("Application value");
    window.findChild<QPushButton*>("saveMetadataToFile")->click();
    ASSERT_TRUE(pump_until([&] { return terminal.has_value(); }));
    EXPECT_EQ(terminal->outcome.error, wiola::media::MetadataWriteError::unsupported);
    EXPECT_FALSE(terminal->outcome.source_modified);
    EXPECT_EQ(wiola::media::fingerprint_media(source), before);
    EXPECT_EQ(player.library().repository().track(media)->overrides.title, "Application value");
    EXPECT_TRUE(window.findChild<QLabel*>("metadataFileStatus")
            ->text()
            .contains("Application overrides remain"));
    EXPECT_TRUE(player.shutdown());
}

TEST_F(PlayerApplication, LibraryReconcilesTwoIndependentWorkspaceGenerations)
{
    QTemporaryDir directory;
    const auto root = wiola::platform::qt::QtPathAdapter::from_qt(directory.path());
    std::filesystem::create_directories(root / "first");
    std::filesystem::create_directories(root / "second");
    const auto source = track("wiola-independent-library.wav");
    std::filesystem::copy_file(source, root / "first" / "one.wav");
    std::filesystem::copy_file(source, root / "second" / "two.wav");
    wiola::app::LibrarySession library{root / "library.json",
        wiola::codec::ffmpeg::make_provider()};
    wiola::library::WorkspaceId first, second;
    {
        wiola::library::RepositoryTransaction transaction{library.repository(),
            library.persistence()};
        first = library.repository().create_workspace(root / "first");
        second = library.repository().create_workspace(root / "second");
        ASSERT_TRUE(transaction.commit());
    }
    std::set<wiola::library::WorkspaceId> completed;
    QObject receiver;
    QObject::connect(&library, &wiola::app::LibrarySession::scan_batch, &receiver,
        [&](const auto& batch) {
            if (batch.status == wiola::app::WorkspaceScanStatus::complete)
                completed.insert(batch.workspace);
        });
    const auto one = library.scan_workspace(first, {".wav"}),
               two = library.scan_workspace(second, {".wav"});
    EXPECT_NE(one, two);
    ASSERT_TRUE(pump_until([&] { return completed.size() == 2; }));
    EXPECT_EQ(library.repository().tracks().size(), 2);
    ASSERT_NE(library.repository().track(library.media_at(root / "first" / "one.wav")), nullptr);
    ASSERT_NE(library.repository().track(library.media_at(root / "second" / "two.wav")), nullptr);
    EXPECT_TRUE(library.shutdown().succeeded());
}

TEST_F(PlayerApplication, RecursiveWorkspaceRefreshContinuesWithoutAWindow)
{
    QTemporaryDir directory;
    const auto base = wiola::platform::qt::QtPathAdapter::from_qt(directory.path()),
               root = base / "music";
    std::filesystem::create_directories(root);
    const auto source = track("wiola-background-watch.wav");
    std::filesystem::copy_file(source, root / "one.wav");
    wiola::app::LibrarySession library{base / "library.json",
        wiola::codec::ffmpeg::make_provider()};
    QObject receiver;
    int completed = 0;
    QObject::connect(&library, &wiola::app::LibrarySession::scan_batch, &receiver,
        [&](const auto& batch) {
            if (batch.status == wiola::app::WorkspaceScanStatus::complete)
                ++completed;
        });
    wiola::library::WorkspaceId workspace;
    {
        wiola::library::RepositoryTransaction transaction{library.repository(),
            library.persistence()};
        workspace = library.repository().create_workspace(root);
        ASSERT_TRUE(transaction.commit());
    }
    ASSERT_TRUE(pump_until([&] {
        return completed >= 1 && library.repository().tracks().size() == 1;
    }));
    const int initial = completed;
    std::filesystem::create_directories(root / "new" / "nested");
    ASSERT_TRUE(pump_until([&] { return completed > initial; }));
    std::filesystem::copy_file(source, root / "new" / "nested" / "two.wav");
    ASSERT_TRUE(pump_until([&] { return library.repository().tracks().size() == 2; }));
    ASSERT_TRUE(library.watcher().coverage(workspace));
    EXPECT_FALSE(library.watcher().coverage(workspace)->partial);
    EXPECT_TRUE(library.shutdown().succeeded());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
