#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <wiola/media/provider.hpp>

namespace wiola::testing {

struct MetadataReadGate {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> claimed{};
    std::atomic<int> returned{};
    bool entered{}, released{};

    bool wait_entered()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds{5}, [&] { return entered; });
    }

    void release()
    {
        std::lock_guard lock{mutex};
        released = true;
        changed.notify_all();
    }

    void arrive(std::stop_token stop)
    {
        std::stop_callback wake{stop, [&] { changed.notify_all(); }};
        std::unique_lock lock{mutex};
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released || stop.stop_requested(); });
    }

    ~MetadataReadGate() { release(); }
};

class GatedMediaAsset final : public media::MediaAsset {
public:
    GatedMediaAsset(std::unique_ptr<media::MediaAsset> delegate, MetadataReadGate& gate)
        : delegate_{std::move(delegate)}
        , gate_{gate}
    {
    }

    media::Result<media::MediaInfo> inspect(const media::OperationContext& context) override
    {
        return delegate_->inspect(context);
    }

    media::Result<media::Metadata> read_metadata(const media::OperationContext& context) override
    {
        auto result = delegate_->read_metadata(context); // Capture the old bytes before the gate.
        if (!gate_.claimed.exchange(true))
            gate_.arrive(context.stop);
        ++gate_.returned;
        return result;
    }

    media::MetadataWriteCapabilities metadata_write_capabilities() const override
    {
        return delegate_->metadata_write_capabilities();
    }

    media::MetadataWriteOutcome write_metadata(const media::MetadataWriteRequest& request,
        const media::OperationContext& context) override
    {
        return delegate_->write_metadata(request, context);
    }

    media::Result<std::unique_ptr<media::AudioDecoder>>
    create_decoder(const media::OperationContext& context) override
    {
        return delegate_->create_decoder(context);
    }

private:
    std::unique_ptr<media::MediaAsset> delegate_;
    MetadataReadGate& gate_;
};

class GatedMediaProvider final : public media::MediaProvider {
public:
    GatedMediaProvider(std::shared_ptr<media::MediaProvider> delegate, MetadataReadGate& gate)
        : delegate_{std::move(delegate)}
        , gate_{gate}
    {
    }

    media::Result<std::unique_ptr<media::MediaAsset>> open(const media::ResourceReference& resource,
        const media::OperationContext& context) override
    {
        auto opened = delegate_->open(resource, context);
        if (!opened)
            return media::unexpected{opened.error()};
        return std::unique_ptr<media::MediaAsset>{
            std::make_unique<GatedMediaAsset>(std::move(*opened), gate_)};
    }

    std::vector<std::string> playback_extensions() const override
    {
        return delegate_->playback_extensions();
    }

private:
    std::shared_ptr<media::MediaProvider> delegate_;
    MetadataReadGate& gate_;
};

} // namespace wiola::testing
