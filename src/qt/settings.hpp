/**
 * @file
 * @brief Typed persisted player settings and the Qt adapter that reads them.
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

#include <app/playback/playback_settings.hpp>
#include <app/playback/queue_settings_store.hpp>
#include <app/playback/volume_scale.hpp>
#include <wiola/audio/equalizer_config.hpp>

#include <QSettings>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace wiola::platform::qt {

/// Converts Qt's string-keyed storage into typed values, with defaults for invalid old data.
class QtSettingsAdapter final : public app::PlaybackSettingsWriter,
                                public app::QueueSettingsStore {
public:
    explicit QtSettingsAdapter(QSettings& settings) noexcept;

    /// Reads slider positions, switches, and per-band decibel gains for the supplied policies.
    [[nodiscard]] app::PlayerSettings read(const app::VolumeScale& volume,
        const audio::EqualizerConfig& equalizer) const;
    [[nodiscard]] app::QueueSnapshot read_queue() const override;
    void write_queue(const app::QueueSnapshot& settings) override;
    void write_volume_position(int position) override;
    void write_boosted(bool boosted) override;
    void write_equalizer_enabled(bool enabled) override;
    void write_band_gain(std::size_t index, float db) override;

private:
    QSettings& settings_;
};

} // namespace wiola::platform::qt
