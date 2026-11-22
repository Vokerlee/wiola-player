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

#include <gui/shell/main_window.hpp>

#include <gui/appearance/theme_editor.hpp>
#include <gui/layout/classic_layout.hpp>
#include <gui/tracks/tag_filter_panel.hpp>
#include <gui/tracks/track_table_view.hpp>

#include <QApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
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
    EXPECT_NE(action(*file, "Open folder..."), nullptr);
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

    QWidget* tags = window.findChild<QWidget*>("customTagEditor");
    QPushButton* add_tag = window.findChild<QPushButton*>("addCustomTag");
    QLabel* tag_explanation = window.findChild<QLabel*>("customTagExplanation");
    ASSERT_NE(tags, nullptr);
    ASSERT_NE(add_tag, nullptr);
    ASSERT_NE(tag_explanation, nullptr);
    EXPECT_FALSE(add_tag->isEnabled());
    EXPECT_EQ(tag_explanation->text(), "Select a track to edit shared custom tags.");
}

TEST_F(MainWindowShell, LayoutSwitchPreservesQueueSelectionFiltersAndActiveDocument)
{
    const std::filesystem::path first{"/tmp/wiola-layout-first.mp3"};
    const std::filesystem::path second{"/tmp/wiola-layout-second.mp3"};
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    window.open({first, second});

    auto* now_playing = window.findChild<wiola::gui::NowPlayingDocument*>();
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(now_playing, nullptr);
    ASSERT_NE(documents, nullptr);

    auto* table = now_playing->findChild<wiola::gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->total_count(), 2);

    table->selectRow(1);
    const std::vector<wiola::engine::QueueEntryId> selected = now_playing->chosen_tracks();
    ASSERT_EQ(selected.size(), 1);

    QLineEdit* key = window.findChild<QLineEdit*>("tagFilterKey");
    QLineEdit* value = window.findChild<QLineEdit*>("tagFilterValue");
    QPushButton* exclude = window.findChild<QPushButton*>("excludeTagFilter");
    ASSERT_NE(key, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(exclude, nullptr);
    key->setText("mood");
    value->setText("calm");
    exclude->click();
    ASSERT_EQ(table->visible_count(), 2);
    ASSERT_EQ(now_playing->chosen_tracks(), selected);

    documents->setCurrentIndex(1);
    QWidget* active = documents->currentWidget();
    QWidget* previous_root = window.centralWidget();
    wiola::gui::LayoutDocument layout = wiola::gui::classic_layout_document();
    layout.root.id = "switchedRoot";

    ASSERT_TRUE(window.switch_layout(layout));

    EXPECT_NE(window.centralWidget(), previous_root);
    EXPECT_EQ(window.findChild<wiola::gui::NowPlayingDocument*>(), now_playing);
    EXPECT_EQ(window.findChild<QTabWidget*>("documentTabs"), documents);
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(now_playing->chosen_tracks(), selected);
    EXPECT_EQ(table->visible_count(), 2);
    ASSERT_EQ(session.playlist().tracks().size(), 2);
    EXPECT_EQ(session.playlist().tracks()[0].path(), first);
    EXPECT_EQ(session.playlist().tracks()[1].path(), second);
    EXPECT_EQ(session.playlist().position(), 0);
}

TEST_F(MainWindowShell, NestedContainersRoundTripWithoutReplacingOpenDocuments)
{
    using wiola::gui::LayoutComponentKind;
    using wiola::gui::LayoutNode;
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    window.open({"/tmp/wiola-nested-layout.mp3"});

    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    auto* inspector = window.findChild<QWidget*>("trackInspector");
    auto* area = window.findChild<QSplitter*>("documentArea");
    ASSERT_NE(documents, nullptr);
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(area, nullptr);
    documents->setCurrentIndex(1);
    QWidget* active = documents->currentWidget();

    auto layout = wiola::gui::classic_layout_document();
    auto found = std::ranges::find_if(layout.root.children,
        [](const LayoutNode& node) { return node.id == "documentArea"; });
    ASSERT_NE(found, layout.root.children.end());
    ASSERT_EQ(found->children.size(), 3);
    LayoutNode grid;
    grid.id = "documentGrid";
    grid.kind = LayoutComponentKind::grid;
    grid.columns = 2;
    grid.children = {found->children[0], found->children[1]};
    LayoutNode tabs;
    tabs.id = "inspectorTabs";
    tabs.kind = LayoutComponentKind::tabs;
    tabs.children = {found->children[2]};
    tabs.children.front().title = "Track details";
    found->orientation = Qt::Vertical;
    found->sizes = {2, 1};
    found->children = {grid, tabs};
    layout.node_count += 2;

    ASSERT_TRUE(window.switch_layout(layout));
    auto* built_grid = window.findChild<QWidget*>("documentGrid");
    auto* built_tabs = window.findChild<QTabWidget*>("inspectorTabs");
    ASSERT_NE(built_grid, nullptr);
    ASSERT_NE(built_tabs, nullptr);
    auto* cells = qobject_cast<QGridLayout*>(built_grid->layout());
    ASSERT_NE(cells, nullptr);
    ASSERT_NE(cells->itemAtPosition(0, 1), nullptr);
    EXPECT_EQ(cells->itemAtPosition(0, 1)->widget(), documents);
    EXPECT_EQ(built_tabs->widget(0), inspector);
    EXPECT_EQ(built_tabs->tabText(0), "Track details");
    EXPECT_EQ(area->orientation(), Qt::Vertical);
    EXPECT_EQ(documents->currentWidget(), active);

    ASSERT_TRUE(window.switch_layout(wiola::gui::classic_layout_document()));
    EXPECT_EQ(window.findChild<QTabWidget*>("documentTabs"), documents);
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(area->orientation(), Qt::Horizontal);
    EXPECT_EQ(area->widget(1), documents);
    EXPECT_EQ(area->widget(2), inspector);
    EXPECT_EQ(window.findChild<QWidget*>("documentGrid"), nullptr);
    EXPECT_EQ(window.findChild<QTabWidget*>("inspectorTabs"), nullptr);
    ASSERT_EQ(session.playlist().tracks().size(), 1);
    EXPECT_EQ(session.playlist().tracks().front().path(), "/tmp/wiola-nested-layout.mp3");
}

TEST_F(MainWindowShell, RejectedLayoutKeepsTheCurrentHierarchyAndDocuments)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    QWidget* document_area = window.findChild<QWidget*>("documentArea");
    QWidget* root = window.centralWidget();
    ASSERT_NE(documents, nullptr);
    ASSERT_NE(document_area, nullptr);
    ASSERT_NE(root, nullptr);

    documents->setCurrentIndex(1);
    QWidget* active = documents->currentWidget();
    QWidget* original_parent = document_area->parentWidget();

    wiola::gui::LayoutNode reused;
    reused.id = "documentArea";
    reused.kind = wiola::gui::LayoutComponentKind::stack;

    wiola::gui::LayoutNode invalid;
    invalid.id = "invalid";
    invalid.kind = static_cast<wiola::gui::LayoutComponentKind>(999);

    wiola::gui::LayoutDocument layout;
    layout.root.id = "invalidRoot";
    layout.root.kind = wiola::gui::LayoutComponentKind::row;
    layout.root.children = {reused, invalid};
    layout.node_count = 3;

    EXPECT_FALSE(window.switch_layout(layout));
    EXPECT_EQ(window.centralWidget(), root);
    EXPECT_EQ(document_area->parentWidget(), original_parent);
    EXPECT_EQ(documents->currentWidget(), active);
}

TEST_F(MainWindowShell, FocusedLayoutsCanRoundTripWithoutReplacingDocuments)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    auto* manager = static_cast<wiola::gui::SavedPlaylistManager*>(
        window.findChild<QWidget*>("savedPlaylistManager")
    );
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    auto* navigation = window.findChild<QWidget*>("libraryNavigation");
    auto* inspector = window.findChild<QWidget*>("trackInspector");
    auto* area = window.findChild<QSplitter*>("documentArea");
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(documents, nullptr);
    ASSERT_NE(navigation, nullptr);
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(area, nullptr);
    ASSERT_TRUE(manager->create("Focus"));

    QWidget* active = documents->currentWidget();
    QMenu* view = menu(window, "&View");
    ASSERT_NE(view, nullptr);
    QAction* queue = action(*view, "Queue Focus");
    QAction* library = action(*view, "Library Focus");
    QAction* classic = action(*view, "Classic");
    ASSERT_NE(queue, nullptr);
    ASSERT_NE(library, nullptr);
    ASSERT_NE(classic, nullptr);

    queue->trigger();

    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_TRUE(navigation->isHidden());
    EXPECT_TRUE(inspector->isHidden());
    EXPECT_TRUE(area->isHidden());

    library->trigger();

    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_FALSE(navigation->isHidden());
    EXPECT_FALSE(inspector->isHidden());
    EXPECT_TRUE(area->isHidden());

    classic->trigger();

    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_FALSE(navigation->isHidden());
    EXPECT_FALSE(inspector->isHidden());
    EXPECT_FALSE(area->isHidden());
    EXPECT_GE(area->indexOf(documents), 0);
    EXPECT_GE(area->indexOf(inspector), 0);
}

TEST_F(MainWindowShell, RestoresLayoutChoiceIndependentlyOfLibraryDocuments)
{
    {
        wiola::engine::Session session;
        wiola::gui::MainWindow window{session};
        auto* manager = static_cast<wiola::gui::SavedPlaylistManager*>(
            window.findChild<QWidget*>("savedPlaylistManager")
        );
        ASSERT_NE(manager, nullptr);
        ASSERT_TRUE(manager->create("Persistent"));

        action(*menu(window, "&View"), "Queue Focus")->trigger();

        QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
        EXPECT_EQ(settings.value("layout/preset").toString(), "queue-focus");
    }

    {
        wiola::engine::Session session;
        wiola::gui::MainWindow window{session};
        auto* documents = window.findChild<QTabWidget*>("documentTabs");
        ASSERT_NE(documents, nullptr);
        ASSERT_EQ(documents->count(), 3);
        EXPECT_EQ(documents->tabText(2), "Persistent");
        EXPECT_NE(window.findChild<QWidget*>("queueFocusRoot"), nullptr);
        EXPECT_TRUE(window.findChild<QWidget*>("libraryNavigation")->isHidden());
        EXPECT_TRUE(action(*menu(window, "&View"), "Queue Focus")->isChecked());

        action(*menu(window, "&View"), "Library Focus")->trigger();
    }

    wiola::engine::Session session;
    wiola::gui::MainWindow restored{session};
    auto* documents = restored.findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(documents, nullptr);
    ASSERT_EQ(documents->count(), 3);
    EXPECT_EQ(documents->tabText(2), "Persistent");
    EXPECT_NE(restored.findChild<QWidget*>("libraryFocusRoot"), nullptr);
    EXPECT_FALSE(restored.findChild<QWidget*>("libraryNavigation")->isHidden());
    EXPECT_TRUE(action(*menu(restored, "&View"), "Library Focus")->isChecked());
}

TEST_F(MainWindowShell, RestoresTheNativeThemeChoice)
{
    {
        wiola::engine::Session session;
        wiola::gui::MainWindow window{session};
        action(*menu(window, "&View"), "Dark theme")->trigger();
        EXPECT_TRUE(action(*menu(window, "&View"), "Dark theme")->isChecked());
    }

    wiola::engine::Session session;
    wiola::gui::MainWindow restored{session};
    EXPECT_TRUE(action(*menu(restored, "&View"), "Dark theme")->isChecked());
    EXPECT_EQ(restored.palette().color(QPalette::Window), QColor{"#202124"});
}

TEST_F(MainWindowShell, AppliesAndRestoresAValidatedCustomTheme)
{
    {
        wiola::engine::Session session;
        wiola::gui::MainWindow window{session};
        action(*menu(window, "&Tools"), "Edit theme...")->trigger();

        auto* dialog = window.findChild<QDialog*>("themeEditor");
        ASSERT_NE(dialog, nullptr);
        auto* editor = static_cast<wiola::gui::ThemeEditor*>(dialog);
        editor->set_color(wiola::gui::ThemeColorRole::window, QColor{"#f0f0f0"});
        dialog->findChild<QPushButton*>("applyThemeEdit")->click();
        EXPECT_EQ(window.palette().color(QPalette::Window), QColor{"#f0f0f0"});
    }

    wiola::engine::Session session;
    wiola::gui::MainWindow restored{session};
    EXPECT_EQ(restored.palette().color(QPalette::Window), QColor{"#f0f0f0"});
}

TEST_F(MainWindowShell, LayoutPreviewCommandsAndCancelDoNotTouchPlayerState)
{
    wiola::engine::Session session;
    wiola::gui::MainWindow window{session};
    window.open({"/tmp/wiola-preview-queue.mp3"});
    auto* manager = static_cast<wiola::gui::SavedPlaylistManager*>(
        window.findChild<QWidget*>("savedPlaylistManager")
    );
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(documents, nullptr);
    ASSERT_TRUE(manager->create("Unchanged"));
    QWidget* active = documents->currentWidget();

    action(*menu(window, "&Tools"), "Edit button layout...")->trigger();
    auto* editor = window.findChild<QDialog*>("buttonLayoutEditor");
    ASSERT_NE(editor, nullptr);
    auto* tree = editor->findChild<QTreeWidget*>("layoutHierarchy");
    auto* palette = editor->findChild<QListWidget*>("layoutComponentPalette");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(palette, nullptr);
    const auto transport =
        tree->findItems("transportControls", Qt::MatchExactly | Qt::MatchRecursive);
    ASSERT_FALSE(transport.empty());
    tree->setCurrentItem(transport.front());
    palette->setCurrentRow(0);
    editor->findChild<QPushButton*>("addLayoutAction")->click();

    auto* preview = editor->findChild<QWidget*>("isolatedLayoutPreview");
    ASSERT_NE(preview, nullptr);
    auto* preview_command = preview->findChild<QPushButton*>("open");
    ASSERT_NE(preview_command, nullptr);
    preview_command->click();

    EXPECT_EQ(session.playlist().tracks().size(), 1);
    EXPECT_EQ(session.playlist().tracks()[0].path(), "/tmp/wiola-preview-queue.mp3");
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(editor->result(), 0);

    editor->findChild<QPushButton*>("cancelLayoutEdit")->click();
    EXPECT_EQ(editor->result(), QDialog::Rejected);
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(window.centralWidget()->findChild<QPushButton*>("open"), nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
