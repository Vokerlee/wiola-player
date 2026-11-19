/**
 * @file
 * @brief Native application-shell tests for the main window.
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

#include <gui/main_window.hpp>

#include <QApplication>
#include <QGroupBox>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

namespace {

QMenu* menu(QMainWindow& window, const QString& title)
{
    const QList<QMenu*> menus{window.menuBar()->findChildren<QMenu*>()};
    const auto found = std::ranges::find_if(menus,
        [&title](const QMenu* candidate) { return candidate->title() == title; });

    return found == menus.end() ? nullptr : *found;
}

QAction* action(QMenu& menu, const QString& text)
{
    const QList<QAction*> actions{menu.actions()};
    const auto found = std::ranges::find_if(actions,
        [&text](const QAction* candidate) { return candidate->text() == text; });

    return found == actions.end() ? nullptr : *found;
}

QPushButton* button(QMainWindow& window, const QString& text)
{
    const QList<QPushButton*> buttons{window.findChildren<QPushButton*>()};
    const auto found = std::ranges::find_if(buttons, [&text](const QPushButton* candidate) {
        return candidate->text() == text || candidate->accessibleName() == text;
    });

    return found == buttons.end() ? nullptr : *found;
}

class MainWindowShell : public testing::Test {
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
        settings.sync();
    }

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
};

TEST_F(MainWindowShell, MenusSharePlayerCommandsWithButtonsAndShortcuts)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};

    ASSERT_NE(window.centralWidget(), nullptr);

    QMenu* file = menu(window, "&File");
    QMenu* playback = menu(window, "&Playback");
    QMenu* tools = menu(window, "&Tools");

    ASSERT_NE(file, nullptr);
    ASSERT_NE(playback, nullptr);
    ASSERT_NE(tools, nullptr);
    EXPECT_NE(action(*file, "Open tracks"), nullptr);
    EXPECT_NE(action(*file, "Add"), nullptr);
    EXPECT_NE(action(*file, "Clear"), nullptr);
    EXPECT_NE(action(*file, "Import M3U8..."), nullptr);
    EXPECT_NE(action(*file, "Export M3U8..."), nullptr);
    EXPECT_NE(action(*tools, "EQ"), nullptr);

    QAction* play = action(*playback, "Play");
    QAction* repeat = action(*playback, "Repeat: off");
    QAction* shuffle = action(*playback, "Shuffle");
    QPushButton* play_button = button(window, "Play");
    QPushButton* repeat_button = button(window, "Repeat: off");
    QPushButton* shuffle_button = button(window, "Shuffle");

    ASSERT_NE(play, nullptr);
    ASSERT_NE(repeat, nullptr);
    ASSERT_NE(shuffle, nullptr);
    ASSERT_NE(play_button, nullptr);
    ASSERT_NE(repeat_button, nullptr);
    ASSERT_NE(shuffle_button, nullptr);

    EXPECT_FALSE(play->isEnabled());
    EXPECT_FALSE(play_button->isEnabled());
    EXPECT_FALSE(play->shortcut().isEmpty());

    repeat->trigger();

    EXPECT_EQ(repeat->text(), "Repeat: all");
    EXPECT_EQ(repeat_button->text(), "Repeat: all");

    shuffle->trigger();

    EXPECT_TRUE(shuffle->isChecked());
    EXPECT_TRUE(shuffle_button->isChecked());
}

TEST_F(MainWindowShell, DocumentTabsKeepNowPlayingIndependentOfTheVisibleDocument)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    QTabWidget* documents = window.findChild<QTabWidget*>("documentTabs");
    wiola::gui::NowPlayingDocument* now_playing =
        window.findChild<wiola::gui::NowPlayingDocument*>();
    QPushButton* play_button = button(window, "Play");

    ASSERT_NE(documents, nullptr);
    ASSERT_NE(now_playing, nullptr);
    ASSERT_NE(play_button, nullptr);
    ASSERT_EQ(documents->count(), 2);

    EXPECT_EQ(documents->tabText(0), "Now Playing");
    EXPECT_EQ(documents->tabText(1), "Folder Workspace");
    EXPECT_EQ(documents->indexOf(now_playing), 0);
    EXPECT_EQ(documents->currentWidget(), now_playing);
    EXPECT_FALSE(play_button->isEnabled());

    documents->setCurrentIndex(1);

    EXPECT_EQ(documents->currentIndex(), 1);
    EXPECT_EQ(documents->indexOf(now_playing), 0);
    EXPECT_EQ(window.windowTitle(), "Wiola Player");
    EXPECT_FALSE(play_button->isEnabled());
}

TEST_F(MainWindowShell, RestoresSavedPlaylistEditorsWithoutChangingPlayback)
{
    SCOPED_TRACE("create and restore saved playlist tabs");

    {
        wiola::engine::Session session;
        wiola::gui::MainWindow window{session};
        wiola::gui::SavedPlaylistManager* manager = static_cast<wiola::gui::SavedPlaylistManager*>(
            window.findChild<QWidget*>("savedPlaylistManager")
        );
        QTabWidget* documents = window.findChild<QTabWidget*>("documentTabs");
        QPushButton* play_button = button(window, "Play");

        ASSERT_NE(manager, nullptr);
        ASSERT_NE(documents, nullptr);
        ASSERT_NE(play_button, nullptr);
        ASSERT_TRUE(manager->create("Road"));
        ASSERT_EQ(documents->count(), 3);

        EXPECT_EQ(documents->currentIndex(), 2);
        EXPECT_EQ(documents->tabText(2), "Road");
        EXPECT_FALSE(play_button->isEnabled());

        wiola::gui::SavedPlaylistDocument* editor =
            static_cast<wiola::gui::SavedPlaylistDocument*>(documents->widget(2));
        QPushButton* use_active = editor->findChild<QPushButton*>("useActivePlaylist");
        ASSERT_NE(use_active, nullptr);
        use_active->click();
        EXPECT_EQ(editor->findChild<QLabel*>("savedPlaylistHeading")->text(),
            "Road — Active destination");

        documents->setCurrentIndex(0);
        EXPECT_EQ(window.windowTitle(), "Wiola Player");
        EXPECT_EQ(editor->findChild<QLabel*>("savedPlaylistHeading")->text(),
            "Road — Active destination");
        EXPECT_FALSE(play_button->isEnabled());
    }

    SCOPED_TRACE("restore the second window");
    wiola::engine::Session restored_session;
    wiola::gui::MainWindow restored{restored_session};
    QTabWidget* documents = restored.findChild<QTabWidget*>("documentTabs");
    QPushButton* play_button = button(restored, "Play");

    ASSERT_NE(documents, nullptr);
    ASSERT_NE(play_button, nullptr);
    ASSERT_EQ(documents->count(), 3);
    EXPECT_EQ(documents->tabText(2), "Road");
    EXPECT_EQ(static_cast<wiola::gui::SavedPlaylistDocument*>(documents->widget(2))
                  ->findChild<QLabel*>("savedPlaylistHeading")
                  ->text(),
        "Road — Active destination");
    EXPECT_EQ(documents->currentIndex(), 0);
    EXPECT_FALSE(play_button->isEnabled());
}

TEST_F(MainWindowShell, GroupsControlsAndExposesTheirPurposeAndKeyboardFocus)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    QGroupBox* playback = window.findChild<QGroupBox*>("playbackGroup");
    QGroupBox* queue = window.findChild<QGroupBox*>("queueCommandsGroup");
    QPushButton* play = button(window, "Play");
    QPushButton* boost = button(window, "+");
    QPushButton* equalizer = button(window, "EQ");

    ASSERT_NE(playback, nullptr);
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(play, nullptr);
    ASSERT_NE(boost, nullptr);
    ASSERT_NE(equalizer, nullptr);

    EXPECT_EQ(playback->title(), "Now playing");
    EXPECT_EQ(queue->title(), "Queue commands");
    EXPECT_GE(window.minimumWidth(), 980);
    EXPECT_GE(window.minimumHeight(), 640);
    EXPECT_EQ(play->focusPolicy(), Qt::StrongFocus);
    EXPECT_EQ(boost->focusPolicy(), Qt::StrongFocus);
    EXPECT_EQ(boost->accessibleName(), "Boost volume above 100%");
    EXPECT_EQ(equalizer->accessibleName(), "Equalizer");
    EXPECT_FALSE(boost->toolTip().isEmpty());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
