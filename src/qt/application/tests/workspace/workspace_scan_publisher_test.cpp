/**
 * @file
 * @brief Generation-aware workspace publication regression tests.
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

#include <fixtures/temporary_directory.hpp>
#include <qt/application/workspace/workspace_scan_publisher.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>

namespace {

void file(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path} << "audio";
}

bool pump_until(std::function<bool()> predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    return predicate();
}

TEST(WorkspaceScanPublisher, PublishesIncrementalBatchesOnTheGuiThread)
{
    const wiola::testing::TemporaryDirectory directory;
    const auto& root = directory.path();
    wiola::app::WorkspaceScanScheduling policy;
    policy.scan.batch_size = 3;
    const auto track_count = 2 * policy.scan.batch_size + 1;
    for (std::size_t index = 0; index < track_count; ++index)
        file(root / (std::to_string(index) + ".wav"));

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    std::vector<std::filesystem::path> tracks;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) {
            EXPECT_EQ(QThread::currentThread(), QCoreApplication::instance()->thread());
            for (const auto& entry : batch.entries)
                tracks.push_back(entry.location);
            batches.push_back(std::move(batch));
        },
        policy};
    const auto generation = publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = root, .recursive = true}, {".wav"}
    );

    ASSERT_TRUE(pump_until([&] {
        return !batches.empty() &&
            batches.back().status != wiola::app::WorkspaceScanStatus::running;
    }));
    EXPECT_EQ(batches.back().status, wiola::app::WorkspaceScanStatus::complete);
    EXPECT_EQ(tracks.size(), track_count);
    EXPECT_EQ(std::set<std::filesystem::path>(tracks.begin(), tracks.end()).size(), track_count);
    EXPECT_EQ(batches.back().generation, generation);

    for (const auto& batch : batches)
        EXPECT_LE(batch.entries.size(), policy.scan.batch_size);
}

TEST(WorkspaceScanPublisher, SupersedingOneWorkspaceDropsOnlyItsOlderQueuedGeneration)
{
    const wiola::testing::TemporaryDirectory directory;
    const auto& base = directory.path();
    const std::filesystem::path old_root = base / "old";
    const std::filesystem::path new_root = base / "new";
    file(old_root / "old.wav");
    file(new_root / "new.wav");

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) { batches.push_back(std::move(batch)); }};

    const auto old_generation = publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = old_root, .recursive = true},
        {".wav"}
    );
    const auto current = publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = new_root, .recursive = true},
        {".wav"}
    );

    EXPECT_NE(old_generation, current);
    ASSERT_TRUE(pump_until([&] {
        return !batches.empty() &&
            batches.back().status != wiola::app::WorkspaceScanStatus::running;
    }));

    for (const auto& batch : batches) {
        EXPECT_EQ(batch.workspace, wiola::library::WorkspaceId{"workspace"});
        EXPECT_EQ(batch.generation, current);
    }
}

TEST(WorkspaceScanPublisher, CancellationInvalidatesAlreadyQueuedBatches)
{
    const wiola::testing::TemporaryDirectory directory;
    const auto& root = directory.path();
    file(root / "track.wav");

    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](wiola::app::WorkspaceScanBatch batch) { batches.push_back(std::move(batch)); }};

    publisher.start(
        {.id = wiola::library::WorkspaceId{"workspace"}, .root = root, .recursive = true}, {".wav"}
    );
    publisher.cancel();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    EXPECT_TRUE(batches.empty());
}

struct ScanGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};

    void wait(std::stop_token stop)
    {
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        std::stop_callback wake{stop, [this] { changed.notify_all(); }};
        changed.wait(lock, [&] { return stop.stop_requested(); });
    }

    bool started()
    {
        std::lock_guard lock{mutex};
        return entered;
    }
};

TEST(WorkspaceScanPublisher, CancellingOneWorkspaceLeavesAnotherConcurrentScanIntact)
{
    const wiola::testing::TemporaryDirectory directory;
    const auto& root = directory.path();
    file(root / "first" / "one.wav");
    file(root / "second" / "two.wav");
    ScanGate gate;
    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanScheduling policy;
    policy.scan.after_entry = [&](const auto& path, std::size_t, std::stop_token stop) {
        if (path.parent_path() == root / "first")
            gate.wait(stop);
    };
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](auto batch) { batches.push_back(std::move(batch)); }, policy};
    const auto first = publisher.start({.id = {"first"}, .root = root / "first"}, {".wav"});
    const auto second = publisher.start({.id = {"second"}, .root = root / "second"}, {".wav"});
    ASSERT_TRUE(pump_until([&] {
        return gate.started() && std::ranges::any_of(batches, [&](const auto& batch) {
            return batch.workspace == wiola::library::WorkspaceId{"second"} &&
                batch.status == wiola::app::WorkspaceScanStatus::complete;
        });
    }));
    publisher.cancel(wiola::library::WorkspaceId{"first"});
    publisher.stop();
    EXPECT_TRUE(std::ranges::none_of(batches, [&](const auto& batch) {
        return batch.generation == first &&
            batch.status == wiola::app::WorkspaceScanStatus::complete;
    }));
    EXPECT_TRUE(std::ranges::any_of(batches, [&](const auto& batch) {
        return batch.generation == second &&
            batch.status == wiola::app::WorkspaceScanStatus::complete && batch.entries.size() == 1;
    }));
}

TEST(WorkspaceScanPublisher, FairBoundedQueueRunsWaitingWorkspaceAndReportsOverflow)
{
    const wiola::testing::TemporaryDirectory directory;
    const auto& root = directory.path();
    for (const auto* name : {"first", "second", "third"})
        file(root / name / "track.wav");
    ScanGate gate;
    std::vector<wiola::app::WorkspaceScanBatch> batches;
    wiola::app::WorkspaceScanScheduling policy;
    policy.max_active = 1;
    policy.max_pending = 1;
    policy.scan.after_entry = [&](const auto& path, std::size_t, std::stop_token stop) {
        if (path.parent_path() == root / "first")
            gate.wait(stop);
    };
    wiola::app::WorkspaceScanPublisher publisher{*QCoreApplication::instance(),
        [&](auto batch) { batches.push_back(std::move(batch)); }, policy};
    publisher.start({.id = {"first"}, .root = root / "first"}, {".wav"});
    ASSERT_TRUE(pump_until([&] { return gate.started(); }));
    const auto waiting = publisher.start({.id = {"second"}, .root = root / "second"}, {".wav"});
    const auto rejected = publisher.start({.id = {"third"}, .root = root / "third"}, {".wav"});
    ASSERT_TRUE(pump_until([&] {
        return std::ranges::any_of(batches, [&](const auto& batch) {
            return batch.generation == rejected &&
                batch.error == wiola::app::WorkspaceScanError::rejected;
        });
    }));
    publisher.cancel(wiola::library::WorkspaceId{"first"});
    ASSERT_TRUE(pump_until([&] {
        return std::ranges::any_of(batches, [&](const auto& batch) {
            return batch.generation == waiting &&
                batch.status == wiola::app::WorkspaceScanStatus::complete;
        });
    }));
    publisher.stop();
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
