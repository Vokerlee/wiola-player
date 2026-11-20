/**
 * @file
 * @brief Metadata conflict resolution control regression tests.
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

#include <gui/metadata_conflict_panel.hpp>

#include <QApplication>

#include <gtest/gtest.h>

namespace {

TEST(MetadataConflictPanel, RoutesEveryChoiceAndRequiresReviewBeforeOverwrite)
{
    wiola::gui::MetadataConflictPanel panel;
    EXPECT_FALSE(panel.isVisible());

    panel.show_conflict("External metadata changed");
    EXPECT_TRUE(panel.isVisible());
    QPushButton* overwrite = panel.findChild<QPushButton*>("overwriteMetadataConflict");
    ASSERT_NE(overwrite, nullptr);
    EXPECT_FALSE(overwrite->isEnabled());

    std::vector<wiola::gui::MetadataConflictAction> actions;
    panel.on_action([&](wiola::gui::MetadataConflictAction action) { actions.push_back(action); });

    panel.findChild<QPushButton*>("reloadEmbeddedMetadata")->click();
    panel.findChild<QPushButton*>("keepApplicationMetadata")->click();
    panel.findChild<QPushButton*>("mergeMetadataFields")->click();
    panel.findChild<QPushButton*>("reviewMetadataConflict")->click();
    EXPECT_TRUE(overwrite->isEnabled());
    overwrite->click();

    ASSERT_EQ(actions.size(), 5);
    EXPECT_EQ(actions[0], wiola::gui::MetadataConflictAction::reload);
    EXPECT_EQ(actions[1], wiola::gui::MetadataConflictAction::keep);
    EXPECT_EQ(actions[2], wiola::gui::MetadataConflictAction::merge);
    EXPECT_EQ(actions[3], wiola::gui::MetadataConflictAction::review);
    EXPECT_EQ(actions[4], wiola::gui::MetadataConflictAction::overwrite);

    panel.hide_conflict();
    EXPECT_FALSE(panel.isVisible());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
