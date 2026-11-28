/**
 * @file
 * @brief Metadata conflict and shutdown lifetimes at durability boundaries.
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

#include <qt/application/metadata/metadata_write_service.hpp>
#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/local/fingerprint.hpp>
#include <wiola/qt/paths.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace wiola;

/// Holds durability acknowledgments while real staging continues, without sleeps or private hooks.
class DeferredMetadataWrites : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory_.isValid());
        replacement_ = media::native_file_replacement(media::fingerprint_media);
        service_ = std::make_unique<app::MetadataWriteService>(
            *QCoreApplication::instance(), codec::ffmpeg::make_provider(), repository_,
            *replacement_,
            [this](const auto&) {
                const auto revision = repository_.staged_delta()->changes.revision;
                return library::CommitResult::accepted(revision,
                    std::make_shared<library::CommitReceipt>(revision));
            },
            [this](const auto& result) { observed_.push_back(result); }
        );
        service_->set_durability([this](
                                     std::uint64_t, auto completion
                                 ) { acknowledgments_.push_back(std::move(completion)); },
            [this] {
                while (!acknowledgments_.empty())
                    acknowledge(false);
            });
    }

    library::MediaId add_track(const std::string& name)
    {
        const auto source =
            platform::qt::QtPathAdapter::from_qt(directory_.path()) / (name + ".mp3");
        std::filesystem::copy_file(std::filesystem::path{WIOLA_TEST_DATA_DIR} / "tone.mp3", source);

        library::TrackRecord record;
        record.id = library::MediaId{name};
        record.locations = {source};
        EXPECT_TRUE(repository_.put_track(record));
        return record.id;
    }

    std::vector<app::MetadataWriteOperationId> submit(const library::MediaId& media)
    {
        auto edit = app::MetadataEditSession::capture({repository_.track(media)});
        library::StandardMetadataPatch patch;
        patch.title = "New title";
        edit.set_draft(patch);
        return service_->submit(edit);
    }

    template<typename Predicate>
    bool await(Predicate ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!ready() && std::chrono::steady_clock::now() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return ready();
    }

    void acknowledge(bool durable)
    {
        auto completion = std::move(acknowledgments_.front());
        acknowledgments_.pop_front();
        completion(durable);
    }

    std::vector<app::MetadataWriteResult> terminal() const
    {
        std::vector<app::MetadataWriteResult> result;
        for (const auto& value : observed_) {
            switch (value.stage) {
            case app::MetadataWriteStage::complete:
            case app::MetadataWriteStage::conflicted:
            case app::MetadataWriteStage::cancelled:
            case app::MetadataWriteStage::failed:
                result.push_back(value);
                break;
            default:
                break;
            }
        }
        return result;
    }

    QTemporaryDir directory_;
    library::InMemoryRepository repository_;
    std::unique_ptr<media::FileReplacement> replacement_;
    std::deque<std::function<void(bool)>> acknowledgments_;
    std::vector<app::MetadataWriteResult> observed_;
    // Closing drains callbacks while every captured collaborator above is still alive.
    std::unique_ptr<app::MetadataWriteService> service_;
};

TEST_F(DeferredMetadataWrites, ConflictedIntentLeavesTheServiceAvailableForTheNextSubmission)
{
    const auto track = add_track("editable");
    ASSERT_EQ(submit(track).size(), 1U);
    ASSERT_TRUE(await([&] { return !acknowledgments_.empty(); }));

    auto changed = *repository_.track(track);
    ++changed.content_revision;
    ASSERT_TRUE(repository_.put_track(changed));
    acknowledge(true);

    const auto first = terminal();
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().stage, app::MetadataWriteStage::conflicted);
    EXPECT_FALSE(first.front().outcome.source_modified);

    ASSERT_EQ(submit(track).size(), 1U);
    ASSERT_TRUE(await([&] { return !acknowledgments_.empty(); }));
    acknowledge(false);

    const auto both = terminal();
    ASSERT_EQ(both.size(), 2U);
    EXPECT_EQ(both.back().stage, app::MetadataWriteStage::failed);
    EXPECT_FALSE(both.back().outcome.source_modified);
}

TEST_F(DeferredMetadataWrites, ClosingCancelsPreparedJobsWaitingBehindADurabilityGate)
{
    const auto held = add_track("held");
    const auto waiting = add_track("waiting");
    const auto marker = add_track("delivery-marker");
    ASSERT_EQ(submit(held).size(), 1U);
    ASSERT_TRUE(await([&] { return !acknowledgments_.empty(); }));

    const auto waiting_operations = submit(waiting);
    ASSERT_EQ(waiting_operations.size(), 1U);
    ASSERT_EQ(submit(marker).size(), 1U);
    // Staging and owner delivery are FIFO: later validation follows the prior prepared delivery.
    ASSERT_TRUE(await([&] {
        return std::ranges::any_of(observed_, [&](const auto& result) {
            return result.job.media == marker &&
                result.stage == app::MetadataWriteStage::validating;
        });
    }));

    EXPECT_NO_THROW(service_->stop());

    const auto results = terminal();
    ASSERT_EQ(results.size(), 3U);
    const auto cancelled = std::ranges::find_if(results,
        [&](const auto& result) { return result.job.operation == waiting_operations.front(); });
    ASSERT_NE(cancelled, results.end());
    EXPECT_EQ(cancelled->stage, app::MetadataWriteStage::cancelled);
    EXPECT_TRUE(acknowledgments_.empty());
    for (const auto& result : results)
        EXPECT_FALSE(result.outcome.source_modified);
}

} // namespace
