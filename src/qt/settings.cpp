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

#include "settings.hpp"
#include <set>

#include "paths.hpp"

#include <QString>
#include <QStringList>

#include <cmath>
#include <cstddef>

namespace wiola::platform::qt {

namespace {

constexpr auto position_key{"volume"};
constexpr auto boosted_key{"volume/boosted"};
constexpr auto enabled_key{"equalizer/enabled"};

QString band_key(std::size_t index)
{
    return QString{"equalizer/band/%1"}.arg(index);
}

float stored_gain(const QSettings& settings, std::size_t index) noexcept
{
    bool numeric{false};
    const float gain{settings.value(band_key(index), 0.0F).toFloat(&numeric)};
    return numeric && std::isfinite(gain) ? gain : 0.0F;
}

} // namespace

QtSettingsAdapter::QtSettingsAdapter(QSettings& settings) noexcept
    : settings_{settings}
{
}

PlayerSettings QtSettingsAdapter::read(const app::VolumeScale& volume,
    const audio::EqualizerConfig& equalizer) const
{
    PlayerSettings settings;
    settings.volume_position = volume.full_position;
    settings.band_gains.reserve(equalizer.layout.count);

    bool numeric{false};
    const int position{settings_.value(position_key, volume.full_position).toInt(&numeric)};
    settings.volume_position = numeric ? position : volume.full_position;
    settings.boosted = settings_.value(boosted_key, false).toBool();
    settings.equalizer_enabled = settings_.value(enabled_key, true).toBool();

    for (std::size_t index = 0; index < equalizer.layout.count; ++index)
        settings.band_gains.push_back(stored_gain(settings_, index));

    return settings;
}

PlaylistSettings QtSettingsAdapter::read_playlist() const
{
    PlaylistSettings settings;
    const QStringList tracks{settings_.value("queue").toStringList()};
    settings.tracks.reserve(static_cast<std::size_t>(tracks.size()));
    for (const QString& track : tracks)
        settings.tracks.push_back(platform::qt::QtPathAdapter::from_qt(track));

    const QString selected{settings_.value("queue/path").toString()};
    if (!selected.isEmpty())
        settings.selected = platform::qt::QtPathAdapter::from_qt(selected);
    settings.repeat = settings_.value("queue/repeat", 0).toInt();
    settings.shuffled = settings_.value("queue/shuffled", false).toBool();
    settings.has_occurrence_ids = settings_.contains("queue/entry_ids");
    const auto ids = settings_.value("queue/entry_ids").toStringList();
    std::set<std::uint64_t> unique;
    if (ids.size() == tracks.size())
        for (const auto& id : ids) {
            bool valid = false;
            const auto token = id.toULongLong(&valid);
            if (!valid || !token || !unique.insert(token).second) {
                settings.occurrence_ids.clear();
                break;
            }
            settings.occurrence_ids.push_back(token);
        }
    bool valid = false;
    const auto selected_id = settings_.value("queue/selected_id").toString().toULongLong(&valid);
    if (valid && selected_id && unique.contains(selected_id) &&
        settings.occurrence_ids.size() == settings.tracks.size())
        settings.selected_occurrence = selected_id;

    return settings;
}

void QtSettingsAdapter::write_playlist(const PlaylistSettings& settings)
{
    QStringList tracks;
    for (const std::filesystem::path& track : settings.tracks)
        tracks.append(platform::qt::QtPathAdapter::to_qt(track));

    settings_.setValue("queue", tracks);
    settings_.setValue("queue/path",
        settings.selected ? platform::qt::QtPathAdapter::to_qt(*settings.selected) : QString{});
    settings_.setValue("queue/repeat", settings.repeat);
    settings_.setValue("queue/shuffled", settings.shuffled);
    if (settings.has_occurrence_ids || !settings.occurrence_ids.empty()) {
        QStringList ids;
        for (const auto id : settings.occurrence_ids)
            ids.push_back(QString::number(id));
        settings_.setValue("queue/entry_ids", ids);
        settings_.setValue("queue/selected_id",
            settings.selected_occurrence ? QString::number(*settings.selected_occurrence)
                                         : QString{});
    } else {
        settings_.remove("queue/entry_ids");
        settings_.remove("queue/selected_id");
    }
}

} // namespace wiola::platform::qt
