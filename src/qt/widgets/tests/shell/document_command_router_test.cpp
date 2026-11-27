/**
 * @file
 * @brief Registration and state tests for shared player actions.
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

#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/documents/saved_playlist_document.hpp>
#include <qt/widgets/metadata/track_inspector.hpp>
#include <qt/widgets/shell/document_command_router.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>
#include <qt/widgets/tests/support/player_dialogs.hpp>
#include <qt/widgets/tracks/track_browser.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/resource.hpp>
#include <wiola/qt/paths.hpp>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

namespace {

using namespace wiola;

class DocumentCommandRouterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        library = std::make_unique<app::LibrarySession>(wiola::codec::ffmpeg::make_provider());
        preferences = std::make_unique<app::PlaybackPreferences>(playback);

        library::RepositoryTransaction seed{library->repository(), library->persistence()};

        for (const auto& name : {"one", "two"}) {
            library::TrackRecord track;
            track.id = {name};
            track.locations = {std::string{name} + ".wav"};
            track.embedded.title = name;
            ASSERT_TRUE(library->repository().put_track(track));
        }

        ASSERT_TRUE(seed.commit());
    }

    gui::DocumentCommandRouter router(wiola::testing::ScriptedPlayerDialogs& dialogs)
    {
        return {*library, playback, preferences->queue(), dialogs,
            [this](const auto& outcome) { outcomes.push_back(outcome); }};
    }

    QTemporaryDir directory;
    app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};

    std::unique_ptr<app::LibrarySession> library;
    std::unique_ptr<app::PlaybackPreferences> preferences;
    std::vector<gui::DocumentCommandOutcome> outcomes;
};

TEST_F(DocumentCommandRouterTest, PlaylistMenuAppendsOnlySelectedTracksWithoutReplacingPlayback)
{
    const auto source = library->playlists().create("Source").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"two"});

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(seed.commit());

    const auto existing = wiola::testing::write_wav("playing.wav", wiola::testing::source_rate,
        platform::qt::QtPathAdapter::from_qt(directory.path()));
    static_cast<void>(playback.open({media::local_resource(existing)}));

    ASSERT_TRUE(wiola::testing::await_gui([&] {
        playback.pump();
        return !playback.reading();
    }));
    ASSERT_TRUE(playback.loaded());
    ASSERT_EQ(playback.play(), engine::CommandResult::accepted);

    playback.set_repeat(engine::RepeatMode::all);
    const auto original = playback.playlist().current()->id();
    const auto original_entries = library->repository().playlist(source)->entries;
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.execute(command, std::move(context));
    };
    gui::DocumentHost host{*library, playback, preferences->ui_session(), std::move(callbacks)};

    ASSERT_TRUE(host.open_playlist(source));

    auto* document = host.registry().playlist(source);
    auto& table = document->browser().table();
    table.selectRow(1);
    auto* append = table.findChild<QAction*>("appendToPlayQueue");

    ASSERT_NE(append, nullptr);
    ASSERT_TRUE(append->isVisible());
    ASSERT_TRUE(append->isEnabled());

    QMenu menu;
    const auto previews = qEnvironmentVariable("WIOLA_GUI_PREVIEW_DIR");
    menu.setUpdatesEnabled(!previews.isEmpty());
    menu.addActions(table.actions());

    if (!previews.isEmpty())
        gui::apply_theme(gui::Theme::dark);

    menu.popup(table.mapToGlobal(QPoint{}));

    ASSERT_TRUE(wiola::testing::await_gui([&] { return menu.isVisible(); }));
    menu.setActiveAction(append);

    if (!previews.isEmpty()) {
        QDir{}.mkpath(previews);
        EXPECT_TRUE(menu.grab().save(QDir{previews}.filePath("add-to-play-queue-menu.png")));
    }

    QTest::keyClick(&menu, Qt::Key_Return);

    ASSERT_EQ(playback.playlist().tracks().size(), 2U);
    EXPECT_EQ(playback.playlist().current()->id(), original);
    EXPECT_EQ(playback.playlist().tracks()[0].resource(), media::local_resource(existing));
    EXPECT_EQ(playback.playlist().tracks()[1].resource(), media::local_resource("two.wav"));
    EXPECT_EQ(playback.repeat(), engine::RepeatMode::none);
    EXPECT_TRUE(playback.playing());
    EXPECT_EQ(library->repository().playlist(source)->entries, original_entries);
    EXPECT_EQ(host.currentWidget(), document);
    ASSERT_EQ(outcomes.size(), 1U);
    EXPECT_TRUE(outcomes.back().queue_changed);
    EXPECT_FALSE(outcomes.back().open_result);
    EXPECT_EQ(outcomes.back().message, "1 track(s) added to play queue");

    // A queued menu event from a closing document must not append a second time.
    const QPointer<QAction> closing_action{append};

    ASSERT_TRUE(host.close(gui::DocumentId::playlist(source)));
    ASSERT_NE(closing_action, nullptr);

    closing_action->trigger();

    EXPECT_EQ(playback.playlist().tracks().size(), 2U);
    EXPECT_EQ(outcomes.size(), 1U);
}

TEST_F(DocumentCommandRouterTest, EmptyQueueAppendPreservesRepeatAndExistingTracks)
{
    playback.add({media::local_resource("existing.wav")});
    playback.set_repeat(engine::RepeatMode::track);
    const auto original = playback.playlist().current()->id();
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommandContext context{
        .document = gui::DocumentId::playlist(library::PlaylistId{"source"}),
        .capabilities = gui::TrackCapabilities::playlist(),
        .selection = {},
        .displayed_paths = {},
        .active_playlist = {},
        .target_playlist = {}};

    commands.execute(gui::TrackCommand::append, context);
    commands.open({}, true);

    EXPECT_EQ(playback.playlist().tracks().size(), 1U);
    EXPECT_EQ(playback.playlist().current()->id(), original);
    EXPECT_EQ(playback.repeat(), engine::RepeatMode::track);
}

TEST_F(DocumentCommandRouterTest,
    ManuallyQueuedTracksFinishWithoutStartingTheRestOfTheSavedPlaylist)
{
    const auto folder = platform::qt::QtPathAdapter::from_qt(directory.path());
    const auto first_path =
        wiola::testing::write_wav("first.wav", wiola::testing::source_rate, folder);
    const auto last_path =
        wiola::testing::write_wav("last.wav", wiola::testing::source_rate, folder);
    library::RepositoryTransaction seed{library->repository(), library->persistence()};

    for (const auto& [id, path] : {
             std::pair{library::MediaId{"one"}, first_path},
             std::pair{library::MediaId{"two"}, last_path }
    }) {
        auto track = *library->repository().track(id);
        track.locations = {path};
        ASSERT_TRUE(library->repository().put_track(track));
    }

    const auto source = library->repository().create_playlist("Source");

    ASSERT_TRUE(library->repository().append_track(source, library::MediaId{"one"}));
    ASSERT_TRUE(library->repository().append_track(source, library::MediaId{"two"}));
    ASSERT_TRUE(library->repository().append_track(source, library::MediaId{"one"}));
    ASSERT_TRUE(seed.commit());

    app::PlaybackService consuming{
        [](audio::Source& stream) { return std::make_unique<wiola::testing::FakeOutput>(stream); },
        codec::ffmpeg::make_provider()};
    app::PlaybackPreferences consuming_preferences{consuming};
    wiola::testing::ScriptedPlayerDialogs dialogs;
    gui::DocumentCommandRouter commands{*library, consuming, consuming_preferences.queue(), dialogs,
        {}};
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.execute(command, std::move(context));
    };
    gui::DocumentHost host{*library, consuming, consuming_preferences.ui_session(),
        std::move(callbacks)};

    ASSERT_TRUE(host.open_playlist(source));

    auto* document = host.registry().playlist(source);
    auto& table = document->browser().table();
    table.selectRow(0);
    table.selectionModel()->select(table.model()->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    consuming.set_repeat(engine::RepeatMode::all);
    table.findChild<QAction*>("appendToPlayQueue")->trigger();

    ASSERT_EQ(consuming.playlist().tracks().size(), 2U);
    EXPECT_EQ(consuming.repeat(), engine::RepeatMode::none);
    EXPECT_FALSE(consuming.playing());
    ASSERT_TRUE(wiola::testing::await_gui([&] {
        consuming.pump();
        return !consuming.reading();
    }));
    ASSERT_TRUE(consuming.loaded());
    ASSERT_EQ(consuming.play(), engine::CommandResult::accepted);

    const auto last = consuming.playlist().tracks().back().id();

    ASSERT_TRUE(wiola::testing::await_gui([&] {
        consuming.pump();
        return !consuming.reading() && consuming.track() == media::local_resource(last_path) &&
            consuming.playlist().current()->id() == last &&
            consuming.state() == engine::PlaybackState::ended;
    }));

    for (int refresh = 0; refresh < 32; ++refresh)
        consuming.pump();

    EXPECT_FALSE(consuming.playing());
    EXPECT_FALSE(consuming.reading());
    EXPECT_EQ(consuming.playlist().current()->id(), last);
    EXPECT_EQ(consuming.track(), media::local_resource(last_path));
    EXPECT_EQ(library->repository().playlist(source)->entries.size(), 3U);
    EXPECT_EQ(host.currentWidget(), document);
}

TEST_F(DocumentCommandRouterTest, ContextMenuTransferAfterCloseAndReopenUsesOneExactOccurrence)
{
    const auto source = library->playlists().create("Source").id,
               destination = library->playlists().create("Destination").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"one"});

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(seed.commit());
    ASSERT_TRUE(library->playlist_destination().set(destination));

    wiola::testing::ScriptedPlayerDialogs dialogs;
    dialogs.chosen_destination = destination;

    auto commands = router(dialogs);
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.execute(command, std::move(context));
    };

    gui::DocumentHost host{*library, playback, preferences->ui_session(), std::move(callbacks)};
    host.setUpdatesEnabled(false);
    host.resize(650, 650);
    host.show();

    for (int opening = 0; opening < 2; ++opening) {
        ASSERT_TRUE(host.open_playlist(source));

        auto* document = host.registry().playlist(source);
        auto& table = document->browser().table();
        auto state = document->browser().capture_browser_state();
        state.selected = {*second};
        state.current = *second;
        document->browser().restore_browser_state(state);

        QAction* transfer = nullptr;

        for (auto* action : table.actions())
            if (action->text() == "Add to playlist")
                transfer = action;

        ASSERT_NE(transfer, nullptr);
        ASSERT_TRUE(transfer->isEnabled());

        auto* menu = transfer->menu();

        ASSERT_NE(menu, nullptr);

        menu->setUpdatesEnabled(false);
        menu->popup(QPoint{10, 10});
        ASSERT_TRUE(wiola::testing::await_gui([&] { return menu->isVisible(); }));

        QAction* choice = nullptr;

        for (auto* item : menu->actions())
            if (item->data().toString().toStdString() == destination.value)
                choice = item;

        ASSERT_NE(choice, nullptr);

        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
            menu->actionGeometry(choice).center());
        EXPECT_EQ(dialogs.prompts, 0);
        ASSERT_EQ(library->repository().playlist(destination)->entries.size(), opening + 1);
        EXPECT_EQ(library->repository().playlist(source)->entries.size(), 2);
        EXPECT_EQ(library->playlist_destination().id(), destination);
        EXPECT_EQ(host.registry().active(), gui::DocumentId::playlist(source));
        EXPECT_EQ(table.chosen_rows(), (std::vector<gui::TrackRowId>{*second}));

        QPointer<QWidget> retired{document};

        ASSERT_TRUE(host.close(gui::DocumentId::playlist(source)));

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        EXPECT_TRUE(retired.isNull());
    }

    ASSERT_EQ(outcomes.size(), 2);
    EXPECT_EQ(playback.playlist().tracks().size(), 0);
}

TEST_F(DocumentCommandRouterTest, DismissingDestinationSubmenuLeavesSelectionAndPlaylistsUnchanged)
{
    const auto source = library->playlists().create("Source").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};
    const auto occurrence = library->repository().append_track(source, library::MediaId{"one"});

    ASSERT_TRUE(occurrence);
    ASSERT_TRUE(seed.commit());

    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.execute(command, std::move(context));
    };

    gui::DocumentHost host{*library, playback, preferences->ui_session(), std::move(callbacks)};

    ASSERT_TRUE(host.open_playlist(source));

    auto& table = host.registry().playlist(source)->browser().table();
    table.selectRow(0);

    QAction* transfer = nullptr;

    for (auto* action : table.actions())
        if (action->text().startsWith("Add to playlist"))
            transfer = action;

    ASSERT_NE(transfer, nullptr);

    QMenu menu;
    menu.setUpdatesEnabled(false);
    menu.addAction(transfer);
    menu.popup(QPoint{10, 10});
    QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier,
        menu.actionGeometry(transfer).center());
    transfer->menu()->hide();
    menu.hide();
    EXPECT_EQ(dialogs.prompts, 0);
    EXPECT_TRUE(outcomes.empty());
    EXPECT_EQ(table.chosen_rows(), (std::vector<gui::TrackRowId>{*occurrence}));
    EXPECT_EQ(library->repository().playlist(source)->entries.size(), 1);
}

TEST_F(DocumentCommandRouterTest,
    InlineMoveMenuUsesFriendlyNamesAndCapturesOneOccurrenceBeforeFocusChanges)
{
    const auto source = library->playlists().create("Source").id;
    const auto other = library->playlists().create("Other").id;
    const library::PlaylistId migrated{"playlist-migrated-queue"};
    library::RepositoryTransaction seed{library->repository(), library->persistence()};

    ASSERT_TRUE(library->repository().put_playlist({.id = migrated, .name = "Migrated queue"}));

    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto duplicate = library->repository().append_track(source, library::MediaId{"one"});

    ASSERT_TRUE(first && duplicate);
    ASSERT_TRUE(seed.commit());

    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommands callbacks;
    callbacks.execute = [&](auto command, auto context) {
        commands.execute(command, std::move(context));
    };

    gui::DocumentHost host{*library, playback, preferences->ui_session(), std::move(callbacks)};

    ASSERT_TRUE(host.open_playlist(source));

    auto& table = host.registry().playlist(source)->browser().table();
    table.selectRow(1);

    QAction* move = nullptr;

    for (auto* item : table.actions())
        if (item->text() == "Move to playlist")
            move = item;

    ASSERT_NE(move, nullptr);

    auto* destinations = move->menu();

    ASSERT_NE(destinations, nullptr);

    destinations->setUpdatesEnabled(false);
    destinations->popup(QPoint{10, 10});
    ASSERT_TRUE(wiola::testing::await_gui([&] { return destinations->isVisible(); }));
    ASSERT_EQ(destinations->actions().size(), 2);

    QAction* selected = nullptr;

    for (auto* item : destinations->actions()) {
        EXPECT_NE(item->data().toString().toStdString(), source.value);
        EXPECT_NE(item->text(), "Migrated queue");

        if (item->data().toString().toStdString() == migrated.value) {
            selected = item;
            EXPECT_EQ(item->text(), "My playlist");
        }
    }

    ASSERT_NE(selected, nullptr);

    table.selectRow(0);
    ASSERT_TRUE(host.open_playlist(other));

    selected->trigger();
    destinations->hide();
    EXPECT_EQ(dialogs.prompts, 0);

    const auto* remaining = library->repository().playlist(source);

    ASSERT_EQ(remaining->entries.size(), 1);
    EXPECT_EQ(remaining->entries.front().id, *first);
    EXPECT_EQ(library->repository().playlist(migrated)->entries.size(), 1);
    EXPECT_TRUE(library->repository().playlist(other)->entries.empty());
}

TEST_F(DocumentCommandRouterTest,
    MoveCapturesDuplicateOccurrenceBeforePromptChangesDocumentAndSelection)
{
    const auto source = library->playlists().create("Source").id,
               destination = library->playlists().create("Destination").id,
               other = library->playlists().create("Other").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"one"});

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(seed.commit());

    wiola::testing::ScriptedPlayerDialogs dialogs;
    dialogs.chosen_destination = destination;

    auto commands = router(dialogs);
    gui::DocumentHost host{*library, playback, preferences->ui_session(), {}};

    ASSERT_TRUE(host.open_playlist(source));
    ASSERT_TRUE(host.open_playlist(other));
    ASSERT_TRUE(host.activate(gui::DocumentId::playlist(source)));

    auto* document = host.registry().playlist(source);
    {
        auto state = document->browser().capture_browser_state();
        state.selected = {*second};
        state.current = *second;
        document->browser().restore_browser_state(state);
    }

    const auto captured = host.capture(gui::DocumentId::playlist(source));

    ASSERT_EQ(captured.selection.size(), 1);

    dialogs.prompted = [&] {
        {
            auto state = document->browser().capture_browser_state();
            state.selected = {*first};
            state.current = *first;
            document->browser().restore_browser_state(state);
        }
        ASSERT_TRUE(host.activate(gui::DocumentId::playlist(other)));

        static_cast<void>(library->playlist_destination().set(other));
    };
    commands.execute(gui::TrackCommand::move, captured);

    const auto* remaining = library->repository().playlist(source);

    ASSERT_EQ(remaining->entries.size(), 1);
    EXPECT_EQ(remaining->entries[0].id, *first);

    const auto* target = library->repository().playlist(destination);

    ASSERT_EQ(target->entries.size(), 1);
    EXPECT_EQ(target->entries[0].media, library::MediaId{"one"});
    EXPECT_TRUE(library->repository().playlist(other)->entries.empty());
    EXPECT_EQ(dialogs.prompts, 1);
}

TEST_F(DocumentCommandRouterTest, SavedPlaylistKeyboardActivationRoutesTheExactDuplicateOccurrence)
{
    const auto source = library->playlists().create("Duplicates").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"one"});

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(seed.commit());

    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommands callbacks;
    callbacks.play_playlist = [&](auto id, auto entry) {
        commands.play_saved(id, entry);
    };

    gui::DocumentHost host{*library, playback, preferences->ui_session(), std::move(callbacks)};

    ASSERT_TRUE(host.open_playlist(source));

    auto* document = host.registry().playlist(source);
    auto state = document->browser().capture_browser_state();
    state.selected = {*second};
    state.current = *second;
    document->browser().restore_browser_state(state);
    QTest::keyClick(&document->browser().table(), Qt::Key_Return);
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().position(), 1);
    EXPECT_EQ(playback.playlist().tracks()[1].resource(),
        wiola::media::local_resource(std::filesystem::path{"one.wav"}));
}

TEST_F(DocumentCommandRouterTest, DisabledCapabilitiesAndCancelledPromptsNeverMutateDocuments)
{
    const auto destination = library->playlists().create("Destination").id;
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommandContext context{gui::DocumentId::queue(), {},
        {{library::MediaId{"one"}, engine::QueueEntryId{9}, "one.wav"}}, {}};
    commands.execute(gui::TrackCommand::transfer_destination, context);
    EXPECT_EQ(dialogs.prompts, 0);

    context.capabilities = gui::TrackCapabilities::queue();
    commands.execute(gui::TrackCommand::transfer_destination, context);
    EXPECT_EQ(dialogs.prompts, 1);
    EXPECT_TRUE(library->repository().playlist(destination)->entries.empty());
    EXPECT_TRUE(outcomes.empty());
}

TEST_F(DocumentCommandRouterTest, DestroyingRouterWhileAChoiceIsPendingCannotIssueTheCommand)
{
    wiola::testing::ScriptedPlayerDialogs dialogs;
    dialogs.chosen_path = std::filesystem::path{directory.path().toStdString()};

    auto* commands = new gui::DocumentCommandRouter{*library, playback, preferences->queue(),
        dialogs, [this](const auto& outcome) { outcomes.push_back(outcome); }};
    dialogs.prompted = [&] {
        delete commands;
        commands = nullptr;
    };

    auto* invoking = commands;
    invoking->choose_folder();
    EXPECT_EQ(commands, nullptr);
    EXPECT_TRUE(library->repository().workspaces().empty());
    EXPECT_TRUE(outcomes.empty());
}

TEST_F(DocumentCommandRouterTest,
    AddToActiveUsesCapturedPlaybackContextAndNeverFallsBackToLegacyDestination)
{
    const auto active = library->playlists().create("Active").id;
    const auto legacy = library->playlists().create("Legacy destination").id;

    ASSERT_TRUE(library->playlist_destination().set(legacy));

    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    gui::DocumentCommandContext context{gui::DocumentId::queue(), gui::TrackCapabilities::queue(),
        {{library::MediaId{"one"}, engine::QueueEntryId{1}, "one.wav"}}, {}};
    context.active_playlist = active;
    commands.execute(gui::TrackCommand::transfer, context);
    ASSERT_EQ(library->repository().playlist(active)->entries.size(), 1);
    EXPECT_TRUE(library->repository().playlist(legacy)->entries.empty());

    context.active_playlist.reset();
    commands.execute(gui::TrackCommand::transfer, context);
    EXPECT_EQ(library->repository().playlist(active)->entries.size(), 1);
    EXPECT_TRUE(library->repository().playlist(legacy)->entries.empty());
    ASSERT_FALSE(outcomes.empty());
    EXPECT_TRUE(outcomes.back().message.contains("No active playlist"));

    commands.export_playlist({});
    EXPECT_EQ(dialogs.prompts, 0);
    EXPECT_TRUE(outcomes.back().message.contains("Open a saved playlist"));
}

TEST_F(DocumentCommandRouterTest, ExportCapturesPlaylistBeforePromptChangesTheDestination)
{
    const auto source = library->playlists().create("Source").id,
               other = library->playlists().create("Other").id;
    library::RepositoryTransaction seed{library->repository(), library->persistence()};

    ASSERT_TRUE(library->repository().append_track(source, library::MediaId{"one"}));
    ASSERT_TRUE(library->repository().append_track(other, library::MediaId{"two"}));
    ASSERT_TRUE(seed.commit());

    static_cast<void>(library->playlist_destination().set(source));

    wiola::testing::ScriptedPlayerDialogs dialogs;
    dialogs.chosen_path = std::filesystem::path{directory.filePath("export.m3u8").toStdString()};
    dialogs.prompted = [&] {
        static_cast<void>(library->playlist_destination().set(other));
    };

    auto commands = router(dialogs);
    commands.export_playlist(source);

    QFile file{directory.filePath("export.m3u8")};

    ASSERT_TRUE(file.open(QIODevice::ReadOnly));

    const auto bytes = file.readAll();

    EXPECT_TRUE(bytes.contains("one.wav"));
    EXPECT_FALSE(bytes.contains("two.wav"));
}

TEST_F(DocumentCommandRouterTest,
    ReparentedInspectorEditsWithoutRetainingTheWindowAndRejectsAfterLibraryDeath)
{
    QWidget survivor;
    auto window = std::make_unique<gui::MainWindow>(playback, *library, *preferences);
    auto* inspector =
        static_cast<gui::TrackInspector*>(window->findChild<QWidget*>("trackInspector"));

    ASSERT_NE(inspector, nullptr);

    gui::InspectorContext context;
    context.origin = gui::InspectorContext::Origin::selection;
    context.editable = {*library->repository().track(library::MediaId{"one"})};
    context.tracks = {gui::present_record(library::PlaylistEntryId{"entry"}, context.editable[0])};
    inspector->show_context(context);
    inspector->setParent(&survivor);
    window.reset();

    auto* title = inspector->findChild<QLineEdit*>("standardTitle");
    title->setText("After closing window");
    inspector->findChild<QPushButton*>("applyStandardMetadata")->click();
    EXPECT_EQ(library->repository().track(library::MediaId{"one"})->display_title(),
        "After closing window");

    library.reset();
    title->setText("Unaccepted draft");
    inspector->findChild<QPushButton*>("applyStandardMetadata")->click();
    EXPECT_EQ(title->text(), "Unaccepted draft");
}

TEST_F(DocumentCommandRouterTest, TrackPickerAppendReplaceClearAndQueueCommandsPersistTheirScope)
{
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    dialogs.paths = {"one.wav", "two.wav"};
    commands.choose_tracks(true);
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(dialogs.prompts, 1);

    const auto selected = playback.playlist().tracks()[1].id();
    gui::DocumentCommandContext context{gui::DocumentId::queue(), gui::TrackCapabilities::queue(),
        {{library::MediaId{"two"}, selected, "two.wav"}}, {}};
    commands.execute(gui::TrackCommand::play, context);
    EXPECT_EQ(playback.playlist().current()->id(), selected);
    commands.execute(gui::TrackCommand::remove, context);
    ASSERT_EQ(playback.playlist().tracks().size(), 1);
    EXPECT_NE(playback.playlist().tracks().front().id(), selected);

    context.selection.clear();
    commands.execute(gui::TrackCommand::play, context);
    EXPECT_EQ(playback.playlist().tracks().size(), 1);
    context.displayed_paths = {"two.wav", "one.wav"};
    commands.execute(gui::TrackCommand::apply_order, context);
    ASSERT_EQ(playback.playlist().tracks().size(), 2);
    EXPECT_EQ(playback.playlist().tracks().front().resource(), media::local_resource("two.wav"));

    dialogs.paths = {"replacement.wav"};
    commands.choose_tracks();
    ASSERT_EQ(playback.playlist().tracks().size(), 1);
    EXPECT_EQ(playback.playlist().tracks().front().resource(),
        media::local_resource("replacement.wav"));
    commands.clear_queue();
    EXPECT_TRUE(playback.playlist().tracks().empty());
    EXPECT_TRUE(outcomes.back().queue_changed);
}

TEST_F(DocumentCommandRouterTest, FolderChoiceCreatesWorkspaceAndRescanPersistsRecursion)
{
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    dialogs.chosen_path = platform::qt::QtPathAdapter::from_qt(directory.path());
    commands.choose_folder();
    ASSERT_FALSE(outcomes.empty());
    ASSERT_TRUE(outcomes.back().open_workspace);
    const auto id = *outcomes.back().open_workspace;
    ASSERT_NE(library->repository().workspace(id), nullptr);

    commands.rescan(id, false);
    EXPECT_FALSE(library->repository().workspace(id)->recursive);
    commands.rescan(id, false);
    library->cancel_workspace_scan(id);
    commands.rescan(library::WorkspaceId{"absent"}, true);
    EXPECT_EQ(library->repository().workspaces().size(), 1);
}

TEST_F(DocumentCommandRouterTest, PlaylistImportReportsSuccessFailureAndCancellationSeparately)
{
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    commands.import_playlist();
    EXPECT_TRUE(outcomes.empty());

    dialogs.chosen_path = platform::qt::QtPathAdapter::from_qt(directory.filePath("missing.m3u8"));
    commands.import_playlist();
    ASSERT_FALSE(outcomes.empty());
    EXPECT_TRUE(outcomes.back().message.contains("Could not import"));
    EXPECT_TRUE(library->repository().playlists().empty());

    QFile input{directory.filePath("list.m3u8")};
    ASSERT_TRUE(input.open(QIODevice::WriteOnly));
    input.write("#EXTM3U\none.wav\ntwo.wav\n");
    input.close();
    dialogs.chosen_path = platform::qt::QtPathAdapter::from_qt(input.fileName());
    commands.import_playlist();
    ASSERT_TRUE(outcomes.back().open_playlist);
    const auto id = *outcomes.back().open_playlist;
    ASSERT_NE(library->repository().playlist(id), nullptr);
    EXPECT_EQ(library->repository().playlist(id)->entries.size(), 2);

    dialogs.chosen_path.reset();
    const auto count = outcomes.size();
    commands.export_playlist(id);
    EXPECT_EQ(outcomes.size(), count);
    commands.play_saved(library->playlists().create("Empty").id);
    EXPECT_TRUE(outcomes.back().message.contains("no playable tracks"));
}

TEST_F(DocumentCommandRouterTest, ClosedLibraryRejectsEveryCommandAndNeverOpensDialogs)
{
    wiola::testing::ScriptedPlayerDialogs dialogs;
    auto commands = router(dialogs);
    library.reset();

    commands.choose_tracks();
    commands.choose_folder();
    commands.import_playlist();
    commands.export_playlist(library::PlaylistId{"saved"});
    commands.play_saved(library::PlaylistId{"saved"});
    commands.rescan(library::WorkspaceId{"folder"}, true);
    commands.clear_queue();
    commands.open({"one.wav"});
    commands.execute(gui::TrackCommand::remove,
        {gui::DocumentId::queue(), gui::TrackCapabilities::all(), {}, {}});

    EXPECT_EQ(dialogs.prompts, 0);
    EXPECT_TRUE(outcomes.empty());
    EXPECT_TRUE(playback.playlist().tracks().empty());
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM",
            qEnvironmentVariableIsEmpty("WIOLA_GUI_PREVIEW_DIR") ? "minimal" : "offscreen");

    QApplication app{argc, argv};

    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
