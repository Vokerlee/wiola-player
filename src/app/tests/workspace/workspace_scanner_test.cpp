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

#include <app/workspace/workspace_reconciliation.hpp>
#include <app/workspace/workspace_scanner.hpp>
#include <library/in_memory_repository.hpp>

#include <fixtures/temporary_directory.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <library/fingerprint.hpp>
#include <mutex>

namespace {

struct ScanCapture {
    void accept(wiola::app::WorkspaceScanBatch batch)
    {
        std::lock_guard lock{mutex};
        batches.push_back(batch);
        for (const auto& entry : batch.entries)
            tracks.push_back(entry.location);

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

TEST(WorkspaceScanner, PartialQualificationKeepsPresentKnownFilesButDoesNotImportUnknownFormats)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-partial-qualification";
    std::filesystem::remove_all(root);
    file(root / "known.flac");
    file(root / "unknown.flac");
    file(root / "new.wav");
    wiola::library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root, false);
    wiola::library::TrackRecord present, removed;
    present.id = {"present"};
    present.locations = {root / "known.flac"};
    removed.id = {"removed"};
    removed.locations = {root / "removed.flac"};
    ASSERT_TRUE(repository.put_track(present));
    ASSERT_TRUE(repository.put_track(removed));
    ScanCapture capture;
    wiola::app::WorkspaceScanner scanner;
    scanner.start(
        *repository.workspace(workspace), {".wav"},
        [&](auto batch) { capture.accept(std::move(batch)); }, repository.tracks()
    );
    ASSERT_TRUE(capture.wait());
    scanner.stop();
    ASSERT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::complete);
    ASSERT_EQ(capture.tracks.size(), 2);
    EXPECT_NE(std::ranges::find(capture.tracks, root / "known.flac"), capture.tracks.end());
    EXPECT_EQ(std::ranges::find(capture.tracks, root / "unknown.flac"), capture.tracks.end());
    std::vector<wiola::app::WorkspaceScanEntry> entries;
    for (const auto& batch : capture.batches)
        entries.insert(entries.end(), batch.entries.begin(), batch.entries.end());
    wiola::app::WorkspaceReconciliation reconcile{[](const auto&) { return true; }};
    ASSERT_TRUE(reconcile.reconcile(repository, workspace, entries).succeeded);
    EXPECT_FALSE(repository.track(present.id)->missing);
    EXPECT_TRUE(repository.track(removed.id)->missing);
    EXPECT_EQ(repository.tracks().size(), 3);
    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, StreamsSupportedFilesInBoundedWorkerBatches)
{
    wiola::testing::TemporaryDirectory temporary;
    const std::filesystem::path root = temporary.path() / "wiola-scan-recursive";
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
    std::ranges::sort(capture.tracks);
    EXPECT_EQ(capture.tracks,
        (std::vector<std::filesystem::path>{root / "a.MP3", root / "nested" / "b.ogg",
            root / "z.wav"}));
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::complete);
    EXPECT_EQ(capture.batches.back().generation, generation);

    for (const auto& batch : capture.batches)
        EXPECT_LE(batch.entries.size(), 64);

    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, HonorsNonRecursiveEnumeration)
{
    wiola::testing::TemporaryDirectory temporary;
    const std::filesystem::path root = temporary.path() / "wiola-scan-flat";
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
    wiola::testing::TemporaryDirectory temporary;
    const std::filesystem::path root = temporary.path() / "wiola-scan-cancel";
    std::filesystem::remove_all(root);
    for (int index = 0; index < 70; ++index)
        file(root / (std::to_string(index) + ".wav"));

    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    scanner.start(
        {.id = wiola::library::WorkspaceId{"workspace-cancel"}, .root = root, .recursive = true},
        {".wav"}, [&capture, &scanner](wiola::app::WorkspaceScanBatch batch) {
            if (!batch.entries.empty())
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

TEST(WorkspaceScanner, ResolvesAKnownRelocationOnTheWorkerBeforePublishingRows)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-scan-relocation";
    std::filesystem::remove_all(root);
    const auto old_path = root / "old.wav";
    const auto new_path = root / "new.wav";
    file(old_path);
    wiola::library::TrackRecord record;
    record.id = {"canonical-media"};
    record.locations = {old_path};
    record.fingerprint = *wiola::library::fingerprint_media(old_path);
    std::filesystem::rename(old_path, new_path);
    wiola::app::WorkspaceScanner scanner;
    ScanCapture capture;
    scanner.start({.id = wiola::library::WorkspaceId{"workspace"}, .root = root}, {".wav"},
        [&](auto batch) { capture.accept(std::move(batch)); }, {record});
    ASSERT_TRUE(capture.wait());
    ASSERT_FALSE(capture.batches.empty());
    ASSERT_EQ(capture.batches.front().entries.size(), 1);
    const auto& discovered = capture.batches.front().entries.front();
    EXPECT_EQ(discovered.media, record.id);
    EXPECT_EQ(discovered.location, new_path);
    EXPECT_TRUE(discovered.relocated());
    EXPECT_EQ(discovered.fingerprint, record.fingerprint);
    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, PublishesRowsBeforeEnumerationCompletesAndCancelsAtAGatedBoundary)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-stream-gated";
    std::filesystem::remove_all(root);
    for (int i = 0; i < 8; ++i)
        file(root / (std::to_string(i) + ".wav"));
    std::mutex mutex;
    std::condition_variable changed;
    bool gated = false;
    wiola::app::WorkspaceScanPolicy policy;
    policy.batch_size = 2;
    policy.after_entry = [&](const auto&, std::size_t visited, std::stop_token stop) {
        if (visited != 3)
            return;
        std::unique_lock lock{mutex};
        gated = true;
        changed.notify_all();
        std::stop_callback wake{stop, [&] { changed.notify_all(); }};
        changed.wait(lock, [&] { return stop.stop_requested(); });
    };
    ScanCapture capture;
    wiola::app::WorkspaceScanner scanner{policy};
    scanner.start({.id = {"stream"}, .root = root}, {".wav"},
        [&](auto batch) { capture.accept(std::move(batch)); });
    {
        std::unique_lock lock{mutex};
        ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds{5}, [&] { return gated; }));
    }
    {
        std::lock_guard lock{capture.mutex};
        ASSERT_EQ(capture.tracks.size(), 2);
        EXPECT_FALSE(capture.finished);
        EXPECT_EQ(capture.batches.front().status, wiola::app::WorkspaceScanStatus::running);
    }
    scanner.cancel();
    ASSERT_TRUE(capture.wait());
    scanner.stop();
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::cancelled);
    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, StopsAtTrackLimitWithoutClaimingAnAuthoritativeCompletion)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-stream-limit";
    std::filesystem::remove_all(root);
    for (int i = 0; i < 4; ++i)
        file(root / (std::to_string(i) + ".wav"));
    wiola::app::WorkspaceScanPolicy policy;
    policy.max_tracks = 2;
    policy.batch_size = 1;
    ScanCapture capture;
    wiola::app::WorkspaceScanner scanner{policy};
    scanner.start({.id = {"limited"}, .root = root}, {".wav"},
        [&](auto batch) { capture.accept(std::move(batch)); });
    ASSERT_TRUE(capture.wait());
    scanner.stop();
    EXPECT_EQ(capture.tracks.size(), 2);
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::failed);
    EXPECT_EQ(capture.batches.back().error, wiola::app::WorkspaceScanError::limit_exceeded);
    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, IdenticalNewCopyDoesNotRelocateAnExistingKnownLocation)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-stream-copies";
    std::filesystem::remove_all(root);
    file(root / "existing.wav");
    file(root / "copy.wav");
    wiola::library::TrackRecord known;
    known.id = {"canonical"};
    known.locations = {root / "existing.wav"};
    known.fingerprint = *wiola::library::fingerprint_media(known.locations.front());
    ScanCapture capture;
    wiola::app::WorkspaceScanner scanner;
    scanner.start({.id = {"copies"}, .root = root}, {".wav"},
        [&](auto batch) { capture.accept(std::move(batch)); }, {known});
    ASSERT_TRUE(capture.wait());
    scanner.stop();
    bool found = false;
    for (const auto& batch : capture.batches)
        for (const auto& entry : batch.entries)
            if (entry.location == root / "copy.wav") {
                found = true;
                EXPECT_NE(entry.media, known.id);
                EXPECT_FALSE(entry.relocated());
            }
    EXPECT_TRUE(found);
    std::filesystem::remove_all(root);
}

TEST(WorkspaceScanner, EmptyQualificationNeverClaimsAllKnownMediaAreMissing)
{
    wiola::testing::TemporaryDirectory temporary;
    const auto root = temporary.path() / "wiola-empty-qualification";
    std::filesystem::remove_all(root);
    file(root / "present.wav");
    ScanCapture capture;
    wiola::app::WorkspaceScanner scanner;
    scanner.start({.id = {"workspace"}, .root = root}, {},
        [&](auto batch) { capture.accept(std::move(batch)); });
    ASSERT_TRUE(capture.wait());
    scanner.stop();
    EXPECT_EQ(capture.batches.back().status, wiola::app::WorkspaceScanStatus::failed);
    EXPECT_EQ(capture.batches.back().error, wiola::app::WorkspaceScanError::unsupported_formats);
    std::filesystem::remove_all(root);
}

} // namespace
