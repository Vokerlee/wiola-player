#include "fixture_files.hpp"

#include <persistence/qt/library_json.hpp>
#include <persistence/qt/library_store.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <limits>

namespace {

using namespace wiola;
using namespace wiola::testing::compatibility;
using namespace persistence::qt;

TEST(CompatibilityLibrary, ReadsAuthoredUnicodeLayeringAndDuplicateOccurrences)
{
    library::LibrarySnapshot snapshot;
    ASSERT_EQ(read_library_json(fixture("library/current-v1.json"), snapshot),
        LibraryJsonError::none);
    ASSERT_EQ(snapshot.tracks.size(), 1U);
    const auto& track = snapshot.tracks.front();
    EXPECT_EQ(track.id.value, "media-楽曲");
    EXPECT_EQ(track.embedded.title, "Соната 楽曲");
    EXPECT_EQ(track.embedded.artist, "Бах");
    ASSERT_TRUE(track.overrides.title);
    EXPECT_TRUE(track.overrides.title->empty());
    ASSERT_TRUE(track.overrides.year);
    EXPECT_FALSE(*track.overrides.year);
    EXPECT_EQ(track.content_revision, std::numeric_limits<std::uint64_t>::max() - 1);
    EXPECT_TRUE(track.missing);
    ASSERT_EQ(track.tags.size(), 2U);
    EXPECT_EQ(track.tags[0].key, track.tags[1].key);
    EXPECT_EQ(track.tags[1].value, "Live");
    ASSERT_EQ(track.locations.size(), 2U);
    EXPECT_EQ(platform::qt::QtPathAdapter::to_qt(track.locations[0]),
        QString::fromUtf8("Music/楽曲.flac"));
    ASSERT_EQ(snapshot.playlists.size(), 1U);
    const auto& entries = snapshot.playlists.front().entries;
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0].id.value, "entry-a");
    EXPECT_EQ(entries[1].id.value, "entry-b");
    EXPECT_EQ(entries[0].media, entries[1].media);
    ASSERT_EQ(snapshot.workspaces.size(), 1U);
    EXPECT_FALSE(snapshot.workspaces.front().recursive);
}

TEST(CompatibilityLibrary, OldOptionalDefaultsDoNotDependOnCurrentWriter)
{
    library::LibrarySnapshot snapshot;
    ASSERT_EQ(read_library_json(fixture("library/minimal-v1.json"), snapshot),
        LibraryJsonError::none);
    ASSERT_EQ(snapshot.tracks.size(), 1U);
    EXPECT_EQ(snapshot.tracks.front().id.value, "old-media");
    EXPECT_TRUE(snapshot.tracks.front().tags.empty());
    EXPECT_TRUE(snapshot.tracks.front().locations.empty());
    EXPECT_FALSE(snapshot.tracks.front().overrides.title);
    EXPECT_EQ(snapshot.tracks.front().content_revision, 0U);
    EXPECT_FALSE(snapshot.tracks.front().missing);
    EXPECT_TRUE(snapshot.workspaces.empty());
}

TEST(CompatibilityLibrary, RejectedDocumentsRetainThePreviousSnapshot)
{
    library::LibrarySnapshot snapshot;
    ASSERT_EQ(read_library_json(fixture("library/current-v1.json"), snapshot),
        LibraryJsonError::none);
    const auto original = snapshot;
    for (const auto& [path, error] : {
             std::pair{"library/invalid-reference-v1.json", LibraryJsonError::invalid_record     },
             std::pair{"library/future.json",               LibraryJsonError::unsupported_version},
             std::pair{"malformed.json",                    LibraryJsonError::malformed          }
    }) {
        EXPECT_EQ(read_library_json(fixture(path), snapshot), error);
        EXPECT_EQ(snapshot.tracks, original.tracks);
        EXPECT_EQ(snapshot.playlists, original.playlists);
        EXPECT_EQ(snapshot.workspaces, original.workspaces);
    }
}

TEST(CompatibilityLibrary, FutureFileSurvivesLoadSaveAndStoreDestructionByteForByte)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("library.json");
    const auto future = fixture("library/future.json");
    overwrite(path, future);
    {
        LibraryStore store{platform::qt::QtPathAdapter::from_qt(path)};
        library::LibrarySnapshot output;
        EXPECT_EQ(store.load(output).schema, LibraryJsonError::unsupported_version);
        EXPECT_EQ(store.save(output).schema, LibraryJsonError::unsupported_version);
    }
    EXPECT_EQ(contents(path), future);
}

} // namespace
