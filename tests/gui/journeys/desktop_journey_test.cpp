#include "session_journey.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <fixtures/atomic_output.hpp>
#include <fixtures/widget_diagnostics.hpp>
#include <gui/appearance/theme_manager.hpp>
#include <gui/layout/classic_layout.hpp>
#include <iostream>

namespace {

TEST(DesktopJourney, RestoresIndependentDocumentsQueriesAndDuplicateOccurrences)
{
    ASSERT_NO_FATAL_FAILURE(wiola::testing::restore_session_journey());
}

TEST(DesktopJourney, FailedPreferencesRetainAcceptedDataAndCanBeRetried)
{
    QTemporaryDir directory;
    QSettings settings{directory.filePath("preferences.ini"), QSettings::IniFormat};
    bool fail = false;
    wiola::gui::ThemeManager themes{settings,
        [&](const auto& path) {
            return std::make_unique<wiola::testing::FaultAtomicOutput>(path, fail);
        }};
    ASSERT_EQ(themes.select(wiola::gui::ThemeChoice::dark), wiola::gui::ThemeError::none);
    const auto accepted = themes.export_json();
    fail = true;
    EXPECT_EQ(themes.select(wiola::gui::ThemeChoice::light), wiola::gui::ThemeError::io);
    EXPECT_EQ(themes.export_json(), accepted);
    fail = false;
    EXPECT_EQ(themes.select(wiola::gui::ThemeChoice::light), wiola::gui::ThemeError::none);
    wiola::gui::ThemeManager restored{settings};
    EXPECT_EQ(restored.state().selected, wiola::gui::ThemeChoice::light);
    auto custom = wiola::gui::classic_layout_document();
    custom.root.id = "journey-custom-root";
    wiola::gui::LayoutDocument decoded;
    const auto wire = wiola::gui::write_layout_json(custom);
    ASSERT_EQ(wiola::gui::read_layout_json(wire, decoded), wiola::gui::LayoutJsonError::none);
    EXPECT_EQ(decoded.root.id, "journey-custom-root");
}

} // namespace

int main(int argc, char** argv)
{
    // Native Windows uses its linked platform plugin; Linux defaults to offscreen.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    ::testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    QTemporaryDir isolated;
    if (!isolated.isValid())
        return 2;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    wiola::testing::retain_widget_screenshots();
    std::cout << "Qt platform: " << qPrintable(QGuiApplication::platformName()) << '\n';
    return RUN_ALL_TESTS();
}
