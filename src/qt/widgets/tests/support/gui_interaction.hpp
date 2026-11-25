#pragma once
#include <QApplication>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>
#include <qt/widgets/tests/support/widget_diagnostics.hpp>
#include <stdexcept>

namespace wiola::testing {

class IsolatedSettings final {
public:
    IsolatedSettings()
    {
        if (!directory_.isValid())
            throw std::runtime_error{"Cannot create isolated GUI settings"};
        const QSettings probe{QSettings::IniFormat, QSettings::UserScope, "wiola-path-probe",
            "probe"};
        auto parent = QFileInfo{probe.fileName()}.dir();
        parent.cdUp();
        previous_ = parent.path();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory_.path());
    }

    ~IsolatedSettings()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, previous_);
    }

    const QString path() const { return directory_.path(); }

private:
    QTemporaryDir directory_;
    QString previous_;
};

template<typename Predicate>
bool await_gui(Predicate&& predicate, int timeout_ms = 5000)
{
    return QTest::qWaitFor(std::forward<Predicate>(predicate), timeout_ms);
}

class GuiInteraction : public ::testing::Test {
protected:
    void watch(QWidget& window) { windows_.push_back(&window); }

    void TearDown() override
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::setPalette(palette_);
        QApplication::setFont(font_);
        for (const auto& window : windows_)
            EXPECT_TRUE(window.isNull()) << "An interaction fixture retained a test window";
    }

    QPalette palette_{QApplication::palette()};
    QFont font_{QApplication::font()};
    IsolatedSettings settings_;
    QTemporaryDir data_;

private:
    std::vector<QPointer<QWidget>> windows_;
};

inline int run_gui_tests(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#ifdef Q_OS_WIN
        qputenv("QT_QPA_PLATFORM", "windows");
#else
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    }
    ::testing::InitGoogleTest(&argc, argv);
    QApplication application{argc, argv};
    QStandardPaths::setTestModeEnabled(true);
    IsolatedSettings isolated;
    retain_widget_screenshots();
    return RUN_ALL_TESTS();
}

} // namespace wiola::testing
