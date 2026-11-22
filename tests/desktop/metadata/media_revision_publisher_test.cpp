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

#include <desktop/metadata/media_revision_publisher.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <gtest/gtest.h>

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

    wiola::media::Result<std::unique_ptr<wiola::codec::Decoder>>
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
    wiola::media::Result<std::unique_ptr<wiola::media::MediaAsset>>
    open(const std::filesystem::path& path, const wiola::media::OperationContext&) override
    {
        if (path == "error.wav") {
            return wiola::media::unexpected{
                wiola::media::Error{wiola::media::ErrorCategory::unreadable, "test"}
            };
        }

        return std::unique_ptr<wiola::media::MediaAsset>{
            std::make_unique<FakeAsset>(path.filename().string())};
    }

    std::vector<std::string> playback_extensions() const override { return {".wav"}; }
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

} // namespace

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};

    return RUN_ALL_TESTS();
}
