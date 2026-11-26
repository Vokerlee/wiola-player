/**
 * @file
 * @brief Debounced workspace filesystem watcher regression tests.
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

#include <qt/application/workspace/workspace_watcher.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <gtest/gtest.h>

#include <QTemporaryDir>
#include <fstream>
#include <set>
#include <wiola/qt/paths.hpp>

namespace {

bool pump_until(std::function<bool()> predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }

    return predicate();
}

TEST(WorkspaceWatcher, CoalescesFilesystemBurstsAndStopsAfterUnwatch)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wiola-watch";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    wiola::app::WorkspaceWatcher watcher;
    const wiola::library::WorkspaceRecord workspace{.id = wiola::library::WorkspaceId{"workspace"},
        .root = root,
        .recursive = true};
    ASSERT_TRUE(watcher.watch(workspace));

    int notifications{0};
    watcher.on_changed([&](wiola::library::WorkspaceId id) {
        EXPECT_EQ(id, workspace.id);
        ++notifications;
    });

    std::ofstream{root / "first.wav"} << "first";
    ASSERT_TRUE(pump_until([&] { return notifications == 1; }));

    for (int index = 0; index < 5; ++index)
        std::ofstream{root / ("burst-" + std::to_string(index) + ".wav")} << "audio";

    ASSERT_TRUE(pump_until([&] { return notifications == 2; }));
    QThread::msleep(250);
    QCoreApplication::processEvents();
    EXPECT_EQ(notifications, 2);

    watcher.unwatch(workspace.id);
    std::ofstream{root / "ignored.wav"} << "ignored";
    QThread::msleep(250);
    QCoreApplication::processEvents();
    EXPECT_EQ(notifications, 2);

    EXPECT_FALSE(watcher.watch(
        {.id = wiola::library::WorkspaceId{"missing"}, .root = root / "missing", .recursive = true}
    ));

    std::filesystem::remove_all(root);
}

class FakeWatchBackend final : public wiola::app::WorkspaceWatchBackend {
public:
    bool add(const std::filesystem::path& path) override
    {
        ++adds[path];
        if (refused.contains(path))
            return false;
        installed.insert(path);
        return true;
    }

    void remove(const std::filesystem::path& path) override
    {
        removed.insert(path);
        installed.erase(path);
    }

    bool contains(const std::filesystem::path& path) const override
    {
        return installed.contains(path);
    }

    void on_changed(Observer observer) override { observer_ = std::move(observer); }

    void change(const std::filesystem::path& path)
    {
        if (observer_)
            observer_(path);
    }

    std::set<std::filesystem::path> installed, refused, removed;
    std::map<std::filesystem::path, int> adds;
    Observer observer_;
};

TEST(WorkspaceWatcher,
    OverlappingSubscriptionsSurviveClosingOneWorkspaceAndNotifyEveryInterestedRoot)
{
    QTemporaryDir directory;
    const auto root = wiola::platform::qt::QtPathAdapter::from_qt(directory.path());
    const auto sub = root / "sub";
    std::filesystem::create_directories(sub);
    auto backend = std::make_unique<FakeWatchBackend>();
    auto* native = backend.get();
    wiola::app::WorkspaceWatcher watcher{std::move(backend),
        {.debounce = std::chrono::milliseconds{0}}};
    const wiola::library::WorkspaceId outer{"outer"}, inner{"inner"};
    ASSERT_TRUE(watcher.watch({.id = outer, .root = root, .recursive = true}));
    watcher.scan_started(outer);
    watcher.directories_discovered(outer, {root, sub}, true, true);
    ASSERT_TRUE(watcher.watch({.id = inner, .root = sub / ".", .recursive = false}));
    EXPECT_EQ(native->adds[sub], 1);
    std::vector<wiola::library::WorkspaceId> events;
    watcher.on_changed([&](auto id) { events.push_back(id); });
    native->change(sub);
    native->change(sub);
    ASSERT_TRUE(pump_until([&] { return events.size() == 2; }));
    EXPECT_EQ(std::set<wiola::library::WorkspaceId>(events.begin(), events.end()),
        (std::set<wiola::library::WorkspaceId>{outer, inner}));
    watcher.unwatch(outer);
    EXPECT_TRUE(native->contains(sub));
    EXPECT_FALSE(native->removed.contains(sub));
    events.clear();
    native->change(sub);
    ASSERT_TRUE(pump_until([&] { return events.size() == 1; }));
    EXPECT_EQ(events.front(), inner);
    watcher.stop();
    native->change(sub);
    QCoreApplication::processEvents();
    EXPECT_EQ(events.size(), 1);
}

TEST(WorkspaceWatcher, WatchRefusalAndNativeLimitAreVisibleAndRetryAfterCapacityIsReleased)
{
    QTemporaryDir directory;
    const auto root = wiola::platform::qt::QtPathAdapter::from_qt(directory.path());
    const auto first = root / "first", second = root / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    auto backend = std::make_unique<FakeWatchBackend>();
    auto* native = backend.get();
    native->refused.insert(first);
    wiola::app::WorkspaceWatcher watcher{
        std::move(backend), {.debounce = std::chrono::milliseconds{0}, .max_paths = 2}
    };
    std::vector<wiola::app::WorkspaceWatchCoverage> statuses;
    watcher.on_status([&](auto status) { statuses.push_back(status); });
    const wiola::library::WorkspaceId id{"workspace"};
    ASSERT_TRUE(watcher.watch({.id = id, .root = root, .recursive = true}));
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root, first}, true, true);
    ASSERT_TRUE(watcher.coverage(id));
    EXPECT_TRUE(watcher.coverage(id)->partial);
    EXPECT_EQ(watcher.coverage(id)->refused_paths, 1);
    native->refused.clear();
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root, first}, true, true);
    EXPECT_FALSE(watcher.coverage(id)->partial);
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root, first, second}, true, true);
    EXPECT_TRUE(watcher.coverage(id)->partial);
    // The authoritative next scan retires first before retrying second at the native limit.
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root, second}, true, true);
    EXPECT_FALSE(watcher.coverage(id)->partial);
    EXPECT_TRUE(native->contains(second));
    EXPECT_FALSE(native->contains(first));
    ASSERT_FALSE(statuses.empty());
    EXPECT_FALSE(statuses.back().partial);
}

TEST(WorkspaceWatcher, MissingRootUsesAnAncestorAndRecoversWhenTheRootReappears)
{
    QTemporaryDir directory;
    const auto parent = wiola::platform::qt::QtPathAdapter::from_qt(directory.path());
    const auto root = parent / "music";
    auto backend = std::make_unique<FakeWatchBackend>();
    auto* native = backend.get();
    native->refused.insert(root);
    wiola::app::WorkspaceWatcher watcher{
        std::move(backend), {.debounce = std::chrono::milliseconds{0}, .max_paths = 1}
    };
    const wiola::library::WorkspaceId id{"missing"};
    EXPECT_FALSE(watcher.watch({.id = id, .root = root, .recursive = true}));
    ASSERT_TRUE(watcher.coverage(id));
    EXPECT_TRUE(watcher.coverage(id)->root_missing);
    EXPECT_TRUE(native->contains(parent));
    std::filesystem::create_directories(root);
    native->refused.clear();
    int events = 0;
    watcher.on_changed([&](auto current) {
        EXPECT_EQ(current, id);
        ++events;
    });
    native->change(parent);
    ASSERT_TRUE(pump_until([&] { return events == 1; }));
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root}, true, true);
    EXPECT_FALSE(watcher.coverage(id)->partial);
    EXPECT_TRUE(native->contains(root));
    EXPECT_FALSE(native->contains(parent));
}

TEST(WorkspaceWatcher, NewSubdirectoriesAreSubscribedOnlyFromWorkerDiscoveryFacts)
{
    QTemporaryDir directory;
    const auto root = wiola::platform::qt::QtPathAdapter::from_qt(directory.path());
    auto backend = std::make_unique<FakeWatchBackend>();
    auto* native = backend.get();
    wiola::app::WorkspaceWatcher watcher{std::move(backend),
        {.debounce = std::chrono::milliseconds{0}}};
    const wiola::library::WorkspaceId id{"recursive"};
    ASSERT_TRUE(watcher.watch({.id = id, .root = root, .recursive = true}));
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root}, true, true);
    const auto child = root / "new", nested = child / "nested";
    std::filesystem::create_directories(nested);
    int events = 0;
    watcher.on_changed([&](auto current) {
        EXPECT_EQ(current, id);
        ++events;
    });
    native->change(root);
    ASSERT_TRUE(pump_until([&] { return events == 1; }));
    EXPECT_FALSE(native->contains(child));
    EXPECT_FALSE(native->contains(nested));
    watcher.scan_started(id);
    watcher.directories_discovered(id, {root, child, nested}, true, true);
    EXPECT_TRUE(native->contains(child));
    EXPECT_TRUE(native->contains(nested));
    EXPECT_FALSE(watcher.coverage(id)->partial);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
