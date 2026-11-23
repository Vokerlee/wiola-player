#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <codec/provider.hpp>
#include <desktop/library/library_session.hpp>
#include <fixtures/gated_media_provider.hpp>
#include <gtest/gtest.h>
#include <library/fingerprint.hpp>
#include <library/location.hpp>
#include <platform/qt/qt_path_adapter.hpp>

namespace {

using namespace wiola;

struct ReleaseRead {
    wiola::testing::MetadataReadGate& gate;

    ~ReleaseRead() { gate.release(); }
};

class LibraryLifetime : public ::testing::Test {
protected:
    void SetUp() override
    {
        path = platform::qt::QtPathAdapter::from_qt(directory.filePath("track.mp3"));
        barrier = platform::qt::QtPathAdapter::from_qt(directory.filePath("barrier.mp3"));
        std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", path);
        std::filesystem::copy_file(path, barrier);
        media = library::media_id_for(path);
        barrier_id = library::media_id_for(barrier);
    }

    QTemporaryDir directory;
    std::filesystem::path path, barrier;
    library::MediaId media, barrier_id;
};

TEST_F(LibraryLifetime, CapturedReadAfterPhysicalWriteCannotOverwriteNewEmbeddedMetadata)
{
    wiola::testing::MetadataReadGate gate;
    auto provider = std::make_shared<wiola::testing::GatedMediaProvider>(
        std::make_shared<codec::LocalMediaProvider>(), gate
    );
    app::LibrarySession session{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")), provider};
    ReleaseRead release{gate};
    {
        library::RepositoryTransaction seed{session.repository(), session.persistence()};
        library::TrackRecord record;
        record.id = media;
        record.locations = {path};
        const auto fingerprint = library::fingerprint_media(path);
        ASSERT_TRUE(fingerprint);
        record.fingerprint = *fingerprint;
        ASSERT_TRUE(session.repository().put_track(record));
        ASSERT_TRUE(seed.commit());
    }
    session.request_metadata(media, path);
    ASSERT_TRUE(gate.wait_entered());
    auto edit = app::MetadataEditSession::capture({session.repository().track(media)});
    library::StandardMetadataPatch patch;
    patch.title = "New physical metadata";
    edit.set_draft(patch);
    std::optional<app::MetadataWriteResult> terminal;
    QObject::connect(&session, &app::LibrarySession::file_write_outcome, &session,
        [&](const auto& result) {
            if (result.stage == app::MetadataWriteStage::complete ||
                result.stage == app::MetadataWriteStage::failed ||
                result.stage == app::MetadataWriteStage::conflicted)
                terminal = result;
        });
    ASSERT_EQ(session.file_writes().submit(edit).size(), 1);
    ASSERT_TRUE(QTest::qWaitFor([&] { return terminal.has_value(); }, 10000));
    ASSERT_EQ(terminal->stage, app::MetadataWriteStage::complete) << terminal->outcome.detail;
    ASSERT_EQ(session.snapshot(media)->embedded.title, "New physical metadata");
    gate.release();
    session.request_metadata(barrier_id, barrier);
    // Same worker/owner delivery order: observing this later request proves the old result ran.
    ASSERT_TRUE(QTest::qWaitFor([&] { return session.snapshot(barrier_id).has_value(); }));
    EXPECT_GE(gate.returned.load(), 2);
    EXPECT_EQ(session.snapshot(media)->embedded.title, "New physical metadata");
    EXPECT_GT(session.snapshot(media)->content_revision, 0);
    EXPECT_EQ(session.search().query({.title = "new physical"}),
        (std::vector<library::MediaId>{media}));
    ASSERT_TRUE(session.shutdown().succeeded());
}

TEST_F(LibraryLifetime, CapturedReadAfterRecordRemovalCannotResurrectItsIdentity)
{
    wiola::testing::MetadataReadGate gate;
    auto provider = std::make_shared<wiola::testing::GatedMediaProvider>(
        std::make_shared<codec::LocalMediaProvider>(), gate
    );
    app::LibrarySession session{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")), provider};
    ReleaseRead release{gate};
    {
        library::RepositoryTransaction seed{session.repository(), session.persistence()};
        library::TrackRecord record;
        record.id = media;
        record.locations = {path};
        ASSERT_TRUE(session.repository().put_track(record));
        ASSERT_TRUE(seed.commit());
    }
    session.request_metadata(media, path);
    ASSERT_TRUE(gate.wait_entered());
    {
        library::RepositoryTransaction remove{session.repository(), session.persistence()};
        ASSERT_TRUE(session.repository().replace_all({}, {}, {}));
        ASSERT_TRUE(remove.commit());
    }
    gate.release();
    session.request_metadata(barrier_id, barrier);
    ASSERT_TRUE(QTest::qWaitFor([&] { return session.snapshot(barrier_id).has_value(); }));
    EXPECT_GE(gate.returned.load(), 2);
    EXPECT_FALSE(session.snapshot(media));
    EXPECT_FALSE(session.search().contains(media));
    ASSERT_TRUE(session.shutdown().succeeded());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application{argc, argv};
    QTemporaryDir settings;
    if (!settings.isValid())
        return 2;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
