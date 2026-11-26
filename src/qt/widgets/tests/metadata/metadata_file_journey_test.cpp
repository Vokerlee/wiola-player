#include <QSignalSpy>
#include <media/codecs/ffmpeg/composition.hpp>

#include <QToolButton>
#include <app/library/local_identity.hpp>
#include <array>
#include <fakes/output.hpp>
#include <media/local/resource.hpp>
#include <qt/application/player_application.hpp>
#include <qt/paths.hpp>
#include <qt/widgets/documents/now_playing_document.hpp>
#include <qt/widgets/metadata/track_inspector.hpp>
#include <qt/widgets/shell/document_host.hpp>
#include <qt/widgets/shell/main_window.hpp>
#include <qt/widgets/tests/support/gui_interaction.hpp>
#include <qt/widgets/tracks/track_browser.hpp>

namespace {

using namespace wiola;

std::vector<float> decoded_samples(const std::filesystem::path& path)
{
    auto provider{wiola::codec::ffmpeg::make_provider()};
    auto asset = provider->open(wiola::media::local_resource(path));
    if (!asset)
        throw std::runtime_error{asset.error().detail};
    auto decoder = (*asset)->create_decoder();
    if (!decoder)
        throw std::runtime_error{decoder.error().detail};
    std::vector<float> samples;
    std::array<float, 4096> block;
    for (int reads = 0; reads < 10000; ++reads) {
        const auto decoded = (*decoder)->read(block);
        if (!decoded)
            throw std::runtime_error{decoded.error().detail};
        const auto count = (*decoder)->spec().samples_per(decoded->frames);
        samples.insert(samples.end(), block.begin(), block.begin() + count);
        if (decoded->end_of_stream)
            return samples;
    }
    throw std::runtime_error{"fixture decoder did not finish within its read budget"};
}

class MetadataFileJourney : public wiola::testing::GuiInteraction,
                            public ::testing::WithParamInterface<const char*> { };

TEST_P(MetadataFileJourney, FileButtonWritesTheSelectedDuplicateAndPreservesDecodedAudio)
{
    const auto path = platform::qt::QtPathAdapter::from_qt(
        data_.filePath(QString::fromUtf8("音楽.") + QFileInfo{GetParam()}.suffix())
    );
    const auto fixtures = qEnvironmentVariableIsEmpty("WIOLA_JOURNEY_MEDIA_DIR")
        ? std::filesystem::path{WIOLA_TEST_DATA_DIR}
        : platform::qt::QtPathAdapter::from_qt(qEnvironmentVariable("WIOLA_JOURNEY_MEDIA_DIR"));
    std::filesystem::copy_file(fixtures / GetParam(), path);
    const auto before = decoded_samples(path);
    ASSERT_FALSE(before.empty());
    app::PlayerRuntimeConfig config;
    config.provider = wiola::codec::ffmpeg::make_provider();
    config.output = [](audio::Source&) {
        return std::make_unique<wiola::testing::FakeOutput>();
    };
    config.library_file = platform::qt::QtPathAdapter::from_qt(data_.filePath("library.json"));
    app::PlayerApplication player{config};
    {
        gui::MainWindow window{player.playback(), player.library(), player.preferences()};
        watch(window);
        window.setUpdatesEnabled(false);
        player.observe(
            window, [&](const auto& state) { window.update_after_pump(state); },
            [&] { window.refresh(); }
        );
        window.open({path, path});
        const auto media = app::media_id_for(path);
        player.library().request_metadata(media, path);
        ASSERT_TRUE(wiola::testing::await_gui([&] {
            const auto record = player.library().snapshot(media);
            return player.playback().loaded() && record && record->metadata_revision > 0;
        }));
        static_cast<void>(player.playback().play());
        ASSERT_TRUE(player.playback().playing());
        window.refresh();
        auto* host = static_cast<gui::DocumentHost*>(window.findChild<QTabWidget*>("documentTabs"));
        ASSERT_NE(host, nullptr);
        auto& table = host->queue().browser().table();
        ASSERT_EQ(table.model()->rowCount(), 2);
        table.selectRow(1);
        auto* inspector =
            static_cast<gui::TrackInspector*>(window.findChild<QWidget*>("trackInspector"));
        ASSERT_NE(inspector, nullptr);
        auto* title = inspector->findChild<QLineEdit*>("standardTitle");
        title->selectAll();
        QTest::keyClicks(title, "GUI embedded title");
        auto* save = inspector->findChild<QPushButton*>("saveMetadataToFile");
        ASSERT_TRUE(save->isEnabled());
        std::vector<app::MetadataWriteResult> terminal;
        QObject::connect(&player.library(), &app::LibrarySession::file_write_outcome, &window,
            [&](const auto& result) {
                if (result.stage == app::MetadataWriteStage::complete ||
                    result.stage == app::MetadataWriteStage::failed ||
                    result.stage == app::MetadataWriteStage::conflicted ||
                    result.stage == app::MetadataWriteStage::cancelled)
                    terminal.push_back(result);
            });
        QTest::mouseClick(save, Qt::LeftButton);
        ASSERT_TRUE(wiola::testing::await_gui([&] { return !terminal.empty(); }, 10000));
        ASSERT_EQ(terminal.size(), 1);
        const bool writable = std::string_view{GetParam()} != "tone.wav";
        if (writable) {
            ASSERT_EQ(terminal.front().stage, app::MetadataWriteStage::complete)
                << terminal.front().outcome.detail;
            EXPECT_EQ(terminal.front().job.media, media);
            EXPECT_EQ(inspector->findChild<QLabel*>("metadataFileStatus")->text(),
                "Saved embedded metadata to file");
            EXPECT_EQ(player.library().snapshot(media)->embedded.title, "GUI embedded title");
        } else {
            EXPECT_EQ(terminal.front().stage, app::MetadataWriteStage::failed);
            EXPECT_EQ(terminal.front().outcome.error, media::MetadataWriteError::unsupported);
            EXPECT_NE(inspector->findChild<QLabel*>("metadataFileStatus")
                          ->text()
                          .indexOf("does not support"),
                -1);
            EXPECT_EQ(player.library().snapshot(media)->overrides.title, "GUI embedded title");
            EXPECT_NE(player.library().snapshot(media)->embedded.title, "GUI embedded title");
        }
        EXPECT_EQ(table.model()->rowCount(), 2);
        EXPECT_EQ(table.chosen_rows().size(), 1);
        auto reader{wiola::codec::ffmpeg::make_provider()};
        auto reopened = reader->open(media::local_resource(path));
        ASSERT_TRUE(reopened);
        auto metadata = (*reopened)->read_metadata();
        ASSERT_TRUE(metadata);
        if (writable)
            EXPECT_EQ(metadata->track.title, "GUI embedded title");
        else
            EXPECT_NE(metadata->track.title, "GUI embedded title");
        EXPECT_EQ(decoded_samples(path), before);
        ASSERT_TRUE(wiola::testing::await_gui([&] {
            return player.playback().loaded() && player.playback().playing();
        }));
        window.close();
    }
    ASSERT_TRUE(player.shutdown());
}

INSTANTIATE_TEST_SUITE_P(QualifiedFormats, MetadataFileJourney,
    ::testing::Values("tone.mp3", "tone.flac", "tone-vorbis.ogg", "tone.wav"));

} // namespace

int main(int argc, char** argv)
{
    return wiola::testing::run_gui_tests(argc, argv);
}
