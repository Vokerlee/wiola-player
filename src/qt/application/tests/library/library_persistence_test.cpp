/**
 * @file
 * @brief Repository rollback, allocation, and persistence contract tests.
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

#include <QCoreApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qt/application/library/library_persistence.hpp>
#include <qt/tests/support/gated_atomic_output.hpp>
#include <wiola/library/repository_transaction.hpp>

namespace {

using namespace wiola;

struct ReleaseGate {
    wiola::testing::AtomicWriteGate& gate;

    ~ReleaseGate() { gate.release(); }
};

TEST(LibraryPersistence, CoalescesAcceptedRevisionsAndRetriesWithoutLosingEitherEdit)
{
    QTemporaryDir directory;
    const auto path = platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    wiola::testing::AtomicWriteGate gate;
    gate.fail = true;
    persistence::qt::LibraryStore store{path,
        [&](const auto& file) {
            return std::make_unique<wiola::testing::GatedAtomicOutput>(file, path, gate);
        }};
    QObject owner;
    app::LibraryPersistence writer{owner, store, {}};
    ReleaseGate release{gate};
    library::InMemoryRepository repository;
    const library::RepositoryTransaction::Persist persist = [&](const auto&) {
        return writer.accept(repository.staged_delta());
    };
    const auto edit = [&](const char* title) {
        library::RepositoryTransaction transaction{repository, persist};
        library::TrackRecord track;
        track.id = {title};
        track.embedded.title = title;
        EXPECT_TRUE(repository.put_track(track));
        return transaction.commit();
    };
    const auto first = edit("A");
    ASSERT_EQ(first.state, library::CommitState::accepted);
    ASSERT_TRUE(gate.wait_entered());
    const auto second = edit("B");
    ASSERT_EQ(second.state, library::CommitState::accepted);
    std::vector<bool> a, b;
    first.receipt->observe([&](auto event) { a.push_back(event.durable); });
    second.receipt->observe([&](auto event) { b.push_back(event.durable); });
    gate.release();
    EXPECT_FALSE(writer.flush().succeeded());
    EXPECT_NE(repository.track(library::MediaId{"A"}), nullptr);
    EXPECT_NE(repository.track(library::MediaId{"B"}), nullptr);
    EXPECT_FALSE(first.receipt->durable());
    EXPECT_FALSE(second.receipt->durable());
    gate.fail = false;
    writer.retry();
    ASSERT_TRUE(writer.flush().succeeded());
    EXPECT_TRUE(first.receipt->durable());
    EXPECT_TRUE(second.receipt->durable());
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    EXPECT_FALSE(a.front());
    EXPECT_FALSE(b.front());
    EXPECT_TRUE(a.back());
    EXPECT_TRUE(b.back());
    library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    ASSERT_EQ(restored.tracks.size(), 2);
    EXPECT_EQ(restored.tracks[0].embedded.title, "A");
    EXPECT_EQ(restored.tracks[1].embedded.title, "B");
    EXPECT_EQ(writer.durable_revision(), 2);
    EXPECT_NE(gate.writer, std::this_thread::get_id());
    writer.stop();
}

TEST(LibraryPersistence,
    BoundedBackpressureRejectsOnlyTheNewestTransactionAndCoalescesThePendingPrefix)
{
    QTemporaryDir directory;
    const auto path = platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    wiola::testing::AtomicWriteGate gate;
    persistence::qt::LibraryStore store{path,
        [&](const auto& file) {
            return std::make_unique<wiola::testing::GatedAtomicOutput>(file, path, gate);
        }};
    QObject owner;
    app::LibraryPersistence writer{owner, store, {}};
    ReleaseGate release{gate};
    library::InMemoryRepository repository;
    const library::RepositoryTransaction::Persist persist = [&](const auto&) {
        return writer.accept(repository.staged_delta());
    };
    const auto create = [&] {
        library::RepositoryTransaction transaction{repository, persist};
        repository.create_playlist("Accepted");
        return transaction.commit();
    };
    ASSERT_TRUE(create());
    ASSERT_TRUE(gate.wait_entered());
    for (int i = 0; i < 63; ++i)
        ASSERT_TRUE(create());
    EXPECT_FALSE(create());
    EXPECT_EQ(repository.playlists().size(), 64);
    EXPECT_EQ(repository.change_revision(), 64);
    {
        library::RepositoryTransaction physical_result{repository, persist,
            library::CommitPriority::file_result};
        repository.create_playlist("Physical write result");
        ASSERT_TRUE(physical_result.commit());
    }
    EXPECT_FALSE(create());
    EXPECT_EQ(repository.playlists().size(), 65);
    EXPECT_EQ(repository.change_revision(), 65);
    gate.release();
    ASSERT_TRUE(writer.flush().succeeded());
    EXPECT_EQ(gate.primary_writes, 2);
    library::LibrarySnapshot restored;
    ASSERT_TRUE(store.load(restored).succeeded());
    EXPECT_EQ(restored.playlists.size(), 65);
    writer.stop();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
