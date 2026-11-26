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

#include <qt/widgets/layout/classic_layout.hpp>
#include <qt/widgets/layout/editor/layout_editor_dialog.hpp>

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTest>
#include <qt/widgets/appearance/theme.hpp>

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

TEST(ButtonLayoutEditor, TextEditingShortcutsDoNotDeleteComponentsAndSmallWindowsCanScroll)
{
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
    // This contract drives focus/input/geometry. Rendering is qualified separately.
    editor.setUpdatesEnabled(false);
    QFont large = editor.font();
    large.setPointSize(24);
    editor.setFont(large);
    editor.resize(640, 400);
    editor.show();
    editor.activateWindow();
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    tree->setCurrentItem(item(*tree, "transportControls"));
    auto* title = editor.findChild<QLineEdit*>("layoutComponentTitle");
    ASSERT_NE(title, nullptr);
    title->setFocus();
    QTest::keyClicks(title, "Draft");
    title->setCursorPosition(0);
    QTest::keyClick(title, Qt::Key_Delete);
    EXPECT_EQ(title->text(), "raft");
    EXPECT_NE(find(editor.document().root, "transportControls"), nullptr);
    auto* scroll = editor.findChild<QScrollArea*>("layoutEditorScroll");
    ASSERT_NE(scroll, nullptr);
    scroll->widget()->layout()->activate();
    editor.layout()->activate();
    EXPECT_GT(scroll->horizontalScrollBar()->maximum() + scroll->verticalScrollBar()->maximum(), 0);
    auto* cancel = editor.findChild<QPushButton*>("cancelLayoutEdit");
    ASSERT_NE(cancel, nullptr);
    EXPECT_TRUE(cancel->isVisibleTo(&editor));
    cancel->setFocus();
    QTest::keyClick(cancel, Qt::Key_Space);
    EXPECT_FALSE(editor.isVisible());
}

TEST(ButtonLayoutEditor, ReadableResizablePanelsAndPreviewHaveIndependentScrollAreas)
{
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
    editor.resize(editor.fontMetrics().averageCharWidth() * 200,
        editor.fontMetrics().height() * 50);
    editor.show();
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    auto* palette = editor.findChild<QListWidget*>("layoutComponentPalette");
    auto* columns = editor.findChild<QSplitter*>("layoutEditorColumns");
    auto* rows = editor.findChild<QSplitter*>("layoutEditorRows");
    auto* scroll = editor.findChild<QScrollArea*>("layoutEditorScroll");
    auto* preview = editor.findChild<QScrollArea*>("layoutPreviewScroll");
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(palette, nullptr);
    ASSERT_NE(columns, nullptr);
    ASSERT_NE(rows, nullptr);
    ASSERT_NE(scroll, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_EQ(columns->count(), 3);
    ASSERT_EQ(rows->count(), 2);
    const auto visible_in = [](const QWidget& viewport, const QWidget& widget) {
        return viewport.rect().contains(QRect{widget.mapTo(&viewport, QPoint{}), widget.size()});
    };
    for (const auto theme : {wiola::gui::Theme::light, wiola::gui::Theme::dark}) {
        wiola::gui::apply_theme(theme);
        QApplication::processEvents();
        EXPECT_GE(tree->viewport()->width(), editor.fontMetrics().averageCharWidth() * 36);
        EXPECT_TRUE(visible_in(*scroll->viewport(), *tree));
        EXPECT_TRUE(visible_in(*scroll->viewport(), *palette));
        for (const auto* name : {"addLayoutAction", "removeLayoutComponent", "addLayoutContainer",
                 "undoLayoutEdit", "redoLayoutEdit"}) {
            auto* command = editor.findChild<QPushButton*>(name);
            ASSERT_NE(command, nullptr);
            EXPECT_TRUE(visible_in(*scroll->viewport(), *command)) << name;
        }
        EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
        EXPECT_TRUE(visible_in(*scroll->viewport(), *preview));
        EXPECT_GT(tree->columnWidth(0), tree->fontMetrics().horizontalAdvance("transportControls"));
        const auto directory = qEnvironmentVariable("WIOLA_GUI_PREVIEW_DIR");
        if (!directory.isEmpty()) {
            QDir{}.mkpath(directory);
            EXPECT_TRUE(editor.grab().save(QDir{directory}.filePath(theme == wiola::gui::Theme::dark
                    ? "layout-editor-dark.png"
                    : "layout-editor-light.png")));
        }
    }
    tree->setCurrentItem(item(*tree, "openTracks"));
    const auto panel_sizes = columns->sizes();
    auto* width = editor.findChild<QSpinBox*>("layoutComponentWidth");
    ASSERT_NE(width, nullptr);
    width->setValue(width->maximum());
    emit width->editingFinished();
    QApplication::processEvents();
    EXPECT_GT(preview->horizontalScrollBar()->maximum(), 0);
    EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
    EXPECT_EQ(columns->sizes(), panel_sizes);
    editor.findChild<QPushButton*>("undoLayoutEdit")->click();
    const int initial = tree->width();
    const auto sizes = columns->sizes();
    columns->setSizes({sizes[0], sizes[1] + sizes[2] / 2, sizes[2] / 2});
    QApplication::processEvents();
    EXPECT_GT(tree->width(), initial);
    editor.resize(640, 480);
    QApplication::processEvents();
    EXPECT_GE(tree->viewport()->width(), editor.fontMetrics().averageCharWidth() * 36);
    for (const auto* name : {"applyLayoutEdit", "cancelLayoutEdit"}) {
        auto* command = editor.findChild<QPushButton*>(name);
        ASSERT_NE(command, nullptr);
        EXPECT_TRUE(visible_in(editor, *command));
    }
}

TEST(ButtonLayoutEditor, InvalidDraftBlocksApplyAndReturnCommitsPropertiesWithoutClosing)
{
    using namespace wiola::gui;
    LayoutDocument initial{
        .root = {.id = "split",
                 .kind = LayoutComponentKind::splitter,
                 .children = {{.id = "left", .kind = LayoutComponentKind::status},
                {.id = "right", .kind = LayoutComponentKind::status}}},
        .node_count = 3
    };
    LayoutEditorDialog editor{initial};
    int attempts{};
    LayoutDocument accepted;
    editor.on_apply([&](const LayoutDocument& document) {
        accepted = document;
        return ++attempts == 2;
    });
    editor.show();
    editor.activateWindow();
    auto* weights = editor.findChild<QLineEdit*>("layoutSplitterSizes");
    auto* title = editor.findChild<QLineEdit*>("layoutComponentTitle");
    auto* apply = editor.findChild<QPushButton*>("applyLayoutEdit");
    ASSERT_NE(weights, nullptr);
    ASSERT_NE(title, nullptr);
    ASSERT_NE(apply, nullptr);
    weights->setFocus();
    QTest::keyClicks(weights, "invalid");
    QTest::keyClick(weights, Qt::Key_Return);
    EXPECT_TRUE(editor.isVisible());
    EXPECT_EQ(attempts, 0);
    apply->click();
    EXPECT_TRUE(editor.isVisible());
    EXPECT_EQ(attempts, 0);
    EXPECT_EQ(weights->text(), "invalid");
    weights->setText("1, 2");
    QTest::keyClick(weights, Qt::Key_Return);
    EXPECT_TRUE(editor.isVisible());
    EXPECT_EQ(attempts, 0);
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{1, 2}));
    editor.findChild<QPushButton*>("undoLayoutEdit")->click();
    EXPECT_TRUE(editor.document().root.sizes.empty());
    editor.findChild<QPushButton*>("redoLayoutEdit")->click();
    EXPECT_EQ(editor.document().root.sizes, (std::vector<int>{1, 2}));
    title->setText("Final draft");
    apply->click();
    EXPECT_EQ(attempts, 1);
    EXPECT_TRUE(editor.isVisible());
    EXPECT_EQ(accepted.root.title, "Final draft");
    EXPECT_EQ(editor.findChild<QLabel*>("layoutEditorStatus")->text(),
        "Could not apply this layout");
    apply->click();
    EXPECT_EQ(attempts, 2);
    EXPECT_EQ(editor.result(), QDialog::Accepted);
    EXPECT_FALSE(editor.isVisible());
}

TEST(ButtonLayoutEditor, RowAlignmentCanBeEditedUndoneAndResetToAutomatic)
{
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
    auto* tree = editor.findChild<QTreeWidget*>("layoutHierarchy");
    tree->setCurrentItem(item(*tree, "transportControls"));
    auto* alignment = editor.findChild<QComboBox*>("layoutRowAlignment");
    ASSERT_NE(alignment, nullptr);
    EXPECT_EQ(alignment->currentData().toInt(), static_cast<int>(Qt::AlignHCenter));
    for (const auto value :
        {static_cast<int>(Qt::AlignLeft), static_cast<int>(Qt::AlignRight), 0}) {
        alignment->setCurrentIndex(alignment->findData(value));
        emit alignment->activated(alignment->currentIndex());
        const auto* row = find(editor.document().root, "transportControls");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->alignment ? static_cast<int>(*row->alignment) : 0, value);
        editor.findChild<QPushButton*>("undoLayoutEdit")->click();
        EXPECT_EQ(find(editor.document().root, "transportControls")->alignment, Qt::AlignHCenter);
    }
}

TEST(ButtonLayoutEditor, AddsConfiguresAndAppliesPaletteActions)
{
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
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
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
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
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
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
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
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
    wiola::gui::LayoutEditorDialog editor{wiola::gui::classic_layout_document()};
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

TEST(LayoutEditorComponents, InvalidPropertyInputRetainsItsDraftUntilCorrected)
{
    using namespace wiola::gui;
    LayoutPropertyInspector inspector;
    LayoutNode node{
        .id = "split",
        .kind = LayoutComponentKind::splitter,
        .children = {{.id = "first", .kind = LayoutComponentKind::status},
                     {.id = "second", .kind = LayoutComponentKind::status}}
    };
    inspector.show_node(&node);
    int accepted{}, rejected{};
    inspector.show();
    auto* weights = inspector.findChild<QLineEdit*>("layoutSplitterSizes");
    ASSERT_NE(weights, nullptr);
    weights->setFocus();
    QApplication::processEvents();
    inspector.on_changed([&] {
        if (auto edited = inspector.configuration(node)) {
            node = *edited;
            ++accepted;
        } else
            ++rejected;
    });
    QTest::keyClicks(weights, "bad");
    QTest::keyClick(weights, Qt::Key_Return);
    EXPECT_EQ(rejected, 1);
    EXPECT_EQ(accepted, 0);
    EXPECT_EQ(weights->text(), "bad");
    EXPECT_TRUE(node.sizes.empty());
    EXPECT_TRUE(inspector.error().contains("positive"));
    EXPECT_FALSE(weights->accessibleDescription().isEmpty());
    weights->selectAll();
    QTest::keyClicks(weights, "1, 3");
    QTest::keyClick(weights, Qt::Key_Return);
    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(node.sizes, (std::vector<int>{1, 3}));
    EXPECT_TRUE(inspector.error().isEmpty());
}

TEST(LayoutEditorComponents, FailedPreviewPreparationRetainsTheLastGoodIsolatedTree)
{
    using namespace wiola::gui;
    LayoutPreviewHost preview;
    LayoutDocument document{
        .root = {.id = "previewPlay",
                 .kind = LayoutComponentKind::button,
                 .action = PlayerAction::play},
        .node_count = 1
    };
    ASSERT_TRUE(preview.show_document(document));
    QPointer<QPushButton> original = preview.findChild<QPushButton*>("previewPlay");
    ASSERT_NE(original, nullptr);
    document.root.kind = static_cast<LayoutComponentKind>(999);
    EXPECT_FALSE(preview.show_document(document));
    EXPECT_NE(original, nullptr);
    EXPECT_EQ(preview.findChild<QPushButton*>("previewPlay"), original.data());
}

TEST(LayoutEditorComponents, RejectsOversizedAndUnknownDragPayloadsWithoutIssuingEditRequests)
{
    wiola::gui::LayoutHierarchy hierarchy;
    int drops{};
    hierarchy.on_drop([&](auto) { ++drops; });
    for (const auto& payload :
        {QByteArray(300, 'x'), QByteArray{"action:unknown"}, QByteArray{"arbitrary"}}) {
        QMimeData data;
        data.setData("application/x-wiola-layout-component", payload);
        QDragEnterEvent enter{
            QPoint{1, 1},
            Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier
        };
        QApplication::sendEvent(hierarchy.viewport(), &enter);
        EXPECT_FALSE(enter.isAccepted());
        QDropEvent drop{
            QPointF{1, 1},
            Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier
        };
        QApplication::sendEvent(hierarchy.viewport(), &drop);
        EXPECT_FALSE(drop.isAccepted());
    }
    EXPECT_EQ(drops, 0);
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#ifdef WIOLA_TEST_TSAN
        qputenv("QT_QPA_PLATFORM", "minimal");
#elif defined(Q_OS_WIN)
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
