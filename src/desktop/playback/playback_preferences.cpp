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
#include <platform/qt/player_settings.hpp>

namespace wiola::app {

PlaybackPreferences::PlaybackPreferences(PlaybackService& playback)
    : settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , queue_{playback, settings_}
    , volume_{playback, settings_}
    , equalizer_{playback, settings_}
{
    const auto restored = platform::qt::QtSettingsAdapter{settings_}.read(volume_.scale(),
        playback.equalizer_config());
    volume_.apply_restored(restored.volume_position, restored.boosted);
    equalizer_.apply_restored(restored.equalizer_enabled, restored.band_gains);
    missing_ = queue_.restore();
}

bool PlaybackPreferences::flush()
{
    queue_.save();
    settings_.sync();
    return settings_.status() == QSettings::NoError;
}

} // namespace wiola::app
