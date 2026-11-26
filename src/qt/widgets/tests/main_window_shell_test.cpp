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

#include <wiola/media/codecs/ffmpeg/composition.hpp>

#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <qt/widgets/controls/equalizer_panel.hpp>
#include <qt/widgets/documents/now_playing_document.hpp>
#include <qt/widgets/documents/saved_playlist_document.hpp>
#include <qt/widgets/documents/workspace_document.hpp>
#include <qt/widgets/metadata/track_inspector.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <qt/widgets/shell/saved_playlist_manager.hpp>
#include <qt/widgets/tracks/track_browser.hpp>
#include <wiola/app/playback/volume_control.hpp>
#include <wiola/media/local/resource.hpp>

#include <app/library/local_identity.hpp>
#include <qt/storage/library_store.hpp>
#include <qt/widgets/appearance/theme_editor.hpp>
#include <qt/widgets/layout/classic_layout.hpp>
#include <qt/widgets/layout/editor/layout_editor_dialog.hpp>
#include <qt/widgets/tracks/tag_filter_panel.hpp>
#include <qt/widgets/tracks/track_table_view.hpp>
#include <wiola/qt/paths.hpp>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <qt/application/player_application.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/layout/layout_tree.hpp>
#include <qt/widgets/shell/player_dialogs.hpp>
#include <qt/widgets/tests/journeys/session_journey.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>

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

class MainWindowShell : public ::testing::Test {
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

    inline static std::unique_ptr<QTemporaryDir> settings_directory;
};

TEST_F(MainWindowShell, MenusSharePlayerCommandsWithButtonsAndShortcuts)
{
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};

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
    EXPECT_NE(action(*file, "Import playlist..."), nullptr);
    EXPECT_NE(action(*file, "Save playlist as..."), nullptr);
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
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
    QTabWidget* documents = window.findChild<QTabWidget*>("documentTabs");
    wiola::gui::NowPlayingDocument* now_playing =
        window.findChild<wiola::gui::NowPlayingDocument*>();
    QPushButton* play_button = button(window, "Play");

    ASSERT_NE(documents, nullptr);
    ASSERT_NE(now_playing, nullptr);
    ASSERT_NE(play_button, nullptr);
    ASSERT_EQ(documents->count(), 2);

    EXPECT_EQ(documents->tabText(0), "Now Playing");
    EXPECT_EQ(documents->tabText(1), "Open Folder");
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
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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
    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    wiola::gui::MainWindow restored{restored_playback, restored_library, restored_preferences};
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
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
    QGroupBox* playback = window.findChild<QGroupBox*>("playbackGroup");
    QGroupBox* queue = window.findChild<QGroupBox*>("queueCommandsGroup");
    QPushButton* play = button(window, "Play");
    QPushButton* boost = button(window, "+");
    QPushButton* equalizer = button(window, "EQ");

    ASSERT_NE(playback, nullptr);
    EXPECT_EQ(queue, nullptr);
    ASSERT_NE(play, nullptr);
    ASSERT_NE(boost, nullptr);
    ASSERT_NE(equalizer, nullptr);

    EXPECT_TRUE(playback->title().isEmpty());
    EXPECT_GE(window.minimumWidth(), 820);
    EXPECT_GE(window.minimumHeight(), 520);
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
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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
    ASSERT_EQ(window_playback.playlist().tracks().size(), 2);
    EXPECT_EQ(window_playback.playlist().tracks()[0].resource(),
        wiola::media::local_resource(first));
    EXPECT_EQ(window_playback.playlist().tracks()[1].resource(),
        wiola::media::local_resource(second));
    EXPECT_EQ(window_playback.playlist().position(), 0);
}

TEST_F(MainWindowShell, NestedContainersRoundTripWithoutReplacingOpenDocuments)
{
    using wiola::gui::LayoutComponentKind;
    using wiola::gui::LayoutNode;
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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
    auto found = layout.root.children.begin() + 1;
    found->id = "documentArea";
    found->kind = LayoutComponentKind::splitter;
    found->view = wiola::gui::component_view("documentArea");
    found->children = {
        {.id = "customLibrary",
         .kind = LayoutComponentKind::inspector,
         .view = wiola::gui::component_view("libraryNavigation")},
        {.id = "customDocuments",
         .kind = LayoutComponentKind::document_host,
         .view = wiola::gui::component_view("documentTabs")     },
        {.id = "customInspector",
         .kind = LayoutComponentKind::inspector,
         .view = wiola::gui::component_view("trackInspector")   }
    };
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
    layout.node_count = wiola::gui::layout_node_count(layout.root);

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
    EXPECT_TRUE(area->isHidden());
    EXPECT_EQ(window.findChild<QDockWidget*>("inspectorDock")->widget(), inspector);
    EXPECT_TRUE(window.findChild<QDockWidget*>("libraryDock")
            ->isAncestorOf(window.findChild<QWidget*>("libraryNavigation")));
    EXPECT_EQ(window.findChild<QWidget*>("documentGrid"), nullptr);
    EXPECT_EQ(window.findChild<QTabWidget*>("inspectorTabs"), nullptr);
    ASSERT_EQ(window_playback.playlist().tracks().size(), 1);
    EXPECT_EQ(window_playback.playlist().tracks().front().resource(),
        wiola::media::local_resource("/tmp/wiola-nested-layout.mp3"));
}

TEST_F(MainWindowShell, RejectedLayoutKeepsTheCurrentHierarchyAndDocuments)
{
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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
    EXPECT_EQ(action(*view, "Queue Focus"), nullptr);
    QAction* library = action(*view, "Library Focus");
    QAction* classic = action(*view, "Classic");
    ASSERT_NE(library, nullptr);
    ASSERT_NE(classic, nullptr);

    library->trigger();

    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_FALSE(navigation->isHidden());
    EXPECT_TRUE(window.findChild<QDockWidget*>("inspectorDock")->isHidden());
    EXPECT_TRUE(area->isHidden());

    classic->trigger();

    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_FALSE(navigation->isHidden());
    EXPECT_TRUE(window.findChild<QDockWidget*>("inspectorDock")->isHidden());
    EXPECT_TRUE(area->isHidden());
    EXPECT_TRUE(window.findChild<QDockWidget*>("libraryDock")->isAncestorOf(navigation));
    EXPECT_EQ(window.findChild<QDockWidget*>("inspectorDock")->widget(), inspector);
}

TEST_F(MainWindowShell, RestoresLayoutChoiceIndependentlyOfLibraryDocuments)
{
    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
        auto* manager = static_cast<wiola::gui::SavedPlaylistManager*>(
            window.findChild<QWidget*>("savedPlaylistManager")
        );
        ASSERT_NE(manager, nullptr);
        ASSERT_TRUE(manager->create("Persistent"));

        action(*menu(window, "&View"), "Library Focus")->trigger();

        QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
        EXPECT_FALSE(settings.contains("layout/preset"));
    }

    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
        auto* documents = window.findChild<QTabWidget*>("documentTabs");
        ASSERT_NE(documents, nullptr);
        ASSERT_EQ(documents->count(), 3);
        EXPECT_EQ(documents->tabText(2), "Persistent");
        EXPECT_NE(window.findChild<QWidget*>("libraryFocusRoot"), nullptr);
        EXPECT_FALSE(window.findChild<QDockWidget*>("libraryDock")->isHidden());
        EXPECT_TRUE(action(*menu(window, "&View"), "Library Focus")->isChecked());
    }

    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    wiola::gui::MainWindow restored{restored_playback, restored_library, restored_preferences};
    auto* documents = restored.findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(documents, nullptr);
    ASSERT_EQ(documents->count(), 3);
    EXPECT_EQ(documents->tabText(2), "Persistent");
    EXPECT_NE(restored.findChild<QWidget*>("libraryFocusRoot"), nullptr);
    EXPECT_FALSE(restored.findChild<QWidget*>("libraryNavigation")->isHidden());
    EXPECT_TRUE(action(*menu(restored, "&View"), "Library Focus")->isChecked());
}

TEST_F(MainWindowShell, RetiredQueueFocusSettingsOpenClassicWithBottomTransport)
{
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    settings.setValue("layout/preset", "queue-focus");
    settings.sync();
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};
    wiola::app::PlaybackPreferences preferences{playback};
    {
        wiola::gui::MainWindow window{playback, library, preferences};
        EXPECT_NE(window.findChild<QWidget*>("classicRoot"), nullptr);
        EXPECT_EQ(action(*menu(window, "&View"), "Queue Focus"), nullptr);
        EXPECT_TRUE(action(*menu(window, "&View"), "Classic")->isChecked());
    }

    auto legacy = wiola::gui::classic_layout_document();
    legacy.root.id = "queueFocusRoot";
    std::swap(legacy.root.children[0], legacy.root.children[2]);
    QFile file{QFileInfo{settings.fileName()}.dir().filePath("layout.json")};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write(QJsonDocument{
                  QJsonObject{{"schema", 1}, {"choice", "queue-focus"},
                              {"layout",
                          QJsonDocument::fromJson(wiola::gui::write_layout_json(legacy)).object()}}
    }
                      .toJson()),
        0);
    file.close();

    wiola::gui::MainWindow restored{playback, library, preferences};
    restored.show();
    QApplication::processEvents();
    auto* root = restored.findChild<QWidget*>("classicRoot");
    auto* transport = restored.findChild<QWidget*>("playbackGroup");
    auto* tabs = restored.findChild<QWidget*>("documentTabs");
    ASSERT_NE(root, nullptr);
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(tabs, nullptr);
    EXPECT_GT(transport->mapTo(&restored, QPoint{}).y(), tabs->mapTo(&restored, QPoint{}).y());
    EXPECT_TRUE(action(*menu(restored, "&View"), "Classic")->isChecked());
    EXPECT_EQ(action(*menu(restored, "&View"), "Queue Focus"), nullptr);
}

TEST_F(MainWindowShell, RestoresTheNativeThemeChoice)
{
    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
        action(*menu(window, "&View"), "Dark theme")->trigger();
        EXPECT_TRUE(action(*menu(window, "&View"), "Dark theme")->isChecked());
    }

    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    wiola::gui::MainWindow restored{restored_playback, restored_library, restored_preferences};
    EXPECT_TRUE(action(*menu(restored, "&View"), "Dark theme")->isChecked());
    EXPECT_EQ(restored.palette().color(QPalette::Window), QColor{"#202124"});
}

TEST_F(MainWindowShell, AppliesAndRestoresAValidatedCustomTheme)
{
    {
        wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences window_preferences{window_playback};
        wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
        action(*menu(window, "&Tools"), "Edit theme...")->trigger();

        auto* dialog = window.findChild<QDialog*>("themeEditor");
        ASSERT_NE(dialog, nullptr);
        auto* editor = static_cast<wiola::gui::ThemeEditor*>(dialog);
        editor->set_color(wiola::gui::ThemeColorRole::window, QColor{"#f0f0f0"});
        dialog->findChild<QPushButton*>("applyThemeEdit")->click();
        EXPECT_EQ(window.palette().color(QPalette::Window), QColor{"#f0f0f0"});
    }

    wiola::app::PlaybackService restored_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession restored_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences restored_preferences{restored_playback};
    wiola::gui::MainWindow restored{restored_playback, restored_library, restored_preferences};
    EXPECT_EQ(restored.palette().color(QPalette::Window), QColor{"#f0f0f0"});
}

TEST_F(MainWindowShell, LayoutPreviewCommandsAndCancelDoNotTouchPlayerState)
{
    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    wiola::gui::MainWindow window{window_playback, window_library, window_preferences};
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

    EXPECT_EQ(window_playback.playlist().tracks().size(), 1);
    EXPECT_EQ(window_playback.playlist().tracks()[0].resource(),
        wiola::media::local_resource("/tmp/wiola-preview-queue.mp3"));
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(editor->result(), 0);

    editor->findChild<QPushButton*>("cancelLayoutEdit")->click();
    EXPECT_EQ(editor->result(), QDialog::Rejected);
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(window.centralWidget()->findChild<QPushButton*>("open"), nullptr);
}

TEST_F(MainWindowShell, CommittedCustomTagsRefreshEveryOpenPlaylist)
{
    using namespace wiola;
    QTemporaryDir media_directory;
    ASSERT_TRUE(media_directory.isValid());
    const auto path = platform::qt::QtPathAdapter::from_qt(media_directory.filePath("shared.mp3"));
    library::InMemoryRepository records;
    library::TrackRecord record;
    record.id = app::media_id_for(path);
    record.locations = {path};
    record.embedded.title = "Shared track";
    ASSERT_TRUE(records.put_track(record));
    const auto first = records.create_playlist("First");
    const auto second = records.create_playlist("Second");
    ASSERT_TRUE(records.append_track(first, record.id));
    ASSERT_TRUE(records.append_track(first, record.id));
    ASSERT_TRUE(records.append_track(second, record.id));
    QSettings settings{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    persistence::qt::LibraryStore store{platform::qt::QtPathAdapter::from_qt(
        QFileInfo{settings.fileName()}.absolutePath() + "/library.json"
    )};
    ASSERT_TRUE(store.save(library::snapshot_of(records)).succeeded());

    wiola::app::PlaybackService window_playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    window_playback.add({wiola::media::local_resource(path)});
    wiola::app::LibrarySession window_library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences window_preferences{window_playback};
    gui::MainWindow window{window_playback, window_library, window_preferences};
    auto* navigation =
        static_cast<gui::LibraryNavigation*>(window.findChild<QWidget*>("libraryNavigation"));
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    ASSERT_NE(navigation, nullptr);
    ASSERT_NE(documents, nullptr);
    ASSERT_TRUE(navigation->select_playlist(first));
    auto* first_table = documents->currentWidget()->findChild<gui::TrackTableView*>();
    ASSERT_NE(first_table, nullptr);
    ASSERT_EQ(first_table->total_count(), 2U);
    ASSERT_TRUE(navigation->select_playlist(second));
    auto* active = documents->currentWidget();
    auto* second_table = active->findChild<gui::TrackTableView*>();
    ASSERT_NE(second_table, nullptr);
    ASSERT_EQ(second_table->total_count(), 1U);
    second_table->selectRow(0);
    auto* key = window.findChild<QLineEdit*>("customTagKey");
    auto* value = window.findChild<QLineEdit*>("customTagValue");
    auto* add = window.findChild<QPushButton*>("addCustomTag");
    ASSERT_NE(key, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_TRUE(add->isEnabled());
    key->setText("Mood");
    value->setText("Calm");
    add->click();
    for (auto* table : {first_table, second_table}) {
        const auto actions = table->horizontalHeader()->actions();
        const auto mood = std::ranges::find_if(actions,
            [](QAction* item) { return item->text() == "Tag: Mood"; });
        ASSERT_NE(mood, actions.end());
        (*mood)->setChecked(true);
        for (int row = 0; row < table->model()->rowCount(); ++row)
            EXPECT_EQ(table->model()->index(row, 5).data().toString(), "Calm");
    }
    EXPECT_EQ(documents->currentWidget(), active);
    EXPECT_EQ(window_playback.playlist().tracks().size(), 1U);
    ASSERT_TRUE(window_library.flush().succeeded());
    library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    ASSERT_EQ(restored.tracks.size(), 1U);
    ASSERT_EQ(restored.tracks.front().tags.size(), 1U);
    EXPECT_EQ(restored.tracks.front().tags.front().value, "Calm");
}

TEST_F(MainWindowShell, SelectedPlaylistTrackOwnsBothEditorsWhileAnotherTrackPlays)
{
    using namespace wiola;
    QTemporaryDir directory;
    app::LibrarySession library{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")),
        wiola::codec::ffmpeg::make_provider()};
    library::TrackRecord selected, playing;
    selected.id = {"selected"};
    selected.locations = {"selected.wav"};
    selected.embedded.title = "Selected";
    playing.id = {"playing"};
    playing.locations = {"playing.wav"};
    playing.embedded.title = "Playing";
    library::RepositoryTransaction seed{library.repository(), library.persistence()};
    ASSERT_TRUE(library.repository().put_track(selected));
    ASSERT_TRUE(library.repository().put_track(playing));
    const auto playlist = library.repository().create_playlist("Selected playlist");
    ASSERT_TRUE(library.repository().append_track(playlist, selected.id));
    ASSERT_TRUE(seed.commit());
    app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    playback.add(wiola::media::local_resources(playing.locations));

    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};
    auto* navigation =
        static_cast<gui::LibraryNavigation*>(window.findChild<QWidget*>("libraryNavigation"));
    ASSERT_TRUE(navigation->select_playlist(playlist));
    auto* documents = window.findChild<QTabWidget*>("documentTabs");
    auto* table = documents->currentWidget()->findChild<gui::TrackTableView*>();
    ASSERT_NE(table, nullptr);
    table->selectRow(0);
    EXPECT_EQ(window.findChild<QLabel*>("inspectorTitle")->text(), "Selected");
    auto* title = window.findChild<QLineEdit*>("standardTitle");
    EXPECT_EQ(title->text(), "Selected");
    window.findChild<QLineEdit*>("customTagKey")->setText("Mood");
    window.findChild<QLineEdit*>("customTagValue")->setText("Calm");
    window.findChild<QPushButton*>("addCustomTag")->click();
    ASSERT_EQ(library.repository().track(selected.id)->tags.size(), 1);
    EXPECT_TRUE(library.repository().track(playing.id)->tags.empty());
    title->setText("Edited selection");
    window.findChild<QPushButton*>("applyStandardMetadata")->click();
    EXPECT_EQ(library.repository().track(selected.id)->overrides.title, "Edited selection");
    EXPECT_FALSE(library.repository().track(playing.id)->overrides.title);
    ASSERT_NE(playback.playlist().current(), nullptr);
    EXPECT_EQ(playback.playlist().current()->resource(),
        wiola::media::local_resource(playing.locations.front()));
}

TEST_F(MainWindowShell, CustomLayoutReopensFromAcceptedValuesAndRestartsWithoutLosingEdits)
{
    QByteArray accepted;
    {
        wiola::app::PlaybackService playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences preferences{playback};
        wiola::gui::MainWindow window{playback, library, preferences};
        auto edited = wiola::gui::classic_layout_document();
        edited.root.id = "acceptedCustomRoot";
        ASSERT_TRUE(window.switch_layout(edited));
        accepted = window.layout_json();
        action(*menu(window, "&Tools"), "Edit button layout...")->trigger();
        auto* dialog = window.findChild<QDialog*>("buttonLayoutEditor");
        ASSERT_NE(dialog, nullptr);
        const auto* editor = static_cast<wiola::gui::LayoutEditorDialog*>(dialog);
        EXPECT_EQ(editor->document().root.id, "acceptedCustomRoot");
        dialog->reject();
        EXPECT_FALSE(window.import_layout(R"({"version":99,"root":{}})"));
        EXPECT_EQ(window.layout_json(), accepted);
    }
    {
        wiola::app::PlaybackService playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};

        wiola::app::PlaybackPreferences preferences{playback};
        wiola::gui::MainWindow window{playback, library, preferences};
        EXPECT_EQ(window.layout_json(), accepted);
        EXPECT_EQ(window.centralWidget()->objectName(), "acceptedCustomRoot");
    }
}

TEST_F(MainWindowShell,
    TypedSessionRestoresBrowserStateAndDestinationIndependentlyWithDuplicateQueueEntries)
{
    ASSERT_NO_FATAL_FAILURE(wiola::testing::restore_session_journey());
}

TEST_F(MainWindowShell,
    ReopeningThemeEditorsUsesCurrentAcceptedColorsAndCustomChoiceSurvivesSwitching)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};

    wiola::app::PlaybackPreferences preferences{playback};
    wiola::gui::MainWindow window{playback, library, preferences};
    for (const auto& color : {QColor{"#f0f0f0"}, QColor{"#eeeeee"}}) {
        action(*menu(window, "&Tools"), "Edit theme...")->trigger();
        auto* dialog = window.findChild<QDialog*>("themeEditor");
        ASSERT_NE(dialog, nullptr);
        auto* editor = static_cast<wiola::gui::ThemeEditor*>(dialog);
        if (color == QColor{"#eeeeee"})
            EXPECT_EQ(editor->colors().window, QColor{"#f0f0f0"});
        editor->set_color(wiola::gui::ThemeColorRole::window, color);
        dialog->findChild<QPushButton*>("applyThemeEdit")->click();
        EXPECT_EQ(window.palette().color(QPalette::Window), color);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    action(*menu(window, "&View"), "Dark theme")->trigger();
    action(*menu(window, "&View"), "Custom theme")->trigger();
    EXPECT_EQ(window.palette().color(QPalette::Window), QColor{"#eeeeee"});
    EXPECT_TRUE(action(*menu(window, "&View"), "Custom theme")->isChecked());
}

TEST_F(MainWindowShell, DefaultBrowserHasMovablePanelsAndReadableCompactControlsInBothThemes)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};
    wiola::app::PlaybackPreferences preferences{playback};
    wiola::gui::MainWindow window{playback, library, preferences};
    QTemporaryDir tracks;
    ASSERT_TRUE(tracks.isValid());
    const auto directory = wiola::platform::qt::QtPathAdapter::from_qt(tracks.path());
    window.open({wiola::testing::write_wav("Evening light.wav", wiola::testing::source_rate,
                     directory),
        wiola::testing::write_wav("Midnight journey.wav", wiola::testing::source_rate, directory),
        wiola::testing::write_wav("Тихий вечер.wav", wiola::testing::source_rate, directory)});
    ASSERT_TRUE(wiola::testing::await_gui([&] { return !playback.reading(); }));
    playback.pump();
    window.refresh();
    window.update_after_pump(wiola::app::present_playback(playback));
    window.resize(1100, 720);
    window.show();
    auto* sidebar = window.findChild<QDockWidget*>("libraryDock");
    auto* details = window.findChild<QDockWidget*>("inspectorDock");
    ASSERT_NE(sidebar, nullptr);
    ASSERT_NE(details, nullptr);
    EXPECT_TRUE(sidebar->features().testFlag(QDockWidget::DockWidgetMovable));
    EXPECT_TRUE(sidebar->features().testFlag(QDockWidget::DockWidgetFloatable));
    EXPECT_TRUE(details->isHidden());
    auto* browser = window.findChild<wiola::gui::TrackBrowser*>();
    ASSERT_NE(browser, nullptr);
    auto* search = browser->findChild<QLineEdit*>("searchTitle");
    auto* more = browser->findChild<QToolButton*>("advancedTrackSearch");
    auto* filters = browser->findChild<QToolButton*>("tagFilterDisclosure");
    ASSERT_NE(search, nullptr);
    ASSERT_NE(more, nullptr);
    ASSERT_NE(filters, nullptr);
    EXPECT_FALSE(more->isChecked());
    EXPECT_FALSE(filters->isChecked());
    for (const auto theme : {wiola::gui::Theme::light, wiola::gui::Theme::dark}) {
        action(*menu(window, "&View"),
            theme == wiola::gui::Theme::dark ? "Dark theme" : "Light theme")
            ->trigger();
        QApplication::processEvents();
        EXPECT_GE(search->height(), search->sizeHint().height());
        EXPECT_GT(browser->table().height(), window.height() / 3);
        auto* volume = window.findChild<QSlider*>("volumeSlider");
        ASSERT_NE(volume, nullptr);
        EXPECT_LT(volume->width(), window.width() / 4);
        EXPECT_EQ(search->palette().color(QPalette::Base), wiola::gui::theme_colors(theme).surface);
        const auto directory = qEnvironmentVariable("WIOLA_GUI_PREVIEW_DIR");
        if (!directory.isEmpty()) {
            QDir{}.mkpath(directory);
            EXPECT_TRUE(window.grab().save(QDir{directory}.filePath(
                theme == wiola::gui::Theme::dark ? "desktop-dark.png" : "desktop-light.png"
            )));
        }
    }
    more->click();
    QApplication::processEvents();
    auto* artist = browser->findChild<QLineEdit*>("searchArtist");
    EXPECT_TRUE(artist->isVisible());
    EXPECT_GE(artist->height(), artist->sizeHint().height());
    sidebar->setFloating(true);
    EXPECT_TRUE(sidebar->isFloating());
    auto* view = menu(window, "&View");
    ASSERT_NE(view, nullptr);
    action(*view, "Reset panel positions")->trigger();
    EXPECT_FALSE(sidebar->isFloating());
    EXPECT_TRUE(sidebar->isVisible());
}

TEST_F(MainWindowShell, EditingIsRequestedFromASelectedTrackAndPanelPositionsPersist)
{
    {
        wiola::app::PlaybackService playback{wiola::testing::passive_output,
            wiola::codec::ffmpeg::make_provider()};
        wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};
        wiola::app::PlaybackPreferences preferences{playback};
        wiola::gui::MainWindow window{playback, library, preferences};
        window.show();
        auto* details = window.findChild<QDockWidget*>("inspectorDock");
        ASSERT_TRUE(details->isHidden());
        window.open({"selected-track.wav"});
        auto* queue = window.findChild<wiola::gui::NowPlayingDocument*>();
        ASSERT_NE(queue, nullptr);
        auto& table = queue->browser().table();
        ASSERT_GT(table.model()->rowCount(), 0);
        table.selectRow(0);
        auto* edit = queue->findChild<QPushButton*>("editSelectedTrack");
        ASSERT_NE(edit, nullptr);
        ASSERT_TRUE(edit->isEnabled());
        EXPECT_TRUE(details->isHidden());
        edit->click();
        EXPECT_TRUE(details->isVisible());
        EXPECT_TRUE(window.findChild<QToolButton*>("metadataDisclosure")->isChecked());
        auto* sidebar = window.findChild<QDockWidget*>("libraryDock");
        window.addDockWidget(Qt::RightDockWidgetArea, sidebar);
        sidebar->hide();
    }
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};
    wiola::app::PlaybackPreferences preferences{playback};
    wiola::gui::MainWindow restored{playback, library, preferences};
    auto* sidebar = restored.findChild<QDockWidget*>("libraryDock");
    EXPECT_EQ(restored.dockWidgetArea(sidebar), Qt::RightDockWidgetArea);
    EXPECT_TRUE(sidebar->isHidden());
    EXPECT_FALSE(restored.findChild<QDockWidget*>("inspectorDock")->isHidden());
}

TEST_F(MainWindowShell, DockedTrackDetailsLeaveFullWidthPlaybackAndItsCurrentTrackUnchanged)
{
    using namespace wiola;
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{codec::ffmpeg::make_provider()};
    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};
    QTemporaryDir tracks;
    ASSERT_TRUE(tracks.isValid());
    const auto directory = platform::qt::QtPathAdapter::from_qt(tracks.path());
    const auto playing =
        wiola::testing::write_wav("Playing.wav", wiola::testing::source_rate, directory);
    const auto selected =
        wiola::testing::write_wav("Selected.wav", wiola::testing::source_rate, directory);
    const auto expected_title = platform::qt::QtPathAdapter::to_qt(playing.filename());
    window.open({playing, selected});
    ASSERT_TRUE(wiola::testing::await_gui([&] { return !playback.reading(); }));
    playback.pump();
    window.refresh();
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    auto* footer = window.findChild<QStatusBar*>("playbackFooter");
    auto* transport = window.findChild<QWidget*>("playbackGroup");
    auto* title = window.findChild<QLabel*>("trackLabel");
    auto* seek = window.findChild<QWidget*>("positionBar");
    auto* volume = window.findChild<QSlider*>("volumeSlider");
    auto* details = window.findChild<QDockWidget*>("inspectorDock");
    auto* sidebar = window.findChild<QDockWidget*>("libraryDock");
    auto* queue = window.findChild<gui::NowPlayingDocument*>();
    ASSERT_NE(footer, nullptr);
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(title, nullptr);
    ASSERT_NE(seek, nullptr);
    ASSERT_NE(volume, nullptr);
    ASSERT_NE(details, nullptr);
    ASSERT_NE(sidebar, nullptr);
    ASSERT_NE(queue, nullptr);
    ASSERT_TRUE(footer->isVisible());
    ASSERT_TRUE(footer->isAncestorOf(transport));
    ASSERT_FALSE(window.centralWidget()->isAncestorOf(transport));
    ASSERT_EQ(title->text(), expected_title);
    ASSERT_EQ(queue->browser().table().model()->rowCount(), 2);
    const auto geometry = [&window](const QWidget* widget) {
        return QRect{widget->mapTo(&window, QPoint{}), widget->size()};
    };

    for (const auto theme : {"Light theme", "Dark theme"}) {
        action(*menu(window, "&View"), theme)->trigger();
        QApplication::processEvents();
        const auto footer_geometry = geometry(footer);
        const auto transport_geometry = geometry(transport);
        const auto seek_geometry = geometry(seek);
        const auto volume_geometry = geometry(volume);
        EXPECT_EQ(footer_geometry.left(), window.contentsRect().left());
        EXPECT_EQ(footer_geometry.right(), window.contentsRect().right());
        for (const auto* name : {"repeatQueue", "shuffleQueue", "equalizer"}) {
            auto* control = window.findChild<QPushButton*>(name);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(transport->isAncestorOf(control));
            EXPECT_TRUE(control->isVisible());
        }
        for (const auto* name : {"openTracks", "addTracks", "removeTracks", "clearQueue"}) {
            auto* control = window.findChild<QPushButton*>(name);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(window.centralWidget()->isAncestorOf(control));
            EXPECT_FALSE(footer->isAncestorOf(control));
        }
        action(*menu(window, "&Tools"), "Edit button layout...")->trigger();
        auto* editor = window.findChild<QDialog*>("buttonLayoutEditor");
        ASSERT_NE(editor, nullptr);
        editor->findChild<QPushButton*>("applyLayoutEdit")->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        ASSERT_TRUE(footer->isVisible());
        ASSERT_TRUE(footer->isAncestorOf(transport));
        EXPECT_FALSE(preferences.ui_session().state().splitters.contains("layoutEditorColumns"));
        EXPECT_FALSE(preferences.ui_session().state().splitters.contains("layoutEditorRows"));
        const auto unchanged = [&] {
            QApplication::processEvents();
            EXPECT_EQ(geometry(footer), footer_geometry);
            EXPECT_EQ(geometry(transport), transport_geometry);
            EXPECT_EQ(geometry(seek), seek_geometry);
            EXPECT_EQ(geometry(volume), volume_geometry);
            EXPECT_EQ(title->text(), expected_title);
            ASSERT_NE(playback.playlist().current(), nullptr);
            EXPECT_EQ(playback.playlist().current()->resource(), media::local_resource(playing));
        };
        queue->browser().table().selectRow(1);
        auto* edit = queue->findChild<QPushButton*>("editSelectedTrack");
        ASSERT_NE(edit, nullptr);
        ASSERT_TRUE(edit->isEnabled());
        edit->click();
        ASSERT_TRUE(details->isVisible());
        unchanged();
        window.resizeDocks({details}, {details->width() + 80}, Qt::Horizontal);
        unchanged();
        sidebar->hide();
        unchanged();
        details->setFloating(true);
        unchanged();
        details->setFloating(false);
        window.addDockWidget(Qt::LeftDockWidgetArea, details);
        unchanged();
        const auto previews = qEnvironmentVariable("WIOLA_GUI_PREVIEW_DIR");
        if (!previews.isEmpty()) {
            QDir{}.mkpath(previews);
            EXPECT_TRUE(window.grab().save(QDir{previews}.filePath(
                QString{"footer-with-details-%1.png"}.arg(QString::fromLatin1(theme).first(4))
            )));
        }
        details->hide();
        sidebar->show();
        window.addDockWidget(Qt::RightDockWidgetArea, details);
        unchanged();
    }
}

TEST_F(MainWindowShell, PlaybackFooterSurvivesCustomPlacementOmissionAndReturnToPresets)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        wiola::codec::ffmpeg::make_provider()};
    wiola::app::LibrarySession library{wiola::codec::ffmpeg::make_provider()};
    wiola::app::PlaybackPreferences preferences{playback};
    wiola::gui::MainWindow window{playback, library, preferences};
    window.show();
    auto* footer = window.findChild<QStatusBar*>("playbackFooter");
    const QPointer<QWidget> transport = window.findChild<QWidget*>("playbackGroup");
    const QPointer<QWidget> seek = window.findChild<QWidget*>("positionBar");
    ASSERT_NE(footer, nullptr);
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(seek, nullptr);
    EXPECT_FALSE(window.import_layout("invalid"));
    EXPECT_TRUE(footer->isAncestorOf(transport));
    for (const auto* preset : {"Library Focus", "Classic"}) {
        auto custom = wiola::gui::classic_layout_document();
        custom.root.id = "customPlaybackRoot";
        ASSERT_TRUE(window.switch_layout(custom));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        EXPECT_TRUE(footer->isVisible());
        ASSERT_NE(transport, nullptr);
        EXPECT_TRUE(footer->isAncestorOf(transport));
        EXPECT_TRUE(transport->isVisible());
        std::erase_if(custom.root.children,
            [](const auto& node) { return node.id == "playbackGroup"; });
        custom.node_count = wiola::gui::layout_node_count(custom.root);
        ASSERT_TRUE(window.switch_layout(custom));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        ASSERT_NE(transport, nullptr);
        EXPECT_TRUE(transport->isHidden());
        action(*menu(window, "&View"), preset)->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        ASSERT_NE(transport, nullptr);
        ASSERT_NE(seek, nullptr);
        EXPECT_TRUE(footer->isVisible());
        EXPECT_TRUE(footer->isAncestorOf(transport));
        EXPECT_TRUE(transport->isVisible());
        EXPECT_TRUE(seek->isVisible());
        for (const auto* name : {"repeatQueue", "shuffleQueue", "equalizer"}) {
            auto* control = window.findChild<QPushButton*>(name);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(transport->isAncestorOf(control));
            EXPECT_TRUE(control->isVisible());
        }
    }
}

TEST_F(MainWindowShell, PlaybackOnlyLayoutRemainsAFullWidthFooterAndCanRestoreDocuments)
{
    using namespace wiola;
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{codec::ffmpeg::make_provider()};
    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};
    const auto original = gui::classic_layout_document();
    auto* tabs = window.findChild<QWidget*>("documentTabs");
    auto* footer = window.findChild<QStatusBar*>("playbackFooter");
    QPointer<QWidget> transport = window.findChild<QWidget*>("playbackGroup");
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(footer, nullptr);
    ASSERT_NE(transport, nullptr);
    gui::LayoutDocument only_player;
    only_player.root = *gui::locate_layout_node(original.root, "playbackGroup").node;
    only_player.node_count = gui::layout_node_count(only_player.root);
    window.show();
    ASSERT_TRUE(window.switch_layout(only_player));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_NE(transport, nullptr);
    EXPECT_NE(window.centralWidget(), transport);
    EXPECT_TRUE(footer->isAncestorOf(transport));
    EXPECT_TRUE(footer->isVisible());
    action(*menu(window, "&View"), "Classic")->trigger();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(window.findChild<QWidget*>("documentTabs"), tabs);
    EXPECT_TRUE(tabs->isVisible());
    EXPECT_TRUE(footer->isAncestorOf(transport));
}

TEST_F(MainWindowShell, PlaybackControlsAndTimelineRemainCenteredWithIndependentSideRegions)
{
    using namespace wiola;
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{codec::ffmpeg::make_provider()};
    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};
    window.show();
    auto* title = window.findChild<QLabel*>("trackLabel");
    auto* seek = window.findChild<QWidget*>("positionBar");
    auto* volume = window.findChild<QSlider*>("volumeSlider");
    auto* footer = window.findChild<QStatusBar*>("playbackFooter");
    auto* details = window.findChild<QDockWidget*>("inspectorDock");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(seek, nullptr);
    ASSERT_NE(volume, nullptr);
    ASSERT_NE(footer, nullptr);
    ASSERT_NE(details, nullptr);
    const auto bounds = [&window](const QWidget* widget) {
        return QRect{widget->mapTo(&window, QPoint{}), widget->size()};
    };
    for (const auto theme : {"Light theme", "Dark theme"}) {
        action(*menu(window, "&View"), theme)->trigger();
        for (const int width : {820, 1100, 1600}) {
            window.resize(width, 720);
            for (const bool show_details : {false, true}) {
                details->setVisible(show_details);
                QApplication::processEvents();
                QRect controls;
                for (const auto* name :
                    {"previousTrack", "playPause", "nextTrack", "stopPlayback"}) {
                    auto* button = window.findChild<QPushButton*>(name);
                    ASSERT_NE(button, nullptr);
                    controls = controls.united(bounds(button));
                }
                EXPECT_NEAR(controls.center().x(), bounds(footer).center().x(), 2);
                EXPECT_NEAR(bounds(seek).center().x(), bounds(footer).center().x(), 2);
                EXPECT_LT(bounds(title).right(), bounds(seek).left());
                EXPECT_GT(bounds(volume).left(), bounds(seek).right());
                EXPECT_LT(controls.bottom(), bounds(seek).top());
                for (const auto* name : {"repeatQueue", "shuffleQueue", "equalizer"}) {
                    auto* option = window.findChild<QPushButton*>(name);
                    ASSERT_NE(option, nullptr);
                    EXPECT_GT(bounds(option).left(), bounds(seek).right());
                }
            }
        }
    }
}

TEST_F(MainWindowShell, FileDialogDescriptionsAreReadableAndInvalidChoicesNeverOpenAPicker)
{
    using wiola::gui::PlayerFileChoice;
    for (const auto purpose : {PlayerFileChoice::import_playlist, PlayerFileChoice::export_playlist,
             PlayerFileChoice::import_layout, PlayerFileChoice::export_layout,
             PlayerFileChoice::import_theme, PlayerFileChoice::export_theme}) {
        const auto specification = wiola::gui::file_dialog_specification(purpose);
        EXPECT_FALSE(specification.title.isEmpty());
        EXPECT_FALSE(specification.filter.isEmpty());
        EXPECT_EQ(specification.save,
            purpose == PlayerFileChoice::export_playlist ||
                purpose == PlayerFileChoice::export_layout ||
                purpose == PlayerFileChoice::export_theme);
    }
    EXPECT_EQ(wiola::gui::file_dialog_specification(PlayerFileChoice::import_playlist).title,
        "Import playlist");
    EXPECT_EQ(wiola::gui::file_dialog_specification(PlayerFileChoice::export_playlist).title,
        "Save playlist as");
    auto parent = std::make_unique<QWidget>();
    wiola::gui::NativePlayerDialogs dialogs{*parent};
    EXPECT_FALSE(dialogs.file(static_cast<PlayerFileChoice>(-1)));
    parent.reset();
    EXPECT_FALSE(dialogs.file(PlayerFileChoice::import_playlist));
}

} // namespace

int main(int argc, char** argv)
{
    return wiola::testing::run_gui_tests(argc, argv);
}
