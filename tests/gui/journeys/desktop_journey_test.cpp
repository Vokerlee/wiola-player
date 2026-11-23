#include "restart_child.hpp"
#include "session_journey.hpp"
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <fixtures/atomic_output.hpp>
#include <fixtures/gui_interaction.hpp>
#include <fixtures/widget_diagnostics.hpp>
#include <gui/appearance/theme_manager.hpp>
#include <gui/layout/classic_layout.hpp>
#include <iostream>

namespace {

int run_child(const QStringList& arguments, QString program = {}, QByteArray* captured = nullptr)
{
    if (program.isEmpty())
        program = QCoreApplication::applicationFilePath();
    QByteArray output;
    int exit_code = -1;
#ifdef _WIN32
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(program, arguments);
    if (!child.waitForStarted(5000) || !child.waitForFinished(20000)) {
        child.kill();
        child.waitForFinished();
        ADD_FAILURE() << "Child process timeout";
        return -1;
    }
    output = child.readAll();
    if (child.exitStatus() == QProcess::NormalExit)
        exit_code = child.exitCode();
#else
    // Use libc's supported spawn boundary. System Qt's direct clone/vfork optimization can
    // abort the TSan runtime before exec; that would test neither the child nor our lifecycle.
    QTemporaryDir logs;
    const auto log_path = logs.filePath("child.log").toUtf8();
    std::vector<std::string> storage{program.toUtf8().toStdString()};
    for (const auto& argument : arguments)
        storage.push_back(argument.toUtf8().toStdString());
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        ADD_FAILURE() << "Cannot initialize spawn actions";
        return -1;
    }
    const auto opened = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
        log_path.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const auto duplicated =
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid{};
    const int launched = opened || duplicated
        ? EIO
        : posix_spawn(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (launched) {
        ADD_FAILURE() << "Cannot spawn child: " << launched;
        return -1;
    }
    int status{};
    const bool completed =
        QTest::qWaitFor([&] { return waitpid(pid, &status, WNOHANG) == pid; }, 20000);
    if (!completed) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        ADD_FAILURE() << "Child process timeout";
    }
    if (completed && WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    QFile log{QString::fromUtf8(log_path)};
    if (log.open(QIODevice::ReadOnly))
        output = log.readAll();
#endif
    if (captured)
        *captured = output;
    const auto artifacts = qEnvironmentVariable("WIOLA_GUI_FAILURE_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QDir{}.mkpath(artifacts);
        QFile log{QDir{artifacts}.filePath("child-" + arguments.value(1) + "-" +
            arguments.value(3) + ".log")};
        if (log.open(QIODevice::WriteOnly))
            log.write(output);
        std::error_code error;
        const auto from = wiola::platform::qt::QtPathAdapter::from_qt(arguments.value(2));
        const auto to =
            wiola::platform::qt::QtPathAdapter::from_qt(QDir{artifacts}.filePath("child-" +
                arguments.value(1) + "-" + arguments.value(3) + "-state"));
        if (arguments.value(0) == "--wiola-child")
            std::filesystem::copy(from, to,
                std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing,
                error);
        if (error)
            qWarning("Could not retain child state: %s", error.message().c_str());
    }
    std::cout << output.constData();
    if (exit_code < 0)
        ADD_FAILURE() << output.constData();
    return exit_code;
}

TEST(DesktopJourney, VisibleWindowKeepsKeyboardFocusAndCommandsThroughThemeAndLayoutChanges)
{
    using namespace wiola;
    wiola::testing::IsolatedSettings settings_scope;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = platform::qt::QtPathAdapter::from_qt(directory.filePath("paint.wav"));
    const auto generated = wiola::testing::write_wav("visible-journey.wav");
    std::filesystem::copy_file(generated, source);
    std::filesystem::remove(generated);
    app::PlayerApplication player{wiola::testing::child_config(directory.path())};
    {
        gui::MainWindow window{player.playback(), player.library(), player.preferences()};
        player.observe(
            window, [&](const auto& state) { window.update_after_pump(state); },
            [&] { window.refresh(); }
        );
        window.open({source});
        window.resize(1100, 750);
        window.show();
        window.activateWindow();
        ASSERT_TRUE(wiola::testing::await_gui([&] { return player.session().loaded(); }));
        auto* title = window.findChild<QLineEdit*>("searchTitle");
        ASSERT_NE(title, nullptr);
        title->setFocus();
        ASSERT_TRUE(wiola::testing::await_gui([&] { return title->hasFocus(); }));
        QTest::keyClicks(title, "no matching title");
        auto* host = static_cast<gui::DocumentHost*>(window.findChild<QTabWidget*>("documentTabs"));
        ASSERT_NE(host, nullptr);
        EXPECT_EQ(host->queue().browser().table().visible_count(), 0);
        auto* reset = window.findChild<QPushButton*>("resetTrackSearch");
        ASSERT_NE(reset, nullptr);
        ASSERT_TRUE(reset->isVisible());
        reset->setFocus();
        ASSERT_TRUE(wiola::testing::await_gui([&] { return reset->hasFocus(); }));
        QTest::keyClick(reset, Qt::Key_Space);
        ASSERT_TRUE(wiola::testing::await_gui([&] {
            return title->text().isEmpty() && host->queue().browser().table().visible_count() == 1;
        }));
        QAction* dark = nullptr;
        for (auto* action : window.findChildren<QAction*>())
            if (action->text() == "Dark theme")
                dark = action;
        ASSERT_NE(dark, nullptr);
        QMenu choices;
        choices.addAction(dark);
        choices.popup(window.mapToGlobal(QPoint{10, 30}));
        ASSERT_TRUE(wiola::testing::await_gui([&] { return choices.isVisible(); }));
        QTest::mouseClick(&choices, Qt::LeftButton, Qt::NoModifier,
            choices.actionGeometry(dark).center());
        EXPECT_EQ(window.palette().color(QPalette::Window),
            gui::theme_colors(gui::Theme::dark).window);
        title->setFocus();
        ASSERT_TRUE(wiola::testing::await_gui([&] { return title->hasFocus(); }));
        auto layout = gui::classic_layout_document();
        layout.root.id = "visible-custom-root";
        ASSERT_TRUE(window.switch_layout(layout));
        ASSERT_TRUE(wiola::testing::await_gui([&] { return window.centralWidget()->isVisible(); }));
        ASSERT_TRUE(wiola::testing::await_gui([&] { return title->hasFocus(); }));
        EXPECT_EQ(host->queue().browser().table().visible_count(), 1);
        EXPECT_TRUE(window.centralWidget()->isVisible());
        window.close();
    }
    if (!player.shutdown()) {
        const auto saved = player.library().flush();
        FAIL() << "library save io=" << static_cast<int>(saved.io)
               << " schema=" << static_cast<int>(saved.schema)
               << " preferences saved=" << player.preferences().flush();
    }
}

TEST(DesktopJourney, PackagedPlayerCliExercisesTheActualEntryPoint)
{
    const auto player = QDir{QCoreApplication::applicationDirPath()}.filePath(
#ifdef _WIN32
        "../../bin/wiola-player.exe"
#else
        "../../bin/wiola-player"
#endif
    );
    ASSERT_TRUE(QFileInfo::exists(player));
    QByteArray output;
    EXPECT_EQ(run_child({"--help"}, player, &output), 0);
    EXPECT_TRUE(output.contains("Usage: wiola-player"));
    EXPECT_EQ(run_child({"--version"}, player, &output), 0);
    EXPECT_TRUE(output.startsWith("wiola-player "));
}

TEST(DesktopJourney, SeparateProcessesRestoreIndependentSessionLayoutThemeAndSelection)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_EQ(run_child({"--wiola-child", "create", directory.path()}), 0);
    ASSERT_EQ(run_child({"--wiola-child", "verify", directory.path()}), 0);
}

TEST(DesktopJourney, EveryAbruptReplacementBoundaryRecoversAValidOldOrNewMediaFile)
{
    using namespace wiola;
    for (int step = 0; step < 5; ++step) {
        SCOPED_TRACE("replacement process stop=" + std::to_string(step));
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        ASSERT_EQ(run_child({"--wiola-child", "crash", directory.path(), QString::number(step)}),
            91);
        const auto evidence = QJsonDocument::fromJson(
            wiola::testing::child_bytes(QDir{directory.path()}.filePath("replacement.json"))
        )
                                  .object();
        const auto source = platform::qt::QtPathAdapter::from_qt(evidence["source"].toString());
        const auto original = evidence["original"].toString().toStdString(),
                   updated = evidence["updated"].toString().toStdString();
        auto port = platform::native_file_replacement(library::fingerprint_media);
        const auto recovered = port->recover(source);
        ASSERT_EQ(recovered.error, platform::FileReplacementError::none) << recovered.detail;
        EXPECT_TRUE(recovered.recovered);
        const auto digest = library::fingerprint_media(source);
        EXPECT_TRUE(digest == original || digest == updated);
        EXPECT_FALSE(std::filesystem::exists(recovered.journal));
        if (std::filesystem::exists(recovered.backup))
            EXPECT_EQ(library::fingerprint_media(recovered.backup), original);
        codec::LocalMediaProvider provider;
        auto asset = provider.open(source);
        ASSERT_TRUE(asset);
        auto metadata = (*asset)->read_metadata();
        ASSERT_TRUE(metadata);
        EXPECT_EQ(metadata->track.title == "Process committed metadata", digest == updated);
    }
}

TEST(DesktopJourney, FutureLibrarySchemaSurvivesARealChildShutdown)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QFile future{QDir{directory.path()}.filePath("library.json")};
    ASSERT_TRUE(future.open(QIODevice::WriteOnly));
    ASSERT_GT(future.write("{\"version\":9999}"), 0);
    future.close();
    EXPECT_EQ(run_child({"--wiola-child", "future", directory.path()}), 0);
    EXPECT_EQ(wiola::testing::child_bytes(future.fileName()), "{\"version\":9999}");
}
#ifdef _WIN32
TEST(DesktopJourney, NativeWindowsSharingViolationPreservesTheUnicodeSource)
{
    using namespace wiola;
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto prepared = wiola::testing::stage_process_replacement(directory.path());
    HANDLE file = CreateFileW(prepared.source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(file, INVALID_HANDLE_VALUE);
    auto port = platform::native_file_replacement(library::fingerprint_media);
    const auto outcome = port->replace({prepared.source, prepared.temporary,
        prepared.source.parent_path() / "backup.mp3", prepared.source_fingerprint,
        prepared.output_fingerprint});
    CloseHandle(file);
    EXPECT_NE(outcome.error, platform::FileReplacementError::none);
    EXPECT_FALSE(outcome.source_modified);
    EXPECT_EQ(library::fingerprint_media(prepared.source), prepared.source_fingerprint);
    EXPECT_EQ(port->recover(prepared.source).error, platform::FileReplacementError::none);
}
#endif

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
    QApplication application{argc, argv};
    const auto arguments = QCoreApplication::arguments();
    if (arguments.value(1) == "--wiola-child") {
        try {
            if (arguments.value(2) == "crash")
                return wiola::testing::crash_child(arguments.value(3), arguments.value(4).toInt());
            return wiola::testing::session_child(arguments.value(2), arguments.value(3));
        } catch (const std::exception& error) {
            std::cerr << error.what() << std::endl;
            return 2;
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    QTemporaryDir isolated;
    if (!isolated.isValid())
        return 2;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    wiola::testing::retain_widget_screenshots();
    std::cout << "Qt platform: " << qPrintable(QGuiApplication::platformName()) << '\n';
    return RUN_ALL_TESTS();
}
