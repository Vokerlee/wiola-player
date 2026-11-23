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

#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <codec/provider.hpp>
#include <gtest/gtest.h>
#include <gui/fixtures/player_dialogs.hpp>
#include <gui/shell/document_command_router.hpp>
#include <gui/shell/document_host.hpp>
#include <gui/shell/main_window.hpp>
#include <gui/tracks/track_browser.hpp>

namespace {

using namespace wiola;

class DocumentCommandRouterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        library =
            std::make_unique<app::LibrarySession>(std::make_shared<codec::LocalMediaProvider>());
        preferences = std::make_unique<app::PlaybackPreferences>(playback);
        for (const auto& name : {"one", "two"}) {
            library::TrackRecord track;
            track.id = {name};
            track.locations = {std::string{name} + ".wav"};
            track.embedded.title = name;
            ASSERT_TRUE(library->repository().put_track(track));
        }
    }

    gui::DocumentCommandRouter router(wiola::testing::ScriptedPlayerDialogs& dialogs)
    {
        return {*library, playback, preferences->queue(), dialogs,
            [this](const auto& outcome) { outcomes.push_back(outcome); }};
    }

    QTemporaryDir directory;
    engine::Session session;
    app::PlaybackService playback{session};
    std::unique_ptr<app::LibrarySession> library;
    std::unique_ptr<app::PlaybackPreferences> preferences;
    std::vector<gui::DocumentCommandOutcome> outcomes;
};

TEST_F(DocumentCommandRouterTest,
    MoveCapturesDuplicateOccurrenceBeforePromptChangesDocumentAndSelection)
{
    const auto source = library->playlists().create("Source"),
               destination = library->playlists().create("Destination"),
               other = library->playlists().create("Other");
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"one"});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
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
        static_cast<void>(library->active_playlist().set(other));
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
    const auto source = library->playlists().create("Duplicates");
    const auto first = library->repository().append_track(source, library::MediaId{"one"});
    const auto second = library->repository().append_track(source, library::MediaId{"one"});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
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
    EXPECT_EQ(playback.playlist().tracks()[1].path(), std::filesystem::path{"one.wav"});
}

TEST_F(DocumentCommandRouterTest, DisabledCapabilitiesAndCancelledPromptsNeverMutateDocuments)
{
    const auto destination = library->playlists().create("Destination");
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

TEST_F(DocumentCommandRouterTest, ExportCapturesPlaylistBeforePromptChangesTheDestination)
{
    const auto source = library->playlists().create("Source"),
               other = library->playlists().create("Other");
    ASSERT_TRUE(library->repository().append_track(source, library::MediaId{"one"}));
    ASSERT_TRUE(library->repository().append_track(other, library::MediaId{"two"}));
    static_cast<void>(library->active_playlist().set(source));
    wiola::testing::ScriptedPlayerDialogs dialogs;
    dialogs.chosen_path = std::filesystem::path{directory.filePath("export.m3u8").toStdString()};
    dialogs.prompted = [&] {
        static_cast<void>(library->active_playlist().set(other));
    };
    auto commands = router(dialogs);
    commands.export_playlist({});
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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
