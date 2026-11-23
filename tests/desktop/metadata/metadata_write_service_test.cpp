/**
 * @file
 * @brief Synchronized payload delivery to the desktop event loop.
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
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <chrono>
#include <codec/provider.hpp>
#include <desktop/metadata/metadata_write_service.hpp>
#include <gtest/gtest.h>
#include <platform/qt/qt_path_adapter.hpp>

namespace {

bool pump(const std::function<bool()>& ready)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!ready() && std::chrono::steady_clock::now() < end)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return ready();
}

TEST(MetadataWriteService, EnqueueRejectionIsIdentifiedAndDeliveredLaterOnOwnerThread)
{
    QObject context;
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = {"missing-location"};
    repository.put_track(record);
    auto edit = wiola::app::MetadataEditSession::capture({repository.track(record.id)});
    wiola::library::StandardMetadataPatch patch;
    patch.title = "Draft";
    edit.set_draft(patch);
    std::vector<wiola::app::MetadataWriteResult> outcomes;
    wiola::app::MetadataWriteService service{context,
        std::make_shared<wiola::codec::LocalMediaProvider>(), repository,
        [](const auto&) { return true; },
        [&](const auto& result) {
            EXPECT_EQ(QThread::currentThread(), context.thread());
            outcomes.push_back(result);
        }};
    const auto ids = service.submit(edit);
    ASSERT_EQ(ids.size(), 1);
    ASSERT_EQ(outcomes.size(), 1);
    EXPECT_EQ(outcomes.front().stage, wiola::app::MetadataWriteStage::queued);
    ASSERT_TRUE(pump([&] { return outcomes.size() == 2; }));
    EXPECT_EQ(outcomes.back().job.operation, ids.front());
    EXPECT_EQ(outcomes.back().stage, wiola::app::MetadataWriteStage::failed);
    service.stop();
    QCoreApplication::processEvents();
    EXPECT_EQ(outcomes.size(), 2);
}

TEST(MetadataWriteService, ReentrantSubmissionKeepsNewestWriteAndTerminatesTheSupersededOne)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = wiola::platform::qt::QtPathAdapter::from_qt(directory.filePath("track.mp3"));
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", path);
    wiola::library::InMemoryRepository repository;
    wiola::library::TrackRecord record;
    record.id = {"media"};
    record.locations = {path};
    repository.put_track(record);
    auto first = wiola::app::MetadataEditSession::capture({repository.track(record.id)}),
         second = first;
    wiola::library::StandardMetadataPatch patch;
    patch.title = "First";
    first.set_draft(patch);
    patch.title = "Newest";
    second.set_draft(patch);
    wiola::app::MetadataWriteService* pointer = nullptr;
    bool submitted = false;
    std::vector<wiola::app::MetadataWriteResult> terminal;
    wiola::app::MetadataWriteService service{*QCoreApplication::instance(),
        std::make_shared<wiola::codec::LocalMediaProvider>(), repository,
        [](const auto&) { return true; },
        [&](const auto& result) {
            if (result.stage == wiola::app::MetadataWriteStage::queued && !submitted) {
                submitted = true;
                static_cast<void>(pointer->submit(second));
            }
            if (result.stage == wiola::app::MetadataWriteStage::complete ||
                result.stage == wiola::app::MetadataWriteStage::failed ||
                result.stage == wiola::app::MetadataWriteStage::cancelled)
                terminal.push_back(result);
        }};
    pointer = &service;
    const auto accepted = service.submit(first);
    ASSERT_EQ(accepted.size(), 1);
    ASSERT_TRUE(pump([&] { return terminal.size() == 2; }));
    EXPECT_NE(terminal.front().job.operation, terminal.back().job.operation);
    EXPECT_EQ(terminal.front().stage, wiola::app::MetadataWriteStage::cancelled);
    EXPECT_EQ(terminal.back().stage, wiola::app::MetadataWriteStage::complete)
        << terminal.back().outcome.detail;
    EXPECT_EQ(repository.track(record.id)->embedded.title, "Newest");
    service.stop();
}

void qualify_durable_file_write(bool save_result)
{
    using namespace wiola;
    QTemporaryDir directory;
    const auto path = platform::qt::QtPathAdapter::from_qt(directory.filePath("音楽.mp3"));
    std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", path);
    const auto bytes = [&] {
        QFile file{platform::qt::QtPathAdapter::to_qt(path)};
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        return file.readAll();
    };
    const auto original = bytes();
    ASSERT_FALSE(original.isEmpty());
    library::InMemoryRepository repository;
    library::TrackRecord record;
    record.id = {"media"};
    record.locations = {path};
    ASSERT_TRUE(repository.put_track(record));
    auto edit = app::MetadataEditSession::capture({repository.track(record.id)});
    library::StandardMetadataPatch patch;
    patch.title = "Durable title";
    edit.set_draft(patch);

    struct Pending {
        std::uint64_t revision;
        std::function<void(bool)> completion;
    };

    std::deque<Pending> pending;
    std::vector<app::MetadataWriteResult> terminal;
    int handoffs = 0;
    app::MetadataWriteService service{*QCoreApplication::instance(),
        std::make_shared<codec::LocalMediaProvider>(), repository,
        [&](const auto&) {
            const auto revision = repository.staged_delta()->changes.revision;
            return library::CommitResult::accepted(revision,
                std::make_shared<library::CommitReceipt>(revision));
        },
        [&](const auto& result) {
            if (result.stage == app::MetadataWriteStage::complete ||
                result.stage == app::MetadataWriteStage::failed)
                terminal.push_back(result);
        }};
    service.set_handoff([&](const auto&) {
        ++handoffs;
        return app::MetadataWriteService::Restore{};
    });
    service.set_durability([&](
                               std::uint64_t revision, auto completion
                           ) { pending.push_back({revision, std::move(completion)}); },
        [&] {
            while (!pending.empty()) {
                auto completion = std::move(pending.front().completion);
                pending.pop_front();
                completion(false);
            }
        });
    ASSERT_EQ(service.submit(edit).size(), 1);
    ASSERT_TRUE(pump([&] { return !pending.empty(); }));
    EXPECT_EQ(handoffs, 0);
    EXPECT_EQ(bytes(), original);
    auto rejected = std::move(pending.front().completion);
    pending.pop_front();
    rejected(false);
    ASSERT_EQ(terminal.size(), 1);
    EXPECT_FALSE(terminal.back().outcome.source_modified);
    EXPECT_EQ(terminal.back().stage, app::MetadataWriteStage::failed);
    EXPECT_EQ(bytes(), original);
    ASSERT_EQ(service.submit(edit).size(), 1);
    ASSERT_TRUE(pump([&] { return !pending.empty(); }));
    auto intent = std::move(pending.front().completion);
    pending.pop_front();
    intent(true);
    ASSERT_TRUE(pump([&] { return !pending.empty(); }));
    EXPECT_EQ(handoffs, 1);
    EXPECT_NE(bytes(), original);
    EXPECT_EQ(terminal.size(), 1);
    EXPECT_EQ(repository.track(record.id)->embedded.title, "Durable title");
    EXPECT_EQ(pending.front().revision, repository.change_revision());
    auto after = std::move(pending.front().completion);
    pending.pop_front();
    after(save_result);
    ASSERT_EQ(terminal.size(), 2);
    EXPECT_TRUE(terminal.back().outcome.source_modified);
    EXPECT_EQ(terminal.back().stage,
        save_result ? app::MetadataWriteStage::complete : app::MetadataWriteStage::failed);
    if (!save_result)
        EXPECT_NE(terminal.back().outcome.detail.find("remain in memory"), std::string::npos);
    EXPECT_EQ(repository.track(record.id)->embedded.title, "Durable title");
    service.stop();
}

TEST(MetadataWriteService,
    ReplacementWaitsForDurableIntentAndCompletionWaitsForDurableLibraryResult)
{
    qualify_durable_file_write(true);
}

TEST(MetadataWriteService,
    FailedLibraryAcknowledgmentAfterReplacementKeepsLearnedStateAndReportsSourceModified)
{
    qualify_durable_file_write(false);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication app{argc, argv};
    return RUN_ALL_TESTS();
}
