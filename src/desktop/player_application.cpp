/**
 * @file
 * @brief Desktop runtime composition, observation, and explicit shutdown.
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

#include "player_application.hpp"
#include <audio/device/device.hpp>
#include <codec/provider.hpp>
#include <stdexcept>

namespace wiola::app {
namespace {

PlayerRuntimeConfig defaults()
{
    PlayerRuntimeConfig config;
    config.provider = std::make_shared<codec::LocalMediaProvider>();
    config.output = [](audio::Source& source) {
        return std::make_unique<audio::Device>(source);
    };
    return config;
}

std::shared_ptr<media::MediaProvider> checked_provider(const PlayerRuntimeConfig& config)
{
    if (!config.provider || !config.output || config.pump_interval.count() <= 0 ||
        config.presentation_interval.count() <= 0)
        throw std::invalid_argument{"invalid desktop runtime dependencies or cadence"};
    return config.provider;
}

} // namespace

PlayerApplication::PlayerApplication(QObject* parent)
    : PlayerApplication{defaults(), parent}
{
}

PlayerApplication::PlayerApplication(PlayerRuntimeConfig config, QObject* parent)
    : QObject{parent}
    , playback_{config.output, checked_provider(config)}
    , library_{config.library_file.empty() ? default_library_file() : config.library_file,
          config.provider, nullptr, std::move(config.library_output)}
{
    preferences_.ui_session().bind_library(library_);
    library_.file_writes().set_handoff(
        [this](const std::filesystem::path& path) -> MetadataWriteService::Restore {
            auto state = playback_.release_media(path);
            if (!state)
                return {};
            return [this, state = *state] {
                playback_.restore_media(state);
            };
        }
    );
    connect(&pump_timer_, &QTimer::timeout, this, [this] {
        static_cast<void>(playback_.pump());
        emit pumped(present_playback(playback_));
    });
    connect(&refresh_timer_, &QTimer::timeout, this, &PlayerApplication::refresh_requested);
    pump_timer_.start(config.pump_interval);
    refresh_timer_.start(config.presentation_interval);
}

PlayerApplication::~PlayerApplication()
{
    static_cast<void>(shutdown());
}

void PlayerApplication::observe(QObject& receiver,
    std::function<void(const PlayerPresentation&)> playback, std::function<void()> refresh)
{
    Q_ASSERT(receiver.thread() == thread());
    connect(this, &PlayerApplication::pumped, &receiver, std::move(playback));
    connect(this, &PlayerApplication::refresh_requested, &receiver, std::move(refresh));
}

bool PlayerApplication::shutdown()
{
    pump_timer_.stop();
    refresh_timer_.stop();
    playback_.stop_accepting();
    const auto library_result = library_.shutdown();
    const bool preferences_saved = preferences_.flush();
    return library_result.succeeded() && preferences_saved;
}

} // namespace wiola::app
