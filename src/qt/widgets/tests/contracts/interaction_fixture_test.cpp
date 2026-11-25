#include <QSignalSpy>
#include <qt/widgets/metadata/standard_metadata_editor.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>
#include <qt/widgets/tracks/track_browser.hpp>
#include <qt/widgets/tracks/track_search_panel.hpp>

namespace {

using wiola::testing::GuiInteraction;

TEST_F(GuiInteraction, KeyboardSearchAndResetReachTheActualQueryObserver)
{
    wiola::gui::TrackSearchPanel panel;
    watch(panel);
    panel.setUpdatesEnabled(false);
    panel.resize(450, 240);
    panel.show();
    std::optional<wiola::library::SearchQuery> query;
    panel.on_changed([&](auto value) { query = std::move(value); });
    auto* title = panel.findChild<QLineEdit*>("searchTitle");
    auto* artist = panel.findChild<QLineEdit*>("searchArtist");
    title->setFocus();
    QTest::keyClicks(title, "Song");
    QTest::keyClick(title, Qt::Key_Tab);
    ASSERT_TRUE(wiola::testing::await_gui([&] { return artist->hasFocus(); }));
    QTest::keyClicks(artist, "Artist");
    ASSERT_TRUE(query);
    EXPECT_EQ(query->title, "Song");
    EXPECT_EQ(query->artist, "Artist");
    auto* reset = panel.findChild<QPushButton*>("resetTrackSearch");
    reset->setFocus();
    QTest::keyClick(reset, Qt::Key_Space);
    ASSERT_TRUE(query);
    EXPECT_TRUE(query->title.empty());
    EXPECT_TRUE(query->artist.empty());
    EXPECT_TRUE(title->text().isEmpty());
    EXPECT_TRUE(artist->text().isEmpty());
}

TEST_F(GuiInteraction, DisabledAndInvalidMetadataCannotFireButValidInputFiresExactlyOnce)
{
    wiola::gui::StandardMetadataEditor editor;
    watch(editor);
    editor.setUpdatesEnabled(false);
    editor.resize(500, 500);
    editor.show();
    int applied = 0;
    editor.on_apply([&](const auto&) { ++applied; });
    auto* apply = editor.findChild<QPushButton*>("applyStandardMetadata");
    ASSERT_FALSE(apply->isEnabled());
    QTest::mouseClick(apply, Qt::LeftButton);
    EXPECT_EQ(applied, 0);
    wiola::library::TrackRecord record;
    record.id = {"selected"};
    editor.show_track(&record);
    auto* year = editor.findChild<QLineEdit*>("standardYear");
    QTest::keyClicks(year, "bad");
    EXPECT_FALSE(apply->isEnabled());
    QTest::mouseClick(apply, Qt::LeftButton);
    EXPECT_EQ(applied, 0);
    year->selectAll();
    QTest::keyClicks(year, "2026");
    ASSERT_TRUE(apply->isEnabled());
    apply->setFocus();
    QTest::keyClick(apply, Qt::Key_Space);
    EXPECT_EQ(applied, 1);
}

TEST_F(GuiInteraction, MouseSelectionUsesOccurrenceIdentityAndEmitsTheBrowserSignal)
{
    wiola::gui::TrackBrowser browser{wiola::gui::TrackCapabilities::queue()};
    watch(browser);
    browser.setUpdatesEnabled(false);
    browser.resize(600, 650);
    wiola::gui::TrackRow first, second;
    first.id = wiola::engine::QueueEntryId{1};
    first.media = {"shared"};
    first.metadata.title = "One";
    second = first;
    second.id = wiola::engine::QueueEntryId{2};
    browser.show_rows({first, second});
    browser.show();
    auto& table = browser.table();
    QSignalSpy changed{&browser, &wiola::gui::TrackBrowser::selection_changed};
    const auto index = table.model()->index(1, 1);
    table.scrollTo(index);
    QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::NoModifier,
        table.visualRect(index).center());
    EXPECT_EQ(table.chosen_rows(), (std::vector<wiola::gui::TrackRowId>{second.id}));
    EXPECT_EQ(changed.count(), 1);
}

} // namespace

int main(int argc, char** argv)
{
    return wiola::testing::run_gui_tests(argc, argv);
}
