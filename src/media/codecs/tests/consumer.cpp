/**
 * @file
 * @brief Public FFmpeg adapter consumer without private headers or Qt.
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

#include <wiola/media/codecs/ffmpeg/composition.hpp>
#include <wiola/media/resource.hpp>

#include <stop_token>

#if defined(QT_CORE_LIB) || defined(QT_GUI_LIB) || defined(QT_WIDGETS_LIB)
#error The media adapter consumer must not depend on Qt.
#endif

int main()
{
    auto provider = wiola::codec::ffmpeg::make_provider();

    if (provider->playback_extensions().empty())
        return 1;

    std::stop_source cancellation;
    cancellation.request_stop();

    const auto opened =
        provider->open(wiola::media::ResourceReference{"controlled", "track", "Track"},
            {.stop = cancellation.get_token(), .deadline = {}});

    return !opened && opened.error().category == wiola::media::ErrorCategory::cancelled ? 0 : 1;
}
