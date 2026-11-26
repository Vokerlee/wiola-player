#pragma once
#include "session_journey.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTest>
#include <app/library/local_identity.hpp>
#include <app/metadata/media_replacement.hpp>
#include <cstdlib>
#include <iostream>
#include <media/local/fingerprint.hpp>
#include <media/local/resource.hpp>
#include <qt/widgets/appearance/theme_manager.hpp>
#include <qt/widgets/layout/classic_layout.hpp>
#include <stdexcept>

#ifdef WIOLA_TEST_COVERAGE
extern "C" int __llvm_profile_write_file();
#endif
namespace wiola::testing {

inline void journey_require(bool condition, const char* operation)
{
    if (!condition)
        throw std::runtime_error{operation};
}

inline std::filesystem::path journey_fixture(const char* file)
{
    const auto root = qEnvironmentVariableIsEmpty("WIOLA_JOURNEY_MEDIA_DIR")
        ? std::filesystem::path{WIOLA_TEST_DATA_DIR}
        : platform::qt::QtPathAdapter::from_qt(qEnvironmentVariable("WIOLA_JOURNEY_MEDIA_DIR"));
    return root / file;
}

inline app::PlayerRuntimeConfig child_config(const QString& directory)
{
    app::PlayerRuntimeConfig config;
    config.provider = std::make_shared<codec::LocalMediaProvider>();
    config.output = [](audio::Source&) {
        return std::make_unique<FakeOutput>();
    };
    config.library_file =
        platform::qt::QtPathAdapter::from_qt(QDir{directory}.filePath("library.json"));
    return config;
}

inline QByteArray child_bytes(const QString& file)
{
    QFile input{file};
    journey_require(input.open(QIODevice::ReadOnly), "open persisted evidence");
    return input.readAll();
}

inline int session_child(const QString& mode, const QString& directory)
{
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    const auto root = platform::qt::QtPathAdapter::from_qt(directory);
    const auto music = root / "music";
    const auto source = music / "song.wav";
    const library::WorkspaceId folder{"folder"};
    const library::PlaylistId visible{"visible"}, destination{"destination"};
    const library::PlaylistEntryId selected{"second"};
    if (mode == "create") {
        std::filesystem::create_directory(music);
        const auto generated = write_wav("process-session.wav");
        std::filesystem::copy_file(generated, source);
        std::filesystem::remove(generated);
    }
    app::PlayerApplication player{child_config(directory)};
    if (mode == "future") {
        const auto original = child_bytes(QDir{directory}.filePath("library.json"));
        journey_require(!player.library().restore_error().isEmpty(),
            "future schema must report unavailable restore");
        journey_require(!player.shutdown(),
            "future library must not claim successful writable shutdown");
        journey_require(child_bytes(QDir{directory}.filePath("library.json")) == original,
            "future library must remain byte-identical");
        return 0;
    }
    {
        gui::MainWindow window{player.playback(), player.library(), player.preferences()};
        auto* host = static_cast<gui::DocumentHost*>(window.findChild<QTabWidget*>("documentTabs"));
        journey_require(host, "document host exists");
        if (mode == "create") {
            library::RepositoryTransaction seed{player.library().repository(),
                player.library().persistence()};
            library::TrackRecord record;
            record.id = app::media_id_for(source);
            record.locations = {source};
            record.overrides.title = "Music";
            record.tags = {
                {{"mood"}, "Mood", "Calm"}
            };
            journey_require(player.library().repository().put_track(record), "seed media");
            library::PlaylistRecord list;
            list.id = visible;
            list.name = "Visible";
            list.entries = {
                {{"first"}, record.id},
                {selected,  record.id}
            };
            journey_require(player.library().repository().put_playlist(list),
                "seed duplicate playlist");
            list.id = destination;
            list.name = "Destination";
            list.entries.clear();
            journey_require(player.library().repository().put_playlist(list),
                "seed independent destination");
            library::WorkspaceRecord workspace;
            workspace.id = folder;
            workspace.root = music;
            journey_require(player.library().repository().put_workspace(workspace),
                "seed workspace");
            journey_require(static_cast<bool>(seed.commit()), "accept seeded session");
            journey_require(player.library().playlist_destination().set(destination),
                "set independent destination");
            player.playback().add({media::local_resource(source), media::local_resource(source)});
            journey_require(player.playback().play_track(1), "select second queue occurrence");
            window.refresh();
            host->queue().browser().table().selectRow(1);
            auto scanned = std::make_shared<bool>(false);
            QObject::connect(&player.library(), &app::LibrarySession::scan_batch, &window,
                [scanned, folder](const auto& batch) {
                    if (batch.workspace == folder &&
                        batch.status == app::WorkspaceScanStatus::complete)
                        *scanned = true;
                });
            journey_require(host->open_workspace(folder), "open workspace document");
            journey_require(QTest::qWaitFor([scanned] { return *scanned; }),
                "finish workspace before restart");
            journey_require(host->open_playlist(visible), "open visible saved document");
            auto& browser = host->registry().playlist(visible)->browser();
            auto state = browser.view_state();
            state.query.title = "music";
            state.query.tags = "calm";
            browser.set_view_state(state);
            browser.table().setColumnWidth(gui::StandardTrackColumn::artist, 153);
            browser.table().selectRow(1);
            auto layout = gui::classic_layout_document();
            layout.root.id = "process-custom-root";
            journey_require(window.switch_layout(layout), "persist custom layout");
            QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player",
                "settings"};
            auto themes = gui::ThemeManager::acquire(settings);
            journey_require(themes->select(gui::ThemeChoice::dark) == gui::ThemeError::none,
                "persist theme");
        } else {
            journey_require(player.playback().playlist().tracks().size() == 2,
                "restore duplicate queue");
            journey_require(player.playback().playlist().position() == 1,
                "restore current duplicate occurrence");
            journey_require(player.library().playlist_destination().id() == destination,
                "restore independent destination");
            journey_require(host->registry().workspace(folder),
                "restore independent workspace tab");
            journey_require(host->registry().active() == gui::DocumentId::playlist(visible),
                "restore visible document");
            auto* document = host->registry().playlist(visible);
            journey_require(document, "restore opened saved list");
            auto& browser = document->browser();
            journey_require(browser.table().chosen_rows() == std::vector<gui::TrackRowId>{selected},
                "restore exact saved occurrence");
            journey_require(browser.view_state().query.title == "music" &&
                    browser.view_state().query.tags == "calm",
                "restore combined query");
            journey_require(browser.table().columnWidth(gui::StandardTrackColumn::artist) == 153,
                "restore column width");
            journey_require(window.centralWidget()->objectName() == "process-custom-root",
                "restore accepted layout");
            QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player",
                "settings"};
            journey_require(gui::ThemeManager::acquire(settings)->state().selected ==
                    gui::ThemeChoice::dark,
                "restore accepted theme");
        }
        window.close();
    }
    journey_require(player.shutdown(), "flush process session on quit");
    std::cout << "session " << qPrintable(mode) << " succeeded\n";
    return 0;
}

inline app::PreparedMediaReplacement stage_process_replacement(const QString& directory)
{
    const auto source = platform::qt::QtPathAdapter::from_qt(
        QDir{directory}.filePath(QString::fromUtf8("音楽.mp3"))
    );
    std::filesystem::copy_file(journey_fixture("tone.mp3"), source);
    codec::LocalMediaProvider provider;
    auto asset = provider.open(media::local_resource(source));
    journey_require(static_cast<bool>(asset), "open real replacement source");
    media::MetadataWriteRequest request;
    request.overrides.title = "Process committed metadata";
    const auto staged = (*asset)->write_metadata(request);
    journey_require(staged.state == media::MetadataWriteState::written && !staged.source_modified,
        "stage real remux");
    const auto output = staged.staged_output;
    auto validation = app::prepare_media_replacement(source, output, provider);
    journey_require(validation.prepared.has_value(),
        "validate unchanged decoded audio before crash");
    const auto& prepared = *validation.prepared;
    const QJsonObject evidence{
        {"original", QString::fromStdString(prepared.source_fingerprint)   },
        {"updated",  QString::fromStdString(prepared.output_fingerprint)   },
        {"source",   platform::qt::QtPathAdapter::to_qt(prepared.source)   },
        {"output",   platform::qt::QtPathAdapter::to_qt(prepared.temporary)}
    };
    QFile file{QDir{directory}.filePath("replacement.json")};
    journey_require(file.open(QIODevice::WriteOnly), "open crash evidence");
    const auto bytes = QJsonDocument{evidence}.toJson();
    journey_require(file.write(bytes) == bytes.size(), "write crash evidence");
    file.close();
    return prepared;
}

inline int crash_child(const QString& directory, int boundary)
{
    const auto prepared = stage_process_replacement(directory);
    auto port = media::native_file_replacement(media::fingerprint_media, [boundary](auto step) {
        if (static_cast<int>(step) == boundary) {
            std::cerr << "abrupt stop after replacement stage " << boundary << std::endl;
#ifdef WIOLA_TEST_COVERAGE
            __llvm_profile_write_file();
#endif
            std::_Exit(91);
        }
        return true;
    });
    static_cast<void>(app::commit_prepared_media_replacement(prepared, *port));
    return 3; // Reaching this means the requested protocol boundary did not run.
}

} // namespace wiola::testing
