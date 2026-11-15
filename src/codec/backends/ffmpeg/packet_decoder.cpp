/**
 * @file
 * @brief Private bounded FFmpeg packet submission, frame reception, and draining.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "packet_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace wiola::codec::ffmpeg {

namespace {

constexpr int max_work_packets{1024};

media::Error decode_error(const char* detail)
{
    return media::Error{.category = media::ErrorCategory::damaged, .detail = detail};
}

} // namespace

PacketDecoder::PacketDecoder(FfmpegInput& input, std::size_t stream_index) noexcept
    : input_{input}
    , stream_index_{stream_index}
{
}

media::Result<std::unique_ptr<PacketDecoder>> PacketDecoder::open(FfmpegInput& input,
    const FfmpegStreamFacts& facts)
{
    auto decoder{std::unique_ptr<PacketDecoder>{new PacketDecoder{input, facts.stream_index}}};
    auto initialized{decoder->initialize()};
    if (!initialized)
        return media::unexpected{initialized.error()};
    return decoder;
}

media::Result<void> PacketDecoder::initialize()
{
    const AVStream* stream{input_.format()->streams[stream_index_]};
    const AVCodec* selected{avcodec_find_decoder(stream->codecpar->codec_id)};
    if (!selected)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::unsupported,
                         .detail = "FFmpeg decoder is unavailable"}
        };

    codec_ = avcodec_alloc_context3(selected);
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!codec_ || !packet_ || !frame_)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::limit_exceeded,
                         .detail = "FFmpeg decoder allocation failed"}
        };

    codec_->thread_count = 1;
    if (avcodec_parameters_to_context(codec_, stream->codecpar) < 0)
        return media::unexpected{decode_error("FFmpeg codec parameters are invalid")};
    if (avcodec_open2(codec_, selected, nullptr) < 0)
        return media::unexpected{decode_error("FFmpeg decoder could not be opened")};
    return {};
}

PacketDecoder::~PacketDecoder()
{
    if (frame_)
        av_frame_free(&frame_);
    if (packet_)
        av_packet_free(&packet_);
    if (codec_)
        avcodec_free_context(&codec_);
}

media::Result<FfmpegFrameEvent> PacketDecoder::error(media::Error error)
{
    return media::unexpected{std::move(error)};
}

void PacketDecoder::reset_after_seek() noexcept
{
    avcodec_flush_buffers(codec_);
    av_packet_unref(packet_);
    av_frame_unref(frame_);
    packet_pending_ = false;
    demux_ended_ = false;
    drain_sent_ = false;
    ended_ = false;
}

media::Result<FfmpegFrameEvent> PacketDecoder::next_frame()
{
    if (ended_)
        return FfmpegFrameEvent{.frame = nullptr, .end_of_stream = true};

    for (int work{0}; work < max_work_packets; ++work) {
        const int received{avcodec_receive_frame(codec_, frame_)};
        if (received >= 0)
            return FfmpegFrameEvent{.frame = frame_, .end_of_stream = false};
        if (received == AVERROR_EOF) {
            ended_ = true;
            return FfmpegFrameEvent{.frame = nullptr, .end_of_stream = true};
        }
        if (received != AVERROR(EAGAIN))
            return error(decode_error("FFmpeg frame reception failed"));

        if (packet_pending_) {
            const int sent{avcodec_send_packet(codec_, packet_)};
            if (sent == AVERROR(EAGAIN))
                return error(decode_error("FFmpeg packet/frame pump is stalled"));
            if (sent < 0)
                return error(decode_error("FFmpeg packet submission failed"));
            packet_pending_ = false;
            av_packet_unref(packet_);
            continue;
        }

        if (!demux_ended_) {
            const int read{av_read_frame(input_.format(), packet_)};
            if (read >= 0) {
                if (packet_->stream_index == static_cast<int>(stream_index_))
                    packet_pending_ = true;
                else
                    av_packet_unref(packet_);
                continue;
            }
            if (read != AVERROR_EOF) {
                if (input_.io_error())
                    return error(*input_.io_error());
                return error(decode_error("FFmpeg packet demuxing failed"));
            }
            demux_ended_ = true;
        }

        if (!drain_sent_) {
            const int sent{avcodec_send_packet(codec_, nullptr)};
            if (sent == AVERROR(EAGAIN))
                continue;
            if (sent < 0)
                return error(decode_error("FFmpeg decoder drain failed"));
            drain_sent_ = true;
        }
    }

    return media::unexpected{
        media::Error{.category = media::ErrorCategory::limit_exceeded,
                     .detail = "FFmpeg decoding exceeded its packet work budget"}
    };
}

} // namespace wiola::codec::ffmpeg
