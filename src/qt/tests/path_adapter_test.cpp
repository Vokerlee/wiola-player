/**
 * @file
 * @brief Native Unicode path conversion tests.
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

#include <wiola/qt/paths.hpp>

#include <gtest/gtest.h>

TEST(QtPathAdapter, PreservesNonAsciiPathsInBothDirections)
{
    const std::filesystem::path path{wiola::platform::qt::QtPathAdapter::from_qt(
        QString::fromUtf8("music/Романтика-音楽.flac")
    )};

    const QString converted{wiola::platform::qt::QtPathAdapter::to_qt(path)};
    EXPECT_EQ(converted, QString::fromUtf8("music/Романтика-音楽.flac"));
    EXPECT_EQ(wiola::platform::qt::QtPathAdapter::from_qt(converted), path);
}
