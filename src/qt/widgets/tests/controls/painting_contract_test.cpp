#include <QAccessible>

#include <QAccessibleValueInterface>
#include <QAction>
#include <QCheckBox>
#include <QImage>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSlider>
#include <app/playback/playback_service.hpp>
#include <fakes/output.hpp>
#include <fixtures/wav.hpp>
#include <limits>
#include <media/local/provider.hpp>
#include <media/local/resource.hpp>
#include <qt/application/playback/equalizer_control.hpp>
#include <qt/paths.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/controls/equalizer_panel.hpp>
#include <qt/widgets/controls/seek_bar.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>

namespace {

using wiola::testing::GuiInteraction;

TEST_F(GuiInteraction, SeekKeyboardAccessibilityAndDragCancellationHaveDistinctOutcomes)
{
    wiola::gui::SeekBar bar;
    watch(bar);
    bar.resize(200, 12);
    QSignalSpy requested{&bar, &wiola::gui::SeekBar::seek_requested};
    bar.set_fraction(0.5);
    QTest::keyClick(&bar, Qt::Key_Right);
    EXPECT_NEAR(bar.fraction(), 0.51, 1e-9);
    QTest::keyClick(&bar, Qt::Key_PageDown);
    EXPECT_NEAR(bar.fraction(), 0.41, 1e-9);
    QTest::keyClick(&bar, Qt::Key_Home);
    EXPECT_EQ(bar.fraction(), 0);
    QTest::keyClick(&bar, Qt::Key_End);
    EXPECT_EQ(bar.fraction(), 1);
    auto* accessible = QAccessible::queryAccessibleInterface(&bar);
    ASSERT_NE(accessible, nullptr);
    EXPECT_EQ(accessible->role(), QAccessible::Slider);
    EXPECT_EQ(accessible->text(QAccessible::Name), "Playback position");
    auto* value = accessible->valueInterface();
    ASSERT_NE(value, nullptr);
    value->setCurrentValue(25);
    EXPECT_EQ(bar.fraction(), 0.25);
    EXPECT_EQ(value->currentValue().toDouble(), 25);
    EXPECT_EQ(value->minimumValue().toInt(), 0);
    EXPECT_EQ(value->maximumValue().toInt(), 100);
    EXPECT_EQ(value->minimumStepSize().toInt(), 1);
    value->setCurrentValue("invalid");
    value->setCurrentValue(std::numeric_limits<double>::infinity());
    QTest::keyClick(&bar, Qt::Key_A);
    const auto before = requested.count();
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{150, 6});
    ASSERT_TRUE(bar.dragging());
    QTest::keyClick(&bar, Qt::Key_Escape);
    EXPECT_FALSE(bar.dragging());
    EXPECT_EQ(bar.fraction(), 0.25);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{150, 6});
    EXPECT_EQ(requested.count(), before);
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{150, 6});
    bar.setEnabled(false);
    EXPECT_FALSE(bar.dragging());
    EXPECT_EQ(bar.fraction(), 0.25);
    value->setCurrentValue(80);
    QTest::keyClick(&bar, Qt::Key_End);
    EXPECT_EQ(requested.count(), before);
    bar.set_fraction(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(bar.fraction(), 0.25);
}

TEST_F(GuiInteraction, SeekDragCancellationTakesPrecedenceOverTheWindowStopShortcut)
{
    wiola::gui::SeekBar bar;
    watch(bar);
    QAction stop{&bar};
    stop.setShortcut(QKeySequence{Qt::Key_Escape});
    bar.addAction(&stop);
    int stops{};
    QObject::connect(&stop, &QAction::triggered, [&] { ++stops; });
    bar.resize(200, 12);
    bar.show();
    bar.activateWindow();
    bar.setFocus();
    ASSERT_TRUE(wiola::testing::await_gui([&] { return bar.hasFocus(); }));
    bar.set_fraction(0.25);
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{150, 6});
    ASSERT_TRUE(bar.dragging());
    QTest::keyClick(&bar, Qt::Key_Escape);
    EXPECT_FALSE(bar.dragging());
    EXPECT_EQ(bar.fraction(), 0.25);
    EXPECT_EQ(stops, 0);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{150, 6});
    QTest::keyClick(&bar, Qt::Key_Escape);
    EXPECT_EQ(stops, 1);
}

TEST_F(GuiInteraction, FocusIsVisibleAndMouseCommitDiffersFromCancelledOrDisplayUpdates)
{
    wiola::gui::SeekBar bar;
    watch(bar);
    bar.resize(200, 12);
    bar.show();
    bar.activateWindow();
    bar.setFocus();
    ASSERT_TRUE(wiola::testing::await_gui([&] { return bar.hasFocus(); }));
    QPalette palette;
    palette.setColor(QPalette::Mid, QColor{"#222222"});
    palette.setColor(QPalette::Highlight, QColor{"#ffee00"});
    bar.setPalette(palette);
    QSignalSpy committed{&bar, &wiola::gui::SeekBar::seek_requested};
    QTest::keyClick(&bar, Qt::Key_Down);
    EXPECT_EQ(bar.fraction(), 0);
    QTest::keyClick(&bar, Qt::Key_Up);
    QTest::keyClick(&bar, Qt::Key_PageUp);
    QTest::keyClick(&bar, Qt::Key_Left);
    EXPECT_NEAR(bar.fraction(), 0.1, 1e-9);
    QTest::mouseClick(&bar, Qt::RightButton, Qt::NoModifier, QPoint{150, 6});
    EXPECT_FALSE(bar.dragging());
    const auto before = committed.count();
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{40, 6});
    QTest::mouseMove(&bar, QPoint{80, 6});
    const auto dragged = bar.fraction();
    bar.set_fraction(0.8);
    EXPECT_EQ(bar.fraction(), dragged);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, QPoint{80, 6});
    EXPECT_FALSE(bar.dragging());
    EXPECT_EQ(committed.count(), before + 1);
    EXPECT_NEAR(committed.back().front().toDouble(), 0.4, 1e-9);
    QImage image{bar.size(), QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    bar.render(&image);
    EXPECT_EQ(image.pixelColor(150, 0), QColor{"#ffee00"});
    EXPECT_EQ(image.pixelColor(150, 6), QColor{"#222222"});
}

TEST_F(GuiInteraction, LargeFontEqualizerRemainsScrollableAndKeyboardAdjustsTheLiveBand)
{
    wiola::app::PlaybackService playback{wiola::testing::passive_output,
        std::make_shared<wiola::codec::LocalMediaProvider>()};
    const auto generated = wiola::testing::write_wav("wiola-equalizer-access.wav");
    const auto path = wiola::platform::qt::QtPathAdapter::from_qt(data_.filePath("equalizer.wav"));
    std::filesystem::copy_file(generated, path);
    std::filesystem::remove(generated);
    static_cast<void>(playback.open({wiola::media::local_resource(path)}));
    ASSERT_TRUE(wiola::testing::await_gui([&] {
        static_cast<void>(playback.pump());
        return playback.loaded();
    }));

    QSettings settings{data_.filePath("equalizer.ini"), QSettings::IniFormat};
    wiola::app::EqualizerControl control{playback, settings};
    wiola::gui::EqualizerPanel panel{control, nullptr};
    watch(panel);
    panel.setUpdatesEnabled(false);
    QFont large = panel.font();
    large.setPointSize(24);
    panel.setFont(large);
    panel.resize(500, 300);
    panel.show();
    auto* scroll = panel.findChild<QScrollArea*>("equalizerBandsScroll");
    ASSERT_NE(scroll, nullptr);
    const auto sliders = panel.findChildren<QSlider*>();
    ASSERT_FALSE(sliders.empty());
    for (auto* slider : sliders)
        EXPECT_FALSE(slider->accessibleName().isEmpty());
    auto* band = sliders.front();
    band->setFocus();
    QTest::keyClick(band, Qt::Key_Right);
    EXPECT_FLOAT_EQ(control.band_gain(0), 0.5F);
    auto* enabled = panel.findChild<QCheckBox*>();
    ASSERT_NE(enabled, nullptr);
    const bool previous = control.enabled();
    QTest::keyClick(enabled, Qt::Key_Space);
    EXPECT_NE(control.enabled(), previous);
    EXPECT_FALSE(enabled->accessibleName().isEmpty());
}

TEST_F(GuiInteraction, ControlledSeekPainterSeparatesPlayedTrackAndDisabledColorsAtEachDpr)
{
    for (double dpr : {1.0, 1.25, 1.5, 2.0}) {
        SCOPED_TRACE(dpr);
        wiola::gui::SeekBar bar;
        bar.resize(200, 12);
        QPalette palette;
        palette.setColor(QPalette::Mid, QColor{"#222222"});
        palette.setColor(QPalette::Highlight, QColor{"#ffee00"});
        palette.setColor(QPalette::Dark, QColor{"#666666"});
        palette.setColor(QPalette::WindowText, QColor{"#ffffff"});
        bar.setPalette(palette);
        bar.set_fraction(0.25);
        const auto render = [&] {
            QImage image{
                QSize{static_cast<int>(200 * dpr), static_cast<int>(12 * dpr)},
                QImage::Format_ARGB32_Premultiplied
            };
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            bar.render(&image);
            return image;
        };
        const auto enabled = render();
        EXPECT_EQ(enabled.pixelColor(static_cast<int>(25 * dpr), static_cast<int>(6 * dpr)),
            QColor{"#ffee00"});
        EXPECT_EQ(enabled.pixelColor(static_cast<int>(150 * dpr), static_cast<int>(6 * dpr)),
            QColor{"#222222"});
        bar.setEnabled(false);
        const auto disabled = render();
        EXPECT_EQ(disabled.pixelColor(static_cast<int>(25 * dpr), static_cast<int>(6 * dpr)),
            QColor{"#666666"});
        EXPECT_EQ(disabled.pixelColor(static_cast<int>(150 * dpr), static_cast<int>(6 * dpr)),
            QColor{"#222222"});
    }
}

} // namespace

int main(int argc, char** argv)
{
    return wiola::testing::run_gui_tests(argc, argv);
}
