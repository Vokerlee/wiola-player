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

#include <gui/saved_playlist_manager.hpp>
#include <gui/saved_playlist_storage.hpp>

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QTemporaryDir>

#include <gtest/gtest.h>

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
    wiola::gui::SavedPlaylistStorage storage{file()};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(records) == wiola::gui::LibraryJsonError::none;
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

TEST_F(SavedPlaylistManager, ReportsSaveFailureAndValidatesSelectionAndNames)
{
    ASSERT_TRUE(QDir{directory.path()}.mkdir("blocked"));

    wiola::gui::SavedPlaylistStorage storage{file("blocked")};
    auto persist = [&storage](const wiola::library::LibraryRepository& records) {
        return storage.save(records) == wiola::gui::LibraryJsonError::none;
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

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
