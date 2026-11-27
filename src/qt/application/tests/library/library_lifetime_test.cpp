#include <app/library/local_identity.hpp>
#include <fakes/provider.hpp>
#include <fixtures/gated_media_provider.hpp>
#include <qt/application/library/library_session.hpp>
#include <qt/tests/support/atomic_output.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/fingerprint.hpp>
#include <wiola/media/local/resource.hpp>
#include <wiola/qt/paths.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include <utility>

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

        std::filesystem::path staged;
        {
            auto provider{wiola::codec::ffmpeg::make_provider()};
            auto asset = provider->open(wiola::media::local_resource(path));

            ASSERT_TRUE(asset);

            media::MetadataWriteRequest request;
            request.overrides.title = "Original physical metadata";

            const auto outcome = (*asset)->write_metadata(request);

            ASSERT_EQ(outcome.state, media::MetadataWriteState::written);

            staged = outcome.staged_output;
            ASSERT_FALSE(staged.empty());
        }
        // This private fixture is not open in a session yet; install its tagged source.
        std::filesystem::remove(path);
        std::filesystem::rename(staged, path);
        media = app::media_id_for(path);
        barrier_id = app::media_id_for(barrier);
    }

    QTemporaryDir directory;
    std::filesystem::path path, barrier;
    library::MediaId media, barrier_id;
};

TEST_F(LibraryLifetime, CapturedReadAfterPhysicalWriteCannotOverwriteNewEmbeddedMetadata)
{
    wiola::testing::MetadataReadGate gate;
    auto provider =
        std::make_shared<wiola::testing::GatedMediaProvider>(wiola::codec::ffmpeg::make_provider(),
            gate);
    app::LibrarySession session{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")), provider};
    ReleaseRead release{gate};
    {
        library::RepositoryTransaction seed{session.repository(), session.persistence()};
        library::TrackRecord record;
        record.id = media;
        record.locations = {path};

        const auto fingerprint = media::fingerprint_media(path);

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
    auto provider =
        std::make_shared<wiola::testing::GatedMediaProvider>(wiola::codec::ffmpeg::make_provider(),
            gate);
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

TEST(LibraryMetadata, UnavailableBackgroundTagsDoNotPublishGlobalErrorMessages)
{
    QTemporaryDir directory;

    ASSERT_TRUE(directory.isValid());

    auto provider = wiola::testing::FakeMediaProvider::metadata_only(
        [](const auto&, const auto&) -> wiola::media::Result<wiola::media::Metadata> {
            return wiola::media::unexpected{
                wiola::media::Error{.category = wiola::media::ErrorCategory::unsupported,
                                    .detail = "no tags"}
            };
        }
    );
    wiola::app::LibrarySession session{
        wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")), provider};
    QStringList notices;
    QObject::connect(&session, &wiola::app::LibrarySession::status_changed, &session,
        [&](const QString& notice) { notices.push_back(notice); });

    const wiola::library::MediaId media{"no-tags"};
    session.request_metadata(media, "no-tags.wav");
    ASSERT_TRUE(QTest::qWaitFor([&] { return session.artwork(media).known(); }));
    EXPECT_TRUE(notices.empty());
    EXPECT_EQ(session.repository().track(media), nullptr);
    EXPECT_TRUE(session.shutdown().succeeded());
}

TEST_F(LibraryLifetime, LearningASecondLocationRestoresAvailabilityWithoutChangingMediaIdentity)
{
    app::LibrarySession session{
        platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json")),
        codec::ffmpeg::make_provider()};
    library::TrackRecord record;
    record.id = media;
    record.locations = {path};
    record.missing = true;
    {
        library::RepositoryTransaction transaction{session.repository(), session.persistence()};
        ASSERT_TRUE(session.repository().put_track(record));
        ASSERT_TRUE(transaction.commit());
    }
    session.request_metadata(media, barrier);
    ASSERT_TRUE(QTest::qWaitFor([&] { return session.artwork(media).known(); }));
    const auto& view = std::as_const(session).repository();
    ASSERT_NE(view.track(media), nullptr);
    EXPECT_FALSE(view.track(media)->missing);
    EXPECT_EQ(view.track(media)->locations, (std::vector<std::filesystem::path>{path, barrier}));
    EXPECT_TRUE(view.track(media)->embedded.duration.has_value());
    session.request_metadata({}, {});
    EXPECT_EQ(session.scan_workspace(library::WorkspaceId{"absent"}, {".mp3"}), 0);
    ASSERT_TRUE(session.shutdown().succeeded());
    session.request_metadata(media, path);
    EXPECT_EQ(session.scan_workspace(library::WorkspaceId{"absent"}, {".mp3"}), 0);
}

TEST(LibrarySessionPersistence, FailedSaveCanRetryAndRestoreTheAcceptedPlaylistDestination)
{
    using namespace wiola;
    QTemporaryDir directory;
    const auto file = platform::qt::QtPathAdapter::from_qt(directory.filePath("library.json"));
    bool fail = true;
    library::PlaylistId saved;
    {
        app::LibrarySession session{file, codec::ffmpeg::make_provider(), nullptr,
            [&](const auto& path) {
                return std::make_unique<wiola::testing::FaultAtomicOutput>(path, fail);
            }};
        QStringList notices;
        QObject::connect(&session, &app::LibrarySession::status_changed, &session,
            [&](const QString& text) { notices.push_back(text); });
        saved = session.playlists().create("Retained").id;
        ASSERT_TRUE(saved.valid());
        ASSERT_TRUE(QTest::qWaitFor([&] { return notices.join(' ').contains("saving failed"); }));
        EXPECT_NE(session.repository().playlist(saved), nullptr);
        fail = false;
        session.retry_save();
        ASSERT_TRUE(QTest::qWaitFor([&] {
            return notices.contains("Pending library changes saved");
        }));
        ASSERT_TRUE(session.playlist_destination().set(saved));
        ASSERT_TRUE(session.flush().succeeded());
    }
    QSettings legacy{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"};
    legacy.setValue("playlists/active", QString::fromStdString(saved.value));
    legacy.sync();
    app::LibrarySession restored{file, codec::ffmpeg::make_provider()};
    EXPECT_NE(std::as_const(restored).repository().playlist(saved), nullptr);
    EXPECT_EQ(restored.playlist_destination().id(), saved);
}

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
