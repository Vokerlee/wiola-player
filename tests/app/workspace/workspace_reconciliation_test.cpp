/**
 * @file
 * @brief Workspace media reconciliation regression tests.
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

#include <library/fingerprint.hpp>
#include <library/location.hpp>

#include <gtest/gtest.h>

#include <fstream>

namespace {

class WorkspaceReconciliation : public testing::Test {
protected:
    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() / "wiola-reconcile";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        workspace = repository.create_workspace(root);
    }

    void TearDown() override { std::filesystem::remove_all(root); }

    void write(const std::filesystem::path& path, const std::string& content)
    {
        std::ofstream{path, std::ios::binary} << content;
    }

    wiola::library::InMemoryRepository repository;
    wiola::library::WorkspaceId workspace;
    std::filesystem::path root;
    wiola::app::WorkspaceReconciliation reconciliation{[](const auto&) { return true; }};

    wiola::app::WorkspaceReconciliationResult
    reconcile(std::span<const std::filesystem::path> paths)
    {
        const auto known = repository.tracks();
        wiola::app::WorkspaceDiscovery discovery{root, known, paths};
        std::vector<wiola::app::WorkspaceScanEntry> entries;
        for (const auto& path : paths)
            entries.push_back(discovery.resolve(path));
        return reconciliation.reconcile(repository, workspace, entries);
    }
};

TEST_F(WorkspaceReconciliation, RelocatesByFingerprintWithoutChangingTagsOrPlaylistReferences)
{
    const std::filesystem::path old_path = root / "old.flac";
    const std::filesystem::path new_path = root / "new.flac";
    write(old_path, "same content");

    wiola::library::TrackRecord track;
    track.id = wiola::library::media_id_for(old_path);
    track.locations = {old_path};
    track.fingerprint = *wiola::library::fingerprint_media(old_path);
    track.overrides.title = "User title";
    ASSERT_TRUE(repository.put_track(track));

    const auto tag = repository.add_tag(track.id, "genre", "Jazz");
    ASSERT_TRUE(tag.valid());
    const auto playlist = repository.create_playlist("Saved");
    ASSERT_TRUE(repository.append_track(playlist, track.id));
    std::filesystem::rename(old_path, new_path);

    const std::filesystem::path paths[]{new_path};
    const auto result = reconcile(paths);

    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.relocated, 1);
    ASSERT_NE(repository.track(track.id), nullptr);
    EXPECT_EQ(repository.track(track.id)->display_title(), "User title");
    EXPECT_EQ(repository.track(track.id)->tags.size(), 1);
    EXPECT_EQ(repository.track(track.id)->locations.front(), new_path);
    EXPECT_FALSE(repository.track(track.id)->missing);
    EXPECT_EQ(repository.playlist(playlist)->entries.front().media, track.id);
    EXPECT_EQ(repository.track(wiola::library::media_id_for(new_path)), nullptr);
}

TEST_F(WorkspaceReconciliation, HandlesEditedMissingRestoredAndNewFiles)
{
    const std::filesystem::path path = root / "track.wav";
    write(path, "before");

    wiola::library::TrackRecord track;
    track.id = wiola::library::media_id_for(path);
    track.locations = {path};
    track.fingerprint = *wiola::library::fingerprint_media(path);
    track.overrides.title = "Keep me";
    ASSERT_TRUE(repository.put_track(track));

    write(path, "after");
    const std::filesystem::path paths[]{path};
    auto result = reconcile(paths);
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.edited, 1);
    EXPECT_EQ(repository.track(track.id)->content_revision, 1);
    EXPECT_EQ(repository.track(track.id)->display_title(), "Keep me");

    result = reconcile({});
    EXPECT_EQ(result.missing, 1);
    EXPECT_TRUE(repository.track(track.id)->missing);

    result = reconcile(paths);
    EXPECT_EQ(result.restored, 1);
    EXPECT_FALSE(repository.track(track.id)->missing);

    const std::filesystem::path learned = root / "learned.ogg";
    write(learned, "new");
    const std::filesystem::path both[]{path, learned};
    result = reconcile(both);

    EXPECT_EQ(result.learned, 1);
    ASSERT_NE(repository.track(wiola::library::media_id_for(learned)), nullptr);
    EXPECT_FALSE(repository.track(wiola::library::media_id_for(learned))->missing);
}

TEST_F(WorkspaceReconciliation, RejectsUnknownWorkspacesAndCountsOutsidePaths)
{
    const std::filesystem::path outside = root.parent_path() / "outside.wav";
    write(outside, "outside");
    const std::filesystem::path paths[]{outside};

    const auto result = reconcile(paths);
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.outside, 1);
    EXPECT_TRUE(repository.tracks().empty());

    const auto missing =
        reconciliation.reconcile(repository, wiola::library::WorkspaceId{"missing"}, {});
    EXPECT_FALSE(missing.succeeded);

    std::filesystem::remove(outside);
}

TEST_F(WorkspaceReconciliation, AppliesWorkerFactsWithoutReopeningTheFile)
{
    const auto path = root / "cached.flac";
    write(path, "cached discovery");
    const std::vector<std::filesystem::path> paths{path};
    wiola::app::WorkspaceDiscovery discovery{root, {}, paths};
    const std::vector entries{discovery.resolve(path)};
    ASSERT_TRUE(entries.front().fingerprint.has_value());
    std::filesystem::remove(path);
    const auto result = reconciliation.reconcile(repository, workspace, entries);
    ASSERT_TRUE(result.succeeded);
    ASSERT_NE(repository.track(entries.front().media), nullptr);
    EXPECT_EQ(repository.track(entries.front().media)->fingerprint, *entries.front().fingerprint);
}

TEST_F(WorkspaceReconciliation, FailedPersistenceRollsBackLearnedRowsAndMissingFlags)
{
    const auto old_path = root / "old.flac";
    const auto new_path = root / "new.flac";
    write(old_path, "old");
    write(new_path, "new");
    wiola::library::TrackRecord old;
    old.id = wiola::library::media_id_for(old_path);
    old.locations = {old_path};
    old.fingerprint = *wiola::library::fingerprint_media(old_path);
    ASSERT_TRUE(repository.put_track(old));
    const auto before = repository.tracks();
    const std::vector<std::filesystem::path> paths{new_path};
    wiola::app::WorkspaceDiscovery discovery{root, before, paths};
    const std::vector entries{discovery.resolve(new_path)};
    int notifications = 0;
    auto subscription = repository.observe([&](const auto&) { ++notifications; });
    wiola::app::WorkspaceReconciliation failing{[](const auto&) { return false; }};
    EXPECT_FALSE(failing.reconcile(repository, workspace, entries).succeeded);
    EXPECT_EQ(repository.tracks(), before);
    EXPECT_EQ(notifications, 0);
}

TEST_F(WorkspaceReconciliation, StaleDiscoveryCannotOverwriteNewerContentState)
{
    const auto path = root / "changed.flac";
    write(path, "before");
    wiola::library::TrackRecord record;
    record.id = wiola::library::media_id_for(path);
    record.locations = {path};
    record.fingerprint = *wiola::library::fingerprint_media(path);
    ASSERT_TRUE(repository.put_track(record));
    const auto known = repository.tracks();
    const std::vector<std::filesystem::path> paths{path};
    wiola::app::WorkspaceDiscovery discovery{root, known, paths};
    const std::vector entries{discovery.resolve(path)};
    ++record.content_revision;
    record.fingerprint = "newer-content";
    ASSERT_TRUE(repository.put_track(record));
    const auto result = reconciliation.reconcile(repository, workspace, entries);
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.failed, 1);
    EXPECT_EQ(repository.track(record.id)->fingerprint, "newer-content");
    EXPECT_EQ(repository.track(record.id)->content_revision, record.content_revision);
}

TEST_F(WorkspaceReconciliation, ANewerOwnerRecordAtANewPathIsNotOverwrittenByDiscovery)
{
    const auto path = root / "new-path.flac";
    write(path, "scan");
    const std::vector<std::filesystem::path> paths{path};
    wiola::app::WorkspaceDiscovery discovery{root, {}, paths};
    const std::vector entries{discovery.resolve(path)};
    wiola::library::TrackRecord newer;
    newer.id = entries.front().media;
    newer.locations = {path};
    newer.content_revision = 3;
    newer.fingerprint = "newer";
    ASSERT_TRUE(repository.put_track(newer));
    const auto result = reconciliation.reconcile(repository, workspace, entries);
    EXPECT_EQ(result.failed, 1);
    EXPECT_EQ(repository.track(newer.id)->fingerprint, "newer");
}

TEST_F(WorkspaceReconciliation, AmbiguousFingerprintsDoNotMergeDistinctMediaIdentities)
{
    const auto path = root / "new.flac";
    write(path, "identical");
    wiola::library::TrackRecord first, second;
    first.id = {"first"};
    first.locations = {root / "first.flac"};
    first.fingerprint = *wiola::library::fingerprint_media(path);
    second = first;
    second.id = {"second"};
    second.locations = {root / "second.flac"};
    ASSERT_TRUE(repository.put_track(first));
    ASSERT_TRUE(repository.put_track(second));
    const auto known = repository.tracks();
    const std::vector<std::filesystem::path> paths{path};
    wiola::app::WorkspaceDiscovery discovery{root, known, paths};
    const auto discovered = discovery.resolve(path);
    EXPECT_FALSE(discovered.known());
    EXPECT_FALSE(discovered.relocated());
    EXPECT_NE(discovered.media, first.id);
    EXPECT_NE(discovered.media, second.id);
}

} // namespace
