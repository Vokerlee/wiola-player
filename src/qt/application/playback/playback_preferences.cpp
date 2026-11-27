/**
 * @file
 * @brief Application-owned playback preferences and one-time restoration.
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

#include "playback_preferences.hpp"

#include <wiola/qt/storage/settings.hpp>

namespace wiola::app {

PlaybackPreferences::PlaybackPreferences(PlaybackService& playback)
    : settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , settings_adapter_{settings_}
    , ui_session_{settings_}
    , queue_{playback, settings_adapter_,
          [](const auto& path) {
              std::error_code error;
              return std::filesystem::exists(path, error) && !error;
          }}
    , volume_{playback, settings_adapter_}
    , equalizer_{playback, settings_adapter_}
{
    const auto restored = platform::qt::QtSettingsAdapter{settings_}.read(volume_.scale(),
        playback.equalizer_config());
    volume_.apply_restored(restored.volume_position, restored.boosted);
    equalizer_.apply_restored(restored.equalizer_enabled, restored.band_gains);
    missing_ = queue_.restore();
    ui_session_.remap_queue(queue_.restore_mapping());
}

bool PlaybackPreferences::flush()
{
    queue_.save();
    settings_.sync();

    const auto ui_saved = ui_session_.flush();

    return settings_.status() == QSettings::NoError && ui_saved == SessionStateError::none;
}

} // namespace wiola::app
