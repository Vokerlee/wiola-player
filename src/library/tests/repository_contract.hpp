/**
 * @file
 * @brief Format-neutral access to an identified media resource.
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

#include <wiola/library/repository.hpp>

#include <gtest/gtest.h>

namespace wiola::testing {

/// Supply a fresh repository. Generated identities are opaque; only uniqueness is required.
inline void verify_repository_contract(library::LibraryRepository& repository)
{
    library::TrackRecord track;
    track.id = library::MediaId{"contract-track"};
    ASSERT_TRUE(repository.put_track(track));
    ASSERT_NE(repository.track(track.id), nullptr);

    const auto playlist = repository.create_playlist("Contract playlist");
    ASSERT_TRUE(playlist.valid());
    EXPECT_FALSE(repository.append_track(playlist, library::MediaId{"missing"}));
    const auto first = repository.append_track(playlist, track.id);
    const auto second = repository.append_track(playlist, track.id);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(*first, *second);
    ASSERT_NE(repository.playlist(playlist), nullptr);
    ASSERT_EQ(repository.playlist(playlist)->entries.size(), 2U);

    ASSERT_TRUE(repository.remove_entry(playlist, *first));
    EXPECT_FALSE(repository.remove_entry(playlist, *first));
    ASSERT_EQ(repository.playlist(playlist)->entries.size(), 1U);
    EXPECT_EQ(repository.playlist(playlist)->entries.front().id, *second);
    EXPECT_TRUE(repository.rename_playlist(playlist, "Renamed"));
    EXPECT_EQ(repository.playlist(playlist)->name, "Renamed");

    const auto tag = repository.add_tag(track.id, "Mood", "Calm");
    ASSERT_TRUE(tag.valid());
    EXPECT_FALSE(repository.add_tag(track.id, " mood ", " calm ").valid());
    EXPECT_TRUE(repository.remove_tag(track.id, tag));
    EXPECT_FALSE(repository.remove_tag(track.id, tag));

    EXPECT_TRUE(repository.delete_playlist(playlist));
    EXPECT_EQ(repository.playlist(playlist), nullptr);
    EXPECT_FALSE(repository.delete_playlist(playlist));
    EXPECT_NE(repository.track(track.id), nullptr);
}

} // namespace wiola::testing
