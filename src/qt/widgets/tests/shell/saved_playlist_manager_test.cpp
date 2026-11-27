/**
 * @file
 * @brief Saved playlist manager regression tests.
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

#include <qt/storage/library_store.hpp>
#include <qt/widgets/shell/saved_playlist_manager.hpp>
#include <qt/widgets/tests/support/modal_answer.hpp>
#include <wiola/app/library/saved_playlist_commands.hpp>

#include <QApplication>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <memory>

namespace {

class SavedPlaylistManager : public testing::Test {
protected:
    std::filesystem::path file(const char* name = "library.json") const
    {
        return std::filesystem::path{directory.filePath(name).toStdString()};
    }

    QTemporaryDir directory;
    wiola::library::InMemoryRepository repository;
    wiola::gui::LibraryNavigation navigation;
};

TEST_F(SavedPlaylistManager, CreatesRenamesDuplicatesAndDeletesWithVisibleSavedState)
{
    wiola::persistence::qt::LibraryStore storage{file()};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(wiola::library::snapshot_of(records)) ==
            wiola::persistence::qt::LibraryStoreResult{};
    };
    wiola::app::SavedPlaylistCommands commands{repository, persist};
    wiola::gui::SavedPlaylistManager manager{repository, commands, navigation};
    QLabel* state = manager.findChild<QLabel*>("playlistSaveState");

    ASSERT_NE(state, nullptr);
    ASSERT_NE(manager.findChild<QPushButton*>("createPlaylist"), nullptr);
    ASSERT_NE(manager.findChild<QPushButton*>("renamePlaylist"), nullptr);
    ASSERT_NE(manager.findChild<QPushButton*>("duplicatePlaylist"), nullptr);
    ASSERT_NE(manager.findChild<QPushButton*>("deletePlaylist"), nullptr);

    ASSERT_TRUE(manager.create("Road"));

    const auto original = navigation.selected_playlist();

    ASSERT_TRUE(original.has_value());
    EXPECT_EQ(repository.playlist(*original)->name, "Road");
    EXPECT_EQ(state->text(), "All changes saved");

    ASSERT_TRUE(manager.rename_selected("Evening"));
    EXPECT_EQ(repository.playlist(*original)->name, "Evening");

    ASSERT_TRUE(manager.duplicate_selected());

    const auto copy = navigation.selected_playlist();

    ASSERT_TRUE(copy.has_value());
    EXPECT_NE(*copy, *original);
    EXPECT_EQ(repository.playlist(*copy)->name, "Evening copy");

    ASSERT_TRUE(manager.remove_selected());
    EXPECT_EQ(repository.playlist(*copy), nullptr);
    EXPECT_NE(repository.playlist(*original), nullptr);
    EXPECT_EQ(state->text(), "All changes saved");
}

TEST_F(SavedPlaylistManager, RenamingFromTheContextMenuUsesTheCurrentPlaylistName)
{
    wiola::app::SavedPlaylistCommands commands{repository, [](const auto&) { return true; }};
    wiola::gui::SavedPlaylistManager manager{repository, commands, navigation};

    ASSERT_TRUE(manager.create("Before"));

    const auto id = navigation.selected_playlist();

    ASSERT_TRUE(id);

    QTimer::singleShot(0, &manager, [] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
                EXPECT_EQ(dialog->textValue(), "Before");

                dialog->setTextValue("After");
                dialog->accept();
            }
    });
    manager.findChild<QPushButton*>("renamePlaylist")->click();
    EXPECT_EQ(repository.playlist(*id)->name, "After");
}

TEST_F(SavedPlaylistManager, ReportsSaveFailureAndValidatesSelectionAndNames)
{
    ASSERT_TRUE(QDir{directory.path()}.mkdir("blocked"));

    wiola::persistence::qt::LibraryStore storage{file("blocked")};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(wiola::library::snapshot_of(records)) ==
            wiola::persistence::qt::LibraryStoreResult{};
    };
    wiola::app::SavedPlaylistCommands commands{repository, persist};
    wiola::gui::SavedPlaylistManager manager{repository, commands, navigation};
    QLabel* state = manager.findChild<QLabel*>("playlistSaveState");

    EXPECT_FALSE(manager.duplicate_selected());
    EXPECT_EQ(state->text(), "Select a playlist first");

    EXPECT_FALSE(manager.create("   "));
    EXPECT_EQ(state->text(), "Enter a playlist name");

    EXPECT_FALSE(manager.create("Cannot save"));
    EXPECT_EQ(state->text(), "Save failed; changes restored");
    EXPECT_TRUE(repository.playlists().empty());
}

TEST_F(SavedPlaylistManager, PendingRevisionsCannotBeFalselyAcknowledgedByAnOlderSave)
{
    using namespace wiola;

    library::InMemoryRepository repository;
    std::vector<std::shared_ptr<library::CommitReceipt>> receipts;
    app::SavedPlaylistCommands commands{repository,
        [&](const auto&) {
            const auto revision = repository.staged_delta()->changes.revision;
            auto receipt = std::make_shared<library::CommitReceipt>(revision);
            receipts.push_back(receipt);

            return library::CommitResult::accepted(revision, receipt);
        }};
    gui::LibraryNavigation navigation;
    auto manager = std::make_unique<gui::SavedPlaylistManager>(repository, commands, navigation);

    ASSERT_TRUE(manager->create("First"));

    auto* status = manager->findChild<QLabel*>("playlistSaveState");

    ASSERT_NE(status, nullptr);
    ASSERT_TRUE(manager->rename_selected("First"));
    EXPECT_EQ(receipts.size(), 1U);
    EXPECT_EQ(status->text(), "Changes accepted; saving library");
    ASSERT_TRUE(manager->rename_selected("Second"));
    EXPECT_EQ(status->text(), "Changes accepted; saving library");
    ASSERT_EQ(receipts.size(), 2);

    receipts[0]->notify(true);
    EXPECT_EQ(status->text(), "Changes accepted; saving library");

    receipts[1]->notify(false);
    EXPECT_EQ(status->text(), "Changes remain in memory; retry saving library");

    receipts[1]->notify(true);
    EXPECT_EQ(status->text(), "All changes saved");
    ASSERT_TRUE(manager->rename_selected("Third"));

    manager.reset();
    receipts.back()->notify(true);
}

TEST_F(SavedPlaylistManager, CreateAndDeletePromptsRespectCancellationAndSelection)
{
    wiola::app::SavedPlaylistCommands commands{repository, [](const auto&) { return true; }};
    wiola::gui::SavedPlaylistManager manager{repository, commands, navigation};
    EXPECT_FALSE(manager.rename_selected("Name"));
    EXPECT_FALSE(manager.remove_selected());
    {
        wiola::testing::ModalAnswer<QInputDialog> answer{[](auto& dialog) { dialog.reject(); }};
        manager.findChild<QPushButton*>("createPlaylist")->click();
    }
    EXPECT_TRUE(repository.playlists().empty());
    {
        wiola::testing::ModalAnswer<QInputDialog> answer{[](auto& dialog) {
            dialog.setTextValue("New list");
            dialog.accept();
        }};
        manager.findChild<QPushButton*>("createPlaylist")->click();
    }
    ASSERT_TRUE(navigation.selected_playlist());
    const auto saved = *navigation.selected_playlist();
    EXPECT_FALSE(manager.rename_selected("   "));
    EXPECT_EQ(repository.playlist(saved)->name, "New list");
    for (const auto answer : {QMessageBox::No, QMessageBox::Yes}) {
        wiola::testing::ModalAnswer<QMessageBox> response{
            [&](auto& dialog) { dialog.button(answer)->click(); }};
        manager.findChild<QPushButton*>("deletePlaylist")->click();
        EXPECT_EQ(repository.playlist(saved) == nullptr, answer == QMessageBox::Yes);
    }
}

} // namespace

TEST_F(SavedPlaylistManager, CommandCompletionMayCloseTheManagerBeforePresentation)
{
    std::unique_ptr<wiola::gui::SavedPlaylistManager> manager;
    wiola::app::SavedPlaylistCommands commands{repository, [&](const auto&) {
                                                   manager.reset();
                                                   return true;
                                               }};
    manager = std::make_unique<wiola::gui::SavedPlaylistManager>(repository, commands, navigation);
    EXPECT_TRUE(manager->create("Accepted before close"));
    EXPECT_FALSE(manager);
    ASSERT_EQ(repository.playlists().size(), 1);
}

TEST_F(SavedPlaylistManager, NavigationSelectionMayCloseTheManagerBeforeStatusDelivery)
{
    wiola::app::SavedPlaylistCommands commands{repository, [](const auto&) { return true; }};
    auto manager =
        std::make_unique<wiola::gui::SavedPlaylistManager>(repository, commands, navigation);
    QObject::connect(&navigation, &QTreeWidget::currentItemChanged, &navigation,
        [&](auto*, auto*) { manager.reset(); });

    EXPECT_TRUE(manager->create("Selected before close"));
    EXPECT_FALSE(manager);
    EXPECT_TRUE(navigation.selected_playlist());
}

TEST_F(SavedPlaylistManager, ActiveObserverRetainsItsStateAfterClosingTheManager)
{
    wiola::app::SavedPlaylistCommands commands{repository, [](const auto&) { return true; }};
    auto manager =
        std::make_unique<wiola::gui::SavedPlaylistManager>(repository, commands, navigation);
    int observed{};
    manager->on_changed([&, calls = 0]() mutable {
        manager.reset();
        observed = ++calls;
    });
    EXPECT_TRUE(manager->create("Observed close"));
    EXPECT_FALSE(manager);
    EXPECT_EQ(observed, 1);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);

    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
