/**
 * @file
 * @brief Portable workspace discovery and reconciliation contracts.
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

#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <vector>
#include <wiola/app/library/local_identity.hpp>
#include <wiola/app/workspace/workspace_reconciliation.hpp>

namespace {

using namespace wiola;

class ObservedFiles final : public app::WorkspaceFiles {
public:
    std::optional<std::string> fingerprint(const std::filesystem::path& path,
        std::stop_token stopped) override
    {
        stopped_seen = stopped.stop_requested();
        const auto found = digests.find(path);
        return found == digests.end() ? std::optional<std::string>{} : found->second;
    }

    Presence presence(const std::filesystem::path& path) override
    {
        ++presence_reads;
        const auto found = present.find(path);
        return found == present.end() ? Presence::absent : found->second;
    }

    std::map<std::filesystem::path, std::string> digests;
    std::map<std::filesystem::path, Presence> present;
    int presence_reads{};
    bool stopped_seen{};
};

class WorkspacePolicy : public ::testing::Test {
protected:
    library::TrackRecord record(std::string id = "stable") const
    {
        library::TrackRecord value;
        value.id = {std::move(id)};
        value.locations = {root / (value.id.value + ".flac")};
        value.fingerprint = "same-content";
        value.content_revision = 7;
        return value;
    }

    ObservedFiles files;
    const std::filesystem::path root{std::filesystem::current_path() / "uncreated-workspace"};
    const std::filesystem::path output{root / "new.flac"};
};

TEST_F(WorkspacePolicy, UnknownLocationOwnsItsIdentityAndObservedFingerprint)
{
    files.digests[output] = "new-content";
    app::WorkspaceDiscovery discovery{files, root, {}};
    const auto found = discovery.resolve(output);
    files.digests.clear();
    EXPECT_EQ(found.media, app::media_id_for(output));
    EXPECT_EQ(found.location, output);
    EXPECT_EQ(found.fingerprint, "new-content");
    EXPECT_FALSE(found.known());
    EXPECT_FALSE(found.unreadable);
}

TEST_F(WorkspacePolicy, KnownLocationPreservesCatalogIdentityAndCapturedRevision)
{
    const std::vector known{record()};
    files.digests[known.front().locations.front()] = "edited-content";
    app::WorkspaceDiscovery discovery{files, root, known};
    const auto found = discovery.resolve(known.front().locations.front());
    EXPECT_EQ(found.media, known.front().id);
    EXPECT_EQ(found.expected_content_revision, known.front().content_revision);
    EXPECT_EQ(found.fingerprint, "edited-content");
    EXPECT_TRUE(found.known());
    EXPECT_FALSE(found.relocated());
}

TEST_F(WorkspacePolicy, UnreadableObservationNeverInventsAContentIdentity)
{
    app::WorkspaceDiscovery discovery{files, root, {}};
    const auto found = discovery.resolve(output);
    EXPECT_TRUE(found.unreadable);
    EXPECT_FALSE(found.fingerprint);
    EXPECT_FALSE(found.known());
}

TEST_F(WorkspacePolicy, OnlyAnAbsentUniqueCandidateCanBeRelocatedAndOnlyOnce)
{
    const std::vector known{record()};
    files.digests[output] = known.front().fingerprint;
    const auto another = root / "another.flac";
    files.digests[another] = known.front().fingerprint;
    app::WorkspaceDiscovery discovery{files, root, known};
    const auto relocated = discovery.resolve(output);
    EXPECT_EQ(relocated.media, known.front().id);
    EXPECT_TRUE(relocated.relocated());
    EXPECT_EQ(relocated.expected_content_revision, known.front().content_revision);
    EXPECT_FALSE(discovery.resolve(another).known());
}

TEST_F(WorkspacePolicy, PresentAndUnknownCandidatesAreNeverEvidenceForRelocation)
{
    const std::vector known{record()};
    files.digests[output] = known.front().fingerprint;
    for (const auto presence :
        {app::WorkspaceFiles::Presence::present, app::WorkspaceFiles::Presence::unknown}) {
        files.present[known.front().locations.front()] = presence;
        app::WorkspaceDiscovery discovery{files, root, known};
        EXPECT_FALSE(discovery.resolve(output).known());
    }
}

TEST_F(WorkspacePolicy, CapturedEnumerationControlsPresenceWithoutLiveExistenceReads)
{
    const std::vector known{record()};
    files.digests[output] = known.front().fingerprint;
    const std::vector current{known.front().locations.front(), output};
    app::WorkspaceDiscovery present{files, root, known, current};
    EXPECT_FALSE(present.resolve(output).known());
    const std::vector absent{output};
    app::WorkspaceDiscovery moved{files, root, known, absent};
    EXPECT_TRUE(moved.resolve(output).relocated());
    EXPECT_EQ(files.presence_reads, 0);
}

TEST_F(WorkspacePolicy, AmbiguousAbsentCandidatesRemainDistinct)
{
    const std::vector known{record("one"), record("two")};
    files.digests[output] = known.front().fingerprint;
    app::WorkspaceDiscovery discovery{files, root, known};
    EXPECT_FALSE(discovery.resolve(output).known());
}

TEST_F(WorkspacePolicy, AVisibleCandidateDoesNotHideAnotherUniquelyAbsentCandidate)
{
    const std::vector known{record("one"), record("two")};
    files.digests[output] = known.front().fingerprint;
    files.present[known.front().locations.front()] = app::WorkspaceFiles::Presence::present;
    app::WorkspaceDiscovery discovery{files, root, known};
    EXPECT_EQ(discovery.resolve(output).media, known.back().id);
}

TEST_F(WorkspacePolicy, OutsideRootAndEmptyFingerprintsAreNotRelocationCandidates)
{
    auto outside = record("outside");
    outside.locations = {root.parent_path() / "outside.flac"};
    auto unknown = record("unknown");
    unknown.fingerprint.clear();
    const std::vector known{outside, unknown};
    files.digests[output] = outside.fingerprint;
    app::WorkspaceDiscovery discovery{files, root, known};
    EXPECT_FALSE(discovery.resolve(output).known());
    EXPECT_FALSE(app::workspace_contains(root, outside.locations.front()));
    EXPECT_TRUE(app::workspace_contains(root, output));
    EXPECT_TRUE(app::workspace_contains(root, root));
}

TEST_F(WorkspacePolicy, TheSuppliedReaderReceivesTheScanCancellationToken)
{
    std::stop_source cancelled;
    cancelled.request_stop();
    app::WorkspaceDiscovery discovery{files, root, {}, cancelled.get_token()};
    static_cast<void>(discovery.resolve(output));
    EXPECT_TRUE(files.stopped_seen);
}

TEST_F(WorkspacePolicy, ReconciliationPreservesTheMutablePersistenceCallbackIdentity)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    std::vector<int> attempts;
    app::WorkspaceReconciliation reconcile{[&, calls = 0](const auto&) mutable {
        attempts.push_back(++calls);
        return true;
    }};
    app::WorkspaceScanEntry entry{.media = {"stable"}, .location = output, .fingerprint = "first"};
    ASSERT_TRUE(reconcile.reconcile(repository, workspace, std::span{&entry, 1}).succeeded);
    entry.expected_content_revision = repository.track(entry.media)->content_revision;
    entry.fingerprint = "second";
    ASSERT_TRUE(reconcile.reconcile(repository, workspace, std::span{&entry, 1}).succeeded);
    EXPECT_EQ(attempts, (std::vector{1, 2}));
}

TEST_F(WorkspacePolicy, AnAdmittedTransactionMayOutliveTheReconciliationFacade)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    std::unique_ptr<app::WorkspaceReconciliation> reconcile;
    reconcile = std::make_unique<app::WorkspaceReconciliation>([&](const auto&) {
        reconcile.reset();
        return true;
    });
    app::WorkspaceScanEntry entry{.media = {"stable"},
        .location = output,
        .fingerprint = "content"};
    EXPECT_TRUE(reconcile->reconcile(repository, workspace, std::span{&entry, 1}).succeeded);
    EXPECT_FALSE(reconcile);
    ASSERT_NE(repository.track(entry.media), nullptr);
    EXPECT_EQ(repository.track(entry.media)->fingerprint, "content");
}

TEST_F(WorkspacePolicy, RejectedPersistenceRollsBackDiscoveryWithoutLosingTheOriginalCatalog)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    const auto before = repository.tracks();
    app::WorkspaceReconciliation reconcile{[](const auto&) { return false; }};
    app::WorkspaceScanEntry entry{.media = {"stable"},
        .location = output,
        .fingerprint = "content"};
    EXPECT_FALSE(reconcile.reconcile(repository, workspace, std::span{&entry, 1}).succeeded);
    EXPECT_EQ(repository.tracks(), before);
}

TEST_F(WorkspacePolicy, UnknownWorkspaceDoesNotInvokePersistence)
{
    library::InMemoryRepository repository;
    int calls{};
    app::WorkspaceReconciliation reconcile{[&](const auto&) {
        ++calls;
        return true;
    }};
    EXPECT_FALSE(reconcile.reconcile(repository, {"missing"}, {}).succeeded);
    EXPECT_EQ(calls, 0);
}

TEST_F(WorkspacePolicy, InvalidAndRemovedCapturedTargetsAreRejectedWithoutInventingRecords)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    app::WorkspaceReconciliation reconcile{[](const auto&) { return true; }};
    std::vector entries{
        app::WorkspaceScanEntry{.media = {}, .location = output, .fingerprint = "content"},
        app::WorkspaceScanEntry{.media = {"removed"},
                                .location = root / "removed.flac",
                                .fingerprint = "content",
                                .kind = app::WorkspaceDiscoveryKind::known_location}
    };
    const auto result = reconcile.reconcile(repository, workspace, entries);
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.failed, entries.size());
    EXPECT_TRUE(repository.tracks().empty());
}

TEST_F(WorkspacePolicy, AnEmptyObservedFingerprintCannotCreateATrack)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    app::WorkspaceReconciliation reconcile{[](const auto&) { return true; }};
    app::WorkspaceScanEntry entry{.media = {"stable"},
        .location = output,
        .fingerprint = std::string{}};
    const auto result = reconcile.reconcile(repository, workspace, std::span{&entry, 1});
    EXPECT_TRUE(result.succeeded);
    EXPECT_EQ(result.failed, 1U);
    EXPECT_TRUE(repository.tracks().empty());
}

TEST_F(WorkspacePolicy, UnreadableLocationsAreRetainedWithoutDuplicatingPaths)
{
    library::InMemoryRepository repository;
    const auto workspace = repository.create_workspace(root);
    app::WorkspaceReconciliation reconcile{[](const auto&) { return true; }};
    app::WorkspaceScanEntry entry{.media = {"stable"},
        .location = output,
        .fingerprint = {},
        .unreadable = true};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto result = reconcile.reconcile(repository, workspace, std::span{&entry, 1});
        EXPECT_TRUE(result.succeeded);
        EXPECT_EQ(result.failed, 1U);
        ASSERT_NE(repository.track(entry.media), nullptr);
        EXPECT_TRUE(repository.track(entry.media)->missing);
        EXPECT_EQ(repository.track(entry.media)->locations, (std::vector{output}));
    }
}

} // namespace
