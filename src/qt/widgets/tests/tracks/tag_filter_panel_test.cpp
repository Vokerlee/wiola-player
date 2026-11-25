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

#include <qt/widgets/tracks/tag_filter_panel.hpp>

#include <QApplication>

#include <algorithm>
#include <gtest/gtest.h>
#include <presentation/tracks/track_view_state.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>

namespace {

TEST(TagFilterPanel, AddsIncludeExcludeAndRemovableChips)
{
    wiola::gui::TagFilterPanel panel;
    QLineEdit* key = panel.findChild<QLineEdit*>("tagFilterKey");
    QLineEdit* value = panel.findChild<QLineEdit*>("tagFilterValue");
    ASSERT_NE(key, nullptr);
    ASSERT_NE(value, nullptr);

    std::vector<wiola::library::TagFilter> filters;
    panel.on_edit([&](wiola::gui::TagFilterEdit edit) {
        if (edit.kind == wiola::gui::TagFilterEdit::Kind::add)
            filters.push_back(edit.filter);
        else
            std::erase(filters, edit.filter);
        wiola::gui::TrackViewState state;
        state.query.filters = filters;
        filters = wiola::gui::normalized_track_view_state(std::move(state)).query.filters;
        panel.show_filters(filters);
    });

    key->setText(" Genre ");
    value->setText(" Jazz ");
    panel.findChild<QPushButton*>("includeTagFilter")->click();

    ASSERT_EQ(filters.size(), 1);
    EXPECT_EQ(filters[0].key, "genre");
    EXPECT_EQ(filters[0].value, "jazz");
    EXPECT_EQ(filters[0].mode, wiola::library::TagFilterMode::include);

    key->setText("Mood");
    value->clear();
    panel.findChild<QPushButton*>("excludeTagFilter")->click();

    ASSERT_EQ(filters.size(), 2);
    EXPECT_EQ(filters[1].key, "mood");
    EXPECT_TRUE(filters[1].value.empty());
    EXPECT_EQ(filters[1].mode, wiola::library::TagFilterMode::exclude);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QList<QPushButton*> chips = panel.findChildren<QPushButton*>("tagFilterChip");
    ASSERT_EQ(chips.size(), 2);
    chips.front()->click();
    ASSERT_EQ(filters.size(), 1);
    EXPECT_EQ(filters.front().key, "mood");
}

TEST(TagFilterPanel, RejectsBlankAndNormalizedDuplicateFilters)
{
    wiola::gui::TagFilterPanel panel;
    QLineEdit* key = panel.findChild<QLineEdit*>("tagFilterKey");
    QLineEdit* value = panel.findChild<QLineEdit*>("tagFilterValue");

    int changes = 0;
    wiola::gui::TrackViewState state;
    panel.on_edit([&](wiola::gui::TagFilterEdit edit) {
        auto next = state;
        next.query.filters.push_back(edit.filter);
        next = wiola::gui::normalized_track_view_state(std::move(next));
        if (next != state)
            ++changes;
        state = std::move(next);
        panel.show_filters(state.query.filters);
    });

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
    return wiola::testing::run_gui_tests(argc, argv);
}
