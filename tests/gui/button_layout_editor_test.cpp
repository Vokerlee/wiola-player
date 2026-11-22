/**
 * @file
 * @brief Button layout editor interaction tests.
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

#include <gui/button_layout_editor.hpp>
#include <gui/classic_layout.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QMimeData>

#include <gtest/gtest.h>

namespace {

const wiola::gui::LayoutNode* find(const wiola::gui::LayoutNode& root, const QString& id)
{
    if (root.id == id)
        return &root;

    for (const wiola::gui::LayoutNode& child : root.children) {
        if (const auto* found = find(child, id))
            return found;
    }

    return nullptr;
}

QTreeWidgetItem* item(QTreeWidget& tree, const QString& id)
{
    const auto items = tree.findItems(id, Qt::MatchExactly | Qt::MatchRecursive);
    return items.empty() ? nullptr : items.front();
}

TEST(ButtonLayoutEditor, AddsConfiguresAndAppliesPaletteActions)
{
    wiola::gui::ButtonLayoutEditor editor{wiola::gui::classic_layout_document()};
    auto* palette = editor.findChild<QListWidget*>("layoutComponentPalette");
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    ASSERT_NE(palette, nullptr);
    ASSERT_NE(tree, nullptr);

    QTreeWidgetItem* transport = item(*tree, "transportControls");
    ASSERT_NE(transport, nullptr);
    tree->setCurrentItem(transport);
    palette->setCurrentRow(0);
    editor.findChild<QPushButton*>("addLayoutAction")->click();

    const auto* inserted = find(editor.document().root, "open");
    ASSERT_NE(inserted, nullptr);
    ASSERT_EQ(inserted->action, wiola::gui::PlayerAction::open);

    tree->setCurrentItem(item(*tree, "open"));
    auto* action = editor.findChild<QComboBox*>("layoutComponentAction");
    ASSERT_NE(action, nullptr);
    const int next = action->findData("next");
    action->setCurrentIndex(next);
    emit action->activated(next);

    inserted = find(editor.document().root, "open");
    ASSERT_NE(inserted, nullptr);
    EXPECT_EQ(inserted->action, wiola::gui::PlayerAction::next);

    wiola::gui::LayoutDocument applied;
    int applications = 0;
    editor.on_apply([&](const wiola::gui::LayoutDocument& document) {
        applied = document;
        ++applications;
        return true;
    });
    editor.findChild<QPushButton*>("applyLayoutEdit")->click();

    EXPECT_EQ(applications, 1);
    EXPECT_NE(find(applied.root, "open"), nullptr);
    EXPECT_EQ(editor.result(), QDialog::Accepted);
}

TEST(ButtonLayoutEditor, AcceptsPaletteDropsAndRestoresHistory)
{
    wiola::gui::ButtonLayoutEditor editor{wiola::gui::classic_layout_document()};
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* transport = item(*tree, "transportControls");
    ASSERT_NE(transport, nullptr);

    QMimeData data;
    data.setData("application/x-wiola-layout-component", "action:next");
    const QPoint position = tree->visualItemRect(transport).center();
    QDragEnterEvent enter{position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    QDropEvent drop{QPointF{position}, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(tree->viewport(), &drop);

    EXPECT_TRUE(drop.isAccepted());
    EXPECT_NE(find(editor.document().root, "next"), nullptr);

    editor.findChild<QPushButton*>("undoLayoutEdit")->click();
    EXPECT_EQ(find(editor.document().root, "next"), nullptr);
    EXPECT_TRUE(editor.findChild<QPushButton*>("redoLayoutEdit")->isEnabled());

    editor.findChild<QPushButton*>("redoLayoutEdit")->click();
    EXPECT_NE(find(editor.document().root, "next"), nullptr);
}

TEST(ButtonLayoutEditor, RepeatedConfigurationRetainsTheSelectedComponentIdentity)
{
    wiola::gui::ButtonLayoutEditor editor{wiola::gui::classic_layout_document()};
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    auto* title = editor.findChild<QLineEdit*>("layoutComponentTitle");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(title, nullptr);
    tree->setCurrentItem(item(*tree, "transportControls"));
    for (int revision = 0; revision < 32; ++revision) {
        const QString text = "Controls " + QString::number(revision);
        title->setText(text);
        emit title->editingFinished();
        const auto* node = find(editor.document().root, "transportControls");
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->title, text);
        ASSERT_NE(tree->currentItem(), nullptr);
        EXPECT_EQ(tree->currentItem()->text(0), "transportControls");
    }
}

TEST(ButtonLayoutEditor, CreatesAndConfiguresContainersWithUndoAndIsolatedPreview)
{
    wiola::gui::ButtonLayoutEditor editor{wiola::gui::classic_layout_document()};
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    auto* kind = editor.findChild<QComboBox*>("layoutContainerType");
    auto* add = editor.findChild<QPushButton*>("addLayoutContainer");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(kind, nullptr);
    ASSERT_NE(add, nullptr);
    for (const auto type : {wiola::gui::LayoutComponentKind::grid,
             wiola::gui::LayoutComponentKind::splitter, wiola::gui::LayoutComponentKind::tabs}) {
        tree->setCurrentItem(tree->topLevelItem(0));
        kind->setCurrentIndex(kind->findData(static_cast<int>(type)));
        add->click();
    }
    ASSERT_NE(find(editor.document().root, "grid"), nullptr);
    ASSERT_NE(find(editor.document().root, "splitter"), nullptr);
    ASSERT_NE(find(editor.document().root, "tabs"), nullptr);
    tree->setCurrentItem(item(*tree, "grid"));
    auto* columns = editor.findChild<QSpinBox*>("layoutGridColumns");
    ASSERT_NE(columns, nullptr);
    EXPECT_TRUE(columns->isEnabled());
    columns->setValue(3);
    emit columns->editingFinished();
    EXPECT_EQ(find(editor.document().root, "grid")->columns, 3);
    tree->setCurrentItem(item(*tree, "splitter"));
    auto* orientation = editor.findChild<QComboBox*>("layoutComponentOrientation");
    ASSERT_NE(orientation, nullptr);
    const int vertical = orientation->findData(static_cast<int>(Qt::Vertical));
    orientation->setCurrentIndex(vertical);
    emit orientation->activated(vertical);
    EXPECT_EQ(find(editor.document().root, "splitter")->orientation, Qt::Vertical);
    editor.findChild<QPushButton*>("undoLayoutEdit")->click();
    EXPECT_NE(find(editor.document().root, "splitter")->orientation, Qt::Vertical);
    editor.findChild<QPushButton*>("redoLayoutEdit")->click();
    EXPECT_EQ(find(editor.document().root, "splitter")->orientation, Qt::Vertical);
    auto* status = editor.findChild<QLabel*>("layoutEditorStatus");
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().isEmpty());
}

#ifndef WIOLA_TEST_TSAN
TEST(ButtonLayoutEditor, RemovesSelectedComponentsWithTheKeyboard)
{
    wiola::gui::ButtonLayoutEditor editor{wiola::gui::classic_layout_document()};
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    ASSERT_NE(tree, nullptr);
    editor.show();
    editor.activateWindow();
    tree->setFocus();
    tree->setCurrentItem(item(*tree, "openTracks"));
    QCoreApplication::processEvents();

    QKeyEvent remove{QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier};
    QCoreApplication::sendEvent(tree, &remove);
    QCoreApplication::processEvents();

    EXPECT_EQ(find(editor.document().root, "openTracks"), nullptr);
}
#endif

} // namespace

int main(int argc, char** argv)
{
#ifdef WIOLA_TEST_TSAN
    qputenv("QT_QPA_PLATFORM", "minimal");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
