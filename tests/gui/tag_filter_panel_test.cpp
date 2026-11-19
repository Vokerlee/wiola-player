/**
 * @file
 * @brief Include/exclude tag filter chip regression tests.
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

#include <gui/tag_filter_panel.hpp>

#include <QApplication>

#include <gtest/gtest.h>

namespace {

TEST(TagFilterPanel, AddsIncludeExcludeAndRemovableChips)
{
    wiola::gui::TagFilterPanel panel;
    QLineEdit* key = panel.findChild<QLineEdit*>("tagFilterKey");
    QLineEdit* value = panel.findChild<QLineEdit*>("tagFilterValue");
    ASSERT_NE(key, nullptr);
    ASSERT_NE(value, nullptr);

    std::vector<wiola::library::TagFilter> filters;
    panel.on_changed([&](std::vector<wiola::library::TagFilter> changed) {
        filters = std::move(changed);
    });

    key->setText(" Genre ");
    value->setText(" Jazz ");
    panel.findChild<QPushButton*>("includeTagFilter")->click();

    ASSERT_EQ(filters.size(), 1);
    EXPECT_EQ(filters[0].key, "Genre");
    EXPECT_EQ(filters[0].value, "Jazz");
    EXPECT_EQ(filters[0].mode, wiola::library::TagFilterMode::include);

    key->setText("Mood");
    value->clear();
    panel.findChild<QPushButton*>("excludeTagFilter")->click();

    ASSERT_EQ(filters.size(), 2);
    EXPECT_EQ(filters[1].key, "Mood");
    EXPECT_TRUE(filters[1].value.empty());
    EXPECT_EQ(filters[1].mode, wiola::library::TagFilterMode::exclude);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QList<QPushButton*> chips = panel.findChildren<QPushButton*>("tagFilterChip");
    ASSERT_EQ(chips.size(), 2);
    chips.front()->click();
    ASSERT_EQ(filters.size(), 1);
    EXPECT_EQ(filters.front().key, "Mood");
}

TEST(TagFilterPanel, RejectsBlankAndNormalizedDuplicateFilters)
{
    wiola::gui::TagFilterPanel panel;
    QLineEdit* key = panel.findChild<QLineEdit*>("tagFilterKey");
    QLineEdit* value = panel.findChild<QLineEdit*>("tagFilterValue");

    int changes = 0;
    panel.on_changed([&](std::vector<wiola::library::TagFilter>) { ++changes; });

    key->setText(" ");
    panel.findChild<QPushButton*>("includeTagFilter")->click();
    EXPECT_EQ(changes, 0);

    key->setText("Genre");
    value->setText("Jazz");
    panel.findChild<QPushButton*>("includeTagFilter")->click();
    EXPECT_EQ(changes, 1);

    key->setText(" genre ");
    value->setText(" jazz ");
    panel.findChild<QPushButton*>("includeTagFilter")->click();
    EXPECT_EQ(changes, 1);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
