/**
 * @file
 * @brief One normalized search, filter, and sort state for each track browser.
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

#include "track_column.hpp"
#include <gui/appearance/presentation_tokens.hpp>
#include <library/tags.hpp>

namespace wiola::gui {

const std::array<TrackColumnDescriptor, num_columns>& standard_track_columns()
{
    static const PresentationTokens t;
    static const std::array<TrackColumnDescriptor, num_columns> columns{
        {{cover, "", t.playlist.artwork.cover_size + t.playlist.row_padding, QHeaderView::Fixed, {},
             Qt::DecorationRole},
         {title, "Title", 200, QHeaderView::Stretch,
                TrackSortKey{.field = TrackSortField::title}},
         {artist, "Artist", t.playlist.artist_column_width, QHeaderView::Interactive,
                TrackSortKey{.field = TrackSortField::artist}},
         {album, "Album", t.playlist.album_column_width, QHeaderView::Interactive,
                TrackSortKey{.field = TrackSortField::album}},
         {length, "Length", t.playlist.length_column_width, QHeaderView::Fixed,
                TrackSortKey{.field = TrackSortField::duration}}}
    };
    return columns;
}

TrackColumnDescriptor tag_track_column(std::string label)
{
    const auto key = library::normalized_tag_text(label);
    return {CustomTagColumn{key}, std::move(label), 120, QHeaderView::Interactive,
        TrackSortKey::for_tag(key)};
}

} // namespace wiola::gui
