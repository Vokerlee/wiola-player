/**
 * @file
 * @brief Queued media-revision publication regression tests.
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

#include <qt/application/metadata/media_revision_publisher.hpp>

#include <wiola/media/local/resource.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>

#include <fixtures/gated_media_provider.hpp>
#include <functional>

namespace {

class FakeAsset final : public wiola::media::MediaAsset {
public:
    explicit FakeAsset(std::string title)
        : title_{std::move(title)}
    {
    }

    wiola::media::Result<wiola::media::MediaInfo>
    inspect(const wiola::media::OperationContext&) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unsupported, "test"}
        };
    }

    wiola::media::Result<wiola::media::Metadata>
    read_metadata(const wiola::media::OperationContext&) override
    {
        wiola::media::Metadata metadata;
        metadata.track.title = title_;

        return metadata;
    }

    wiola::media::Result<std::unique_ptr<wiola::media::AudioDecoder>>
    create_decoder(const wiola::media::OperationContext&) override
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unsupported, "test"}
        };
    }

private:
    std::string title_;
};

class FakeProvider final : public wiola::media::MediaProvider {
public:
    explicit FakeProvider(wiola::testing::MetadataReadGate* gate = nullptr)
        : gate_{gate}
    {
    }

    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const wiola::media::ResourceReference& resource,
        const wiola::media::OperationContext& context) override
    {
        const auto path{wiola::media::local_path(resource).value()};
        if (gate_ && path == "barrier.wav")
            gate_->arrive(context.stop);
        if (path == "throws.wav")
            throw std::runtime_error{"injected read failure"};
        if (path == "error.wav") {
            return wiola::media::unexpected{
                wiola::media::Error{wiola::media::ErrorCategory::unreadable, "test"}
            };
        }

        return std::unique_ptr<wiola::media::MediaAsset>{
            std::make_unique<FakeAsset>(path.filename().string())};
    }

    std::vector<std::string> playback_extensions() const override { return {".wav"}; }

private:
    wiola::testing::MetadataReadGate* gate_{};
};

bool pump_until(std::function<bool()> predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    return predicate();
}

TEST(MediaRevisionPublisher, PublishesOnlyTheNewestRequestForEachIdentity)
{
    std::vector<wiola::app::BackgroundMetadataResult> results;
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(),
        std::make_shared<FakeProvider>(), [&](wiola::app::BackgroundMetadataResult result) {
            EXPECT_EQ(QThread::currentThread(), QCoreApplication::instance()->thread());
            results.push_back(std::move(result));
        }};

    const wiola::library::MediaId media{"media"};
    ASSERT_TRUE(publisher.enqueue(media, "old.wav"));
    ASSERT_TRUE(publisher.enqueue(media, "new.wav"));

    ASSERT_TRUE(pump_until([&] { return !results.empty(); }));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().location, "new.wav");
    ASSERT_TRUE(results.front().metadata.has_value());
    EXPECT_EQ(results.front().metadata->track.title, "new.wav");
}

TEST(MediaRevisionPublisher, AQueuedOldResultCannotPublishAfterANewerRequest)
{
    wiola::testing::MetadataReadGate gate;
    std::vector<wiola::app::BackgroundMetadataResult> results;
    const wiola::library::MediaId target{"target"};
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(),
        std::make_shared<FakeProvider>(&gate), [&](auto result) {
            if (result.media == target)
                results.push_back(std::move(result));
        }};
    ASSERT_TRUE(publisher.enqueue(target, "old.wav"));
    ASSERT_TRUE(publisher.enqueue(wiola::library::MediaId{"barrier"}, "barrier.wav"));
    // The same worker has delivered old.wav before entering the following I/O request.
    // Waiting without pumping owner events keeps that old result queued deterministically.
    ASSERT_TRUE(gate.wait_entered());
    EXPECT_TRUE(results.empty());
    ASSERT_TRUE(publisher.enqueue(target, "new.wav"));
    gate.release();
    ASSERT_TRUE(pump_until([&] { return !results.empty(); }));
    publisher.stop();
    QCoreApplication::processEvents();
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().location, "new.wav");
    ASSERT_TRUE(results.front().metadata);
    EXPECT_EQ(results.front().metadata->track.title, "new.wav");
}

TEST(MediaRevisionPublisher, DeliversErrorsWithoutInventingMetadata)
{
    std::optional<wiola::app::BackgroundMetadataResult> result;
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(),
        std::make_shared<FakeProvider>(),
        [&](wiola::app::BackgroundMetadataResult incoming) { result = std::move(incoming); }};

    ASSERT_TRUE(publisher.enqueue(wiola::library::MediaId{"media"}, "error.wav"));
    ASSERT_TRUE(pump_until([&] { return result.has_value(); }));

    EXPECT_EQ(result->location, "error.wav");
    EXPECT_TRUE(result->error.has_value());
    EXPECT_FALSE(result->metadata.has_value());
}

TEST(MediaRevisionPublisher, DestructionDropsAlreadyQueuedResults)
{
    int publications = 0;
    auto publisher =
        std::make_unique<wiola::app::MediaRevisionPublisher>(*QCoreApplication::instance(),
            std::make_shared<FakeProvider>(),
            [&](wiola::app::BackgroundMetadataResult) { ++publications; });

    ASSERT_TRUE(publisher->enqueue(wiola::library::MediaId{"media"}, "track.wav"));
    publisher.reset();

    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(publications, 0);
}

TEST(MediaRevisionPublisher, EnqueueRejectionUsesQueuedOwnerDelivery)
{
    std::optional<wiola::app::BackgroundMetadataResult> result;
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(),
        std::make_shared<FakeProvider>(), [&](auto value) {
            EXPECT_EQ(QThread::currentThread(), QCoreApplication::instance()->thread());
            result = std::move(value);
        }};
    publisher.stop();
    EXPECT_FALSE(publisher.enqueue(wiola::library::MediaId{"rejected"}, "track.wav"));
    EXPECT_FALSE(result);
    ASSERT_TRUE(pump_until([&] { return result.has_value(); }));
    ASSERT_TRUE(result->error);
    EXPECT_EQ(result->error->category, wiola::media::ErrorCategory::limit_exceeded);
    EXPECT_FALSE(result->metadata);
    EXPECT_EQ(result->location, "track.wav");
}

TEST(MediaRevisionPublisher, WorkerIoExceptionsBecomeTypedFailures)
{
    std::optional<wiola::app::BackgroundMetadataResult> result;
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(),
        std::make_shared<FakeProvider>(), [&](auto value) { result = std::move(value); }};
    ASSERT_TRUE(publisher.enqueue(wiola::library::MediaId{"exception"}, "throws.wav"));
    ASSERT_TRUE(pump_until([&] { return result.has_value(); }));
    ASSERT_TRUE(result->error);
    EXPECT_EQ(result->error->category, wiola::media::ErrorCategory::read_failed);
    EXPECT_EQ(result->error->detail, "injected read failure");
}

class CancellableProvider final : public wiola::media::MediaProvider {
public:
    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>> open(
        const wiola::media::ResourceReference&, const wiola::media::OperationContext& context
    ) override
    {
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        std::stop_callback wake{context.stop, [this] { changed.notify_all(); }};
        changed.wait(lock, [&] { return context.stop.stop_requested(); });
        return wiola::media::unexpected{context.error()};
    }

    std::vector<std::string> playback_extensions() const override { return {}; }

    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};
};

TEST(MediaRevisionPublisher, BoundedQueueReportsRejectionAndGatedShutdownCancelsWithoutDelivery)
{
    auto provider = std::make_shared<CancellableProvider>();
    std::vector<wiola::app::BackgroundMetadataResult> results;
    wiola::app::MediaRevisionPublisher publisher{*QCoreApplication::instance(), provider,
        [&](auto result) { results.push_back(std::move(result)); }};
    ASSERT_TRUE(publisher.enqueue(wiola::library::MediaId{"active"}, "gated.wav"));
    {
        std::unique_lock lock{provider->mutex};
        ASSERT_TRUE(provider->changed.wait_for(lock, std::chrono::seconds{5},
            [&] { return provider->entered; }));
    }
    for (int i = 0; i < 256; ++i)
        ASSERT_TRUE(publisher.enqueue(wiola::library::MediaId{"queued-" + std::to_string(i)},
            "queued.wav"));
    EXPECT_FALSE(publisher.enqueue(wiola::library::MediaId{"overflow"}, "overflow.wav"));
    EXPECT_TRUE(results.empty());
    ASSERT_TRUE(pump_until([&] { return !results.empty(); }));
    ASSERT_EQ(results.size(), 1);
    ASSERT_TRUE(results.front().error);
    EXPECT_EQ(results.front().error->category, wiola::media::ErrorCategory::limit_exceeded);
    publisher.stop();
    QCoreApplication::processEvents();
    EXPECT_EQ(results.size(), 1);
}

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
