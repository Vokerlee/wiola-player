/**
 * @file
 * @brief Shared custom tag editor regression tests.
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

#include <qt/widgets/metadata/custom_tag_editor.hpp>

#include <QApplication>

#include <gtest/gtest.h>

namespace {

TEST(CustomTagEditor, ExplainsSharedValuesAndRoutesAccessibleCommands)
{
    wiola::library::TrackRecord track;
    track.id = wiola::library::MediaId{"media"};
    track.metadata_revision = 1;
    track.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"},
        {.id = wiola::library::TagId{"tag-2"}, .key = "Mood",  .value = "Calm"}
    };

    wiola::gui::CustomTagEditor editor;
    editor.show_track(&track);

    QLabel* explanation = editor.findChild<QLabel*>("customTagExplanation");
    QListWidget* values = editor.findChild<QListWidget*>("customTagValues");
    QLineEdit* key = editor.findChild<QLineEdit*>("customTagKey");
    QLineEdit* value = editor.findChild<QLineEdit*>("customTagValue");
    QPushButton* add = editor.findChild<QPushButton*>("addCustomTag");
    QPushButton* edit = editor.findChild<QPushButton*>("editCustomTag");
    QPushButton* remove = editor.findChild<QPushButton*>("removeCustomTag");

    ASSERT_NE(explanation, nullptr);
    ASSERT_NE(values, nullptr);
    ASSERT_NE(key, nullptr);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(edit, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(explanation->text().contains("Shared across every playlist"));
    EXPECT_TRUE(explanation->text().contains("Embedded metadata is not changed"));
    EXPECT_EQ(values->count(), 2);
    EXPECT_EQ(add->accessibleName(), "Add custom tag");
    EXPECT_EQ(edit->accessibleName(), "Edit custom tag");
    EXPECT_EQ(remove->accessibleName(), "Remove custom tag");

    values->setCurrentRow(1);
    EXPECT_EQ(key->text(), "Mood");
    EXPECT_EQ(value->text(), "Calm");
    EXPECT_TRUE(edit->isEnabled());
    EXPECT_TRUE(remove->isEnabled());

    wiola::library::MediaId edited_media;
    wiola::library::TagId edited_tag;
    std::string edited_key;
    std::string edited_value;
    editor.on_edit([&](wiola::library::MediaId media, wiola::library::TagId tag,
                       std::string new_key, std::string new_value) {
        edited_media = std::move(media);
        edited_tag = std::move(tag);
        edited_key = std::move(new_key);
        edited_value = std::move(new_value);
    });

    key->setText("Mood");
    value->setText("Relaxed");
    edit->click();
    EXPECT_EQ(edited_media, track.id);
    EXPECT_EQ(edited_tag, wiola::library::TagId{"tag-2"});
    EXPECT_EQ(edited_key, "Mood");
    EXPECT_EQ(edited_value, "Relaxed");

    wiola::library::TagId removed;
    editor.on_remove([&](wiola::library::MediaId, wiola::library::TagId tag) { removed = tag; });
    remove->click();
    EXPECT_EQ(removed, wiola::library::TagId{"tag-2"});

    editor.on_add([&](wiola::library::MediaId media, std::string new_key, std::string new_value) {
        EXPECT_EQ(media, track.id);
        EXPECT_EQ(new_key, "Genre");
        EXPECT_EQ(new_value, "Fusion");
    });
    key->setText("Genre");
    value->setText("Fusion");
    add->click();
}

TEST(CustomTagEditor, RefreshesOnlyForANewRevisionAndClearsWithoutATrack)
{
    wiola::library::TrackRecord track;
    track.id = wiola::library::MediaId{"media"};
    track.tags = {
        {.id = wiola::library::TagId{"tag-1"}, .key = "Genre", .value = "Jazz"}
    };

    wiola::gui::CustomTagEditor editor;
    editor.show_track(&track);

    QLineEdit* key = editor.findChild<QLineEdit*>("customTagKey");
    QLineEdit* value = editor.findChild<QLineEdit*>("customTagValue");
    QListWidget* values = editor.findChild<QListWidget*>("customTagValues");
    QPushButton* add = editor.findChild<QPushButton*>("addCustomTag");

    key->setText("Unsaved key");
    value->setText("Unsaved value");
    editor.show_track(&track);
    EXPECT_EQ(key->text(), "Unsaved key");
    EXPECT_EQ(value->text(), "Unsaved value");

    ++track.metadata_revision;
    track.tags.front().value = "Fusion";
    editor.show_track(&track);
    EXPECT_EQ(values->item(0)->text(), "Genre: Fusion");

    editor.show_track(nullptr);
    EXPECT_EQ(values->count(), 0);
    EXPECT_FALSE(add->isEnabled());
    EXPECT_TRUE(key->text().isEmpty());
    EXPECT_TRUE(value->text().isEmpty());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
