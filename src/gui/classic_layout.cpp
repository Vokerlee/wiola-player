/**
 * @file
 * @brief Declarative description of the Classic player layout.
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

#include "classic_layout.hpp"

namespace wiola::gui {

LayoutDocument classic_layout_document()
{
    static const QByteArray json = R"({
        "version": 1,
        "root": {
            "id": "classicRoot",
            "type": "column",
            "children": [
                {
                    "id": "playbackGroup",
                    "type": "group",
                    "title": "Now playing",
                    "children": [
                        {"id": "trackLabel", "type": "status"},
                        {"id": "positionBar", "type": "transport"},
                        {
                            "id": "timeRow",
                            "type": "row",
                            "children": [
                                {"id": "elapsedTime", "type": "status"},
                                {"id": "totalDuration", "type": "status"}
                            ]
                        },
                        {
                            "id": "transportControls",
                            "type": "row",
                            "children": [
                                {"id": "previousTrack", "type": "button", "action": "previous"},
                                {"id": "playPause", "type": "button", "action": "play"},
                                {"id": "nextTrack", "type": "button", "action": "next"},
                                {"id": "stopPlayback", "type": "button", "action": "stop"},
                                {"id": "volumeSlider", "type": "volume"},
                                {"id": "volumeValue", "type": "status"},
                                {"id": "boostButton", "type": "button"}
                            ]
                        }
                    ]
                },
                {
                    "id": "libraryCommands",
                    "type": "row",
                    "children": [
                        {"id": "openTracks", "type": "button", "action": "open"},
                        {"id": "repeatQueue", "type": "button", "action": "repeat"},
                        {"id": "shuffleQueue", "type": "button", "action": "shuffle"},
                        {"id": "equalizer", "type": "button", "action": "equalizer"}
                    ]
                },
                {"id": "documentArea", "type": "stack"},
                {
                    "id": "queueCommandsGroup",
                    "type": "group",
                    "title": "Queue commands",
                    "children": [
                        {"id": "addTracks", "type": "button", "action": "add"},
                        {"id": "removeTracks", "type": "button", "action": "remove"},
                        {"id": "clearQueue", "type": "button", "action": "clear"}
                    ]
                },
                {"id": "playerStatus", "type": "status"}
            ]
        }
    })";

    LayoutDocument document;
    const auto error = read_layout_json(json, document);
    if (error != LayoutJsonError::none)
        return {};

    return document;
}

} // namespace wiola::gui
