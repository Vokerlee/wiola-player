/**
 * @file
 * @brief Controlled stream facts and lifecycle helpers for Session contracts.
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

#pragma once

#include <fakes/decoder.hpp>
#include <fakes/output.hpp>
#include <fakes/provider.hpp>
#include <fixtures/shaping.hpp>
#include <wiola/playback/session.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace wiola::testing::session {

using engine::OpenResult;
using engine::Session;
namespace units = wiola::units;

/// Loads and waits for it, the way a window that draws does: asking, then taking the answer up
/// on a later turn.
inline OpenResult load_and_wait(Session& session, const std::filesystem::path& path)
{
    session.open({path});

    while (session.reading())
        std::this_thread::yield();

    session.catch_up();

    return session.open_result();
}

/// Turns the session over until `settled` says it has caught up, the way a window that draws
/// does. Fails rather than hangs when it never does.
template<typename Predicate>
bool poll_until(Session& session, Predicate settled)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};

    while (std::chrono::steady_clock::now() < deadline) {
        session.catch_up();

        if (settled())
            return true;

        std::this_thread::yield();
    }

    return settled();
}

/// Supplies decoded-stream facts, not encoded files. The store outlives Session's owned workers.
struct SessionFixture {
private:
    mutable std::mutex media_mutex_;
    std::map<std::filesystem::path, wiola::media::DecoderStreamInfo> media_;
    std::size_t next_resource_{};

    std::optional<wiola::media::DecoderStreamInfo> lookup(const std::filesystem::path& path) const
    {
        const std::lock_guard lock{media_mutex_};
        const auto found{media_.find(path)};
        return found == media_.end() ? std::nullopt : std::optional{found->second};
    }

    static wiola::media::unexpected missing()
    {
        return wiola::media::unexpected{
            wiola::media::Error{wiola::media::ErrorCategory::unreadable,
                                "controlled resource was not supplied"}
        };
    }

public:
    // Longer than priming and the seek positions exercised by these controlled cases.
    static constexpr units::Time duration{std::chrono::milliseconds{1500}};

    SessionFixture()
        : session{processing.chain,
              [this](wiola::audio::Source& source) {
                  ++num_outputs;
                  return consuming ? std::make_unique<wiola::testing::FakeOutput>(source)
                                   : std::make_unique<wiola::testing::FakeOutput>();
              },
              std::make_shared<wiola::testing::FakeMediaProvider>(
                  [this](const std::filesystem::path& path,
                      const wiola::media::OperationContext&) -> wiola::media::DecoderOpenResult {
                      const auto info{lookup(path)};
                      if (!info)
                          return missing();
                      return wiola::media::DecoderOpenResult{
                          std::make_unique<wiola::testing::FakeDecoder>(*info)};
                  },
                  [this](const std::filesystem::path& path, const wiola::media::OperationContext&)
                      -> wiola::media::Result<wiola::media::Metadata> {
                      const auto info{lookup(path)};
                      if (!info)
                          return missing();
                      wiola::media::Metadata result;
                      result.track.duration = info->spec.time_per(info->length.value());
                      return result;
                  }
              )}
    {
    }

    std::filesystem::path track(wiola::pcm::StreamSpec spec = {})
    {
        const std::filesystem::path path{"resource-" + std::to_string(++next_resource_) + ".media"};
        const wiola::media::DecoderStreamInfo info{.spec = spec,
            .length = spec.checked_frames_at(duration).value(),
            .length_accuracy = wiola::media::LengthAccuracy::exact,
            .seekable = true};
        const std::lock_guard lock{media_mutex_};
        media_.emplace(path, info);
        return path;
    }

    /// Set before loading, when a test needs a track that cannot run out from under it.
    bool consuming{true};
    std::size_t num_outputs{0};
    wiola::testing::Shaping processing{wiola::pcm::StreamSpec{}};
    Session session;
};

} // namespace wiola::testing::session
