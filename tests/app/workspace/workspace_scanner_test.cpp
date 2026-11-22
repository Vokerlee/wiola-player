/**
 * @file
 * @brief Worker-thread workspace enumeration regression tests.
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

#include <app/workspace/workspace_scanner.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <mutex>

namespace {

struct ScanCapture {
    void accept(wiola::app::WorkspaceScanBatch batch)
    {
        std::lock_guard lock{mutex};
        batches.push_back(batch);
        tracks.insert(tracks.end(), batch.tracks.begin(), batch.tracks.end());

        if (batch.status != wiola::app::WorkspaceScanStatus::running)
            finished = true;

        changed.notify_all();
    }

    bool wait()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds{5}, [this] { return finished; });
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::vector<wiola::app::WorkspaceScanBatch> batches;
    std::vector<std::filesystem::path> tracks;
    bool finished{false};
};

void file(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path} << "audio";
}

TEST(WorkspaceScanner, EnumeratesSupportedFilesInDeterministicWorkerBatches)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "wiola-scan-recursive";
    std::filesystem::remove_all(root);
    file(root / "z.wav");
    file(root / "a.MP3");
    file(root / "skip.txt");
    file(root / "nested" / "b.ogg");

    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    const std::thread::id caller = std::this_thread::get_id();
    const auto generation = scanner.start(
        {.id = wiola::library::WorkspaceId{"workspace-1"}, .root = root, .recursive = true},
        {".wav", ".mp3", ".ogg"}, [&capture, caller](wiola::app::WorkspaceScanBatch batch) {
            EXPECT_NE(std::this_thread::get_id(), caller);
            capture.accept(std::move(batch));
        }
    );

    ASSERT_TRUE(capture.wait());
    ASSERT_EQ(capture.tracks.size(), 3);
    EXPECT_TRUE(std::ranges::is_sorted(capture.tracks));
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::complete);
    EXPECT_EQ(capture.batches.back().generation, generation);

    for (const auto& batch : capture.batches)
        EXPECT_LE(batch.tracks.size(), 64);

    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, HonorsNonRecursiveEnumeration)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wiola-scan-flat";
    std::filesystem::remove_all(root);
    file(root / "one.wav");
    file(root / "nested" / "two.wav");

    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    scanner.start(
        {.id = wiola::library::WorkspaceId{"workspace-flat"}, .root = root, .recursive = false},
        {".wav"},
        [&capture](wiola::app::WorkspaceScanBatch batch) { capture.accept(std::move(batch)); }
    );

    ASSERT_TRUE(capture.wait());
    ASSERT_EQ(capture.tracks.size(), 1);
    EXPECT_EQ(capture.tracks.front(), root / "one.wav");

    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, CancelsCooperativelyAfterTheFirstBatch)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "wiola-scan-cancel";
    std::filesystem::remove_all(root);
    for (int index = 0; index < 70; ++index)
        file(root / (std::to_string(index) + ".wav"));

    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    scanner.start(
        {.id = wiola::library::WorkspaceId{"workspace-cancel"}, .root = root, .recursive = true},
        {".wav"}, [&capture, &scanner](wiola::app::WorkspaceScanBatch batch) {
            if (!batch.tracks.empty())
                scanner.cancel();
            capture.accept(std::move(batch));
        }
    );

    ASSERT_TRUE(capture.wait());
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::cancelled);
    EXPECT_LE(capture.tracks.size(), 64);

    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, ReportsAnInvalidRoot)
{
    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    scanner.start({.id = wiola::library::WorkspaceId{"workspace-invalid"},
                      .root = "/definitely/missing/wiola-workspace",
                      .recursive = true},
        {".wav"},
        [&capture](wiola::app::WorkspaceScanBatch batch) { capture.accept(std::move(batch)); });

    ASSERT_TRUE(capture.wait());
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::failed);
    EXPECT_TRUE(capture.tracks.empty());
}

} // namespace
