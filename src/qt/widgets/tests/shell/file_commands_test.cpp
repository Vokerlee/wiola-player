/**
 * @file
 * @brief Native file dialogs and shell import/export behavior contracts.
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
#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <qt/widgets/shell/player_dialogs.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>
#include <qt/widgets/tests/support/modal_answer.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/qt/paths.hpp>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

namespace {

using namespace wiola;

class FileCommands : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    }

    QAction* command(gui::MainWindow& window, const QString& text)
    {
        for (auto* menu : window.menuBar()->findChildren<QMenu*>())
            for (auto* action : menu->actions())
                if (action->text() == text)
                    return action;
        ADD_FAILURE() << "Missing menu command: " << text.toStdString();
        return nullptr;
    }

    void choose(QFileDialog& dialog, const QString& path)
    {
        // selectFile preserves a focused filename edit; transfer focus before supplying a choice.
        if (auto* focused = QApplication::focusWidget())
            focused->clearFocus();

        dialog.selectFile(path);
        static_cast<QDialog&>(dialog).accept();
    }

    QTemporaryDir directory;
};

class FileDialogs : public FileCommands {
protected:
    void SetUp() override
    {
        FileCommands::SetUp();
#ifdef WIOLA_TEST_TSAN
        GTEST_SKIP() << "QFileSystemModel needs instrumented Qt Gui; file pickers are qualified in "
                        "Debug and ASan";
#endif
    }
};

TEST_F(FileDialogs, NativePickersReturnChosenPathsAndCancellationIsEmpty)
{
    QWidget parent;
    gui::NativePlayerDialogs dialogs{parent};
    const auto filename = directory.filePath("song.wav");
    QFile file{filename};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    const auto path = platform::qt::QtPathAdapter::from_qt(filename);

    {
        wiola::testing::ModalAnswer<QFileDialog> answer{[&](auto& dialog) {
            EXPECT_TRUE(dialog.nameFilters().front().contains("*.wav"));
            choose(dialog, filename);
        }};
        const std::vector<std::string> extensions{".wav", ".flac"};
        EXPECT_EQ(dialogs.tracks(extensions), std::vector{path});
    }
    {
        wiola::testing::ModalAnswer<QFileDialog> answer{[&](auto& dialog) {
            dialog.setDirectory(directory.path());
            static_cast<QDialog&>(dialog).accept();
        }};
        EXPECT_EQ(dialogs.folder(), platform::qt::QtPathAdapter::from_qt(directory.path()));
    }
    for (const auto purpose :
        {gui::PlayerFileChoice::import_playlist, gui::PlayerFileChoice::export_playlist,
            gui::PlayerFileChoice::import_layout, gui::PlayerFileChoice::export_layout,
            gui::PlayerFileChoice::import_theme, gui::PlayerFileChoice::export_theme}) {
        wiola::testing::ModalAnswer<QFileDialog> answer{[](auto& dialog) { dialog.reject(); }};
        EXPECT_FALSE(dialogs.file(purpose));
    }
}

TEST_F(FileCommands, DestinationMenuSelectsExactPlaylistAndHandlesEmptyOrClosedOwner)
{
    auto parent = std::make_unique<QWidget>();
    gui::NativePlayerDialogs dialogs{*parent};
    const library::PlaylistId first{"one"}, second{"two"};
    const std::vector<gui::PlaylistChoice> choices{
        {first,  "Same"},
        {second, "Same"}
    };
    {
        wiola::testing::ModalAnswer<QMenu> answer{[](auto& menu) {
            ASSERT_EQ(menu.actions().size(), 2);
            menu.setActiveAction(menu.actions().back());
            QTest::keyClick(&menu, Qt::Key_Return);
        }};
        EXPECT_EQ(dialogs.destination(choices), second);
    }
    {
        wiola::testing::ModalAnswer<QMenu> answer{[](auto& menu) {
            ASSERT_EQ(menu.actions().size(), 1);
            EXPECT_FALSE(menu.actions().front()->isEnabled());
            menu.close();
        }};
        EXPECT_FALSE(dialogs.destination({}));
    }
    parent.reset();
    EXPECT_TRUE(dialogs.tracks({}).empty());
    EXPECT_FALSE(dialogs.folder());
    EXPECT_FALSE(dialogs.file(gui::PlayerFileChoice::export_theme));
    EXPECT_FALSE(dialogs.destination(choices));
}

TEST_F(FileDialogs, LayoutAndThemeExportsCanBeImportedAndCancellationPreservesThem)
{
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{codec::ffmpeg::make_provider()};
    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};

    for (const auto& kind : {QString{"layout"}, QString{"theme"}}) {
        const auto filename = directory.filePath(kind + ".json");
        const auto exported = command(window, "Export " + kind + "...");
        const auto imported = command(window, "Import " + kind + "...");
        ASSERT_NE(exported, nullptr);
        ASSERT_NE(imported, nullptr);
        {
            wiola::testing::ModalAnswer<QFileDialog> answer{
                [&](auto& dialog) { choose(dialog, filename); }};
            exported->trigger();
        }
        QFile file{filename};
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        EXPECT_FALSE(bytes.isEmpty());
        file.close();
        const auto layout = window.layout_json();
        {
            wiola::testing::ModalAnswer<QFileDialog> answer{
                [&](auto& dialog) { choose(dialog, filename); }};
            imported->trigger();
        }
        EXPECT_EQ(window.layout_json(), layout);
        for (auto* action : {imported, exported}) {
            wiola::testing::ModalAnswer<QFileDialog> answer{[](auto& dialog) { dialog.reject(); }};
            action->trigger();
            EXPECT_EQ(window.layout_json(), layout);
        }
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("invalid json");
        file.close();
        {
            wiola::testing::ModalAnswer<QFileDialog> answer{
                [&](auto& dialog) { choose(dialog, filename); }};
            imported->trigger();
        }
        EXPECT_EQ(window.layout_json(), layout);
    }
}

TEST_F(FileDialogs, FolderAndPlaylistImportsOpenTheirDocumentsAndExportUsesTheVisiblePlaylist)
{
    app::PlaybackService playback{wiola::testing::passive_output, codec::ffmpeg::make_provider()};
    app::LibrarySession library{codec::ffmpeg::make_provider()};
    app::PlaybackPreferences preferences{playback};
    gui::MainWindow window{playback, library, preferences};
    auto* host = window.findChild<gui::DocumentHost*>();
    ASSERT_NE(host, nullptr);
    auto* folder = command(window, "Open folder...");
    ASSERT_NE(folder, nullptr);
    {
        wiola::testing::ModalAnswer<QFileDialog> answer{[&](auto& dialog) {
            dialog.setDirectory(directory.path());
            static_cast<QDialog&>(dialog).accept();
        }};
        folder->trigger();
    }
    ASSERT_EQ(library.repository().workspaces().size(), 1);
    EXPECT_EQ(host->registry().active(),
        gui::DocumentId::workspace(library.repository().workspaces()[0].id));
    const auto filename = directory.filePath("Imported.m3u8");
    QFile input{filename};
    ASSERT_TRUE(input.open(QIODevice::WriteOnly));
    input.write("#EXTM3U\ntrack.wav\n");
    input.close();
    auto* import = command(window, "Import playlist...");
    ASSERT_NE(import, nullptr);
    {
        wiola::testing::ModalAnswer<QFileDialog> answer{
            [&](auto& dialog) { choose(dialog, filename); }};
        import->trigger();
    }
    ASSERT_EQ(library.repository().playlists().size(), 1);
    EXPECT_EQ(host->registry().active(),
        gui::DocumentId::playlist(library.repository().playlists()[0].id));
    const auto output = directory.filePath("exported.m3u8");
    auto* save = command(window, "Save playlist as...");
    ASSERT_NE(save, nullptr);
    {
        wiola::testing::ModalAnswer<QFileDialog> answer{
            [&](auto& dialog) { choose(dialog, output); }};
        save->trigger();
    }
    QFile exported{output};
    ASSERT_TRUE(exported.open(QIODevice::ReadOnly));
    EXPECT_TRUE(exported.readAll().contains("track.wav"));
    auto* retry = command(window, "Retry saving library");
    ASSERT_NE(retry, nullptr);
    retry->trigger();
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
