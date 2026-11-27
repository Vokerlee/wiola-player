/**
 * @file
 * @brief Value, error and ownership contracts for media results.
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

#include <wiola/media/result.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace {

using wiola::media::Error;
using wiola::media::ErrorCategory;
using wiola::media::Result;
using wiola::media::unexpected;

TEST(MediaResult, ValueAccessorsReferToTheSameStoredObject)
{
    Result<std::string> result{std::string{"track"}};

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), "track");
    EXPECT_EQ(&*result, &result.value());
    EXPECT_EQ(result.operator->(), &result.value());

    result->append(" title");

    const auto& view = std::as_const(result);

    EXPECT_EQ(view.value(), "track title");
    EXPECT_EQ(*view, "track title");
    EXPECT_EQ(view->size(), 11U);
    EXPECT_EQ(view.operator->(), &view.value());
}

TEST(MediaResult, TransfersAMoveOnlyValueWithoutCopyingOrLosingOwnership)
{
    auto original = std::make_unique<int>(42);
    const int* identity = original.get();
    Result<std::unique_ptr<int>> result{std::move(original)};

    EXPECT_FALSE(original);
    ASSERT_TRUE(result);
    ASSERT_TRUE(result.value());
    EXPECT_EQ(result.value().get(), identity);

    auto transferred = std::move(result).value();

    ASSERT_TRUE(transferred);
    EXPECT_EQ(transferred.get(), identity);
    EXPECT_EQ(*transferred, 42);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(result.value());
}

TEST(MediaResult, ErrorAccessorsPreserveTheDiagnosticAndReferToTheSameError)
{
    Result<int> result{
        unexpected{Error{.category = ErrorCategory::damaged, .detail = "truncated frame"}}};

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error().category, ErrorCategory::damaged);

    result.error().detail += " at end";

    const auto& view = std::as_const(result);

    EXPECT_EQ(view.error().detail, "truncated frame at end");
    EXPECT_EQ(&view.error(), &result.error());
    EXPECT_THROW(result.value(), std::bad_variant_access);
    EXPECT_THROW(view.value(), std::bad_variant_access);
    EXPECT_THROW(std::move(result).value(), std::bad_variant_access);
    EXPECT_EQ(result.error().detail, "truncated frame at end");
}

TEST(MediaResult, ErrorAccessOnASuccessThrowsWithoutChangingTheValue)
{
    Result<int> result{7};
    const auto& view = std::as_const(result);

    EXPECT_THROW(result.error(), std::bad_variant_access);
    EXPECT_THROW(view.error(), std::bad_variant_access);
    EXPECT_EQ(result.value(), 7);
}

TEST(MediaResult, VoidSuccessSupportsValueAccessWithoutAnError)
{
    const Result<void> result;

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_NO_THROW(result.value());
}

TEST(MediaResult, VoidFailurePreservesItsErrorAndRejectsValueAccess)
{
    Result<void> result{
        unexpected{Error{.category = ErrorCategory::read_failed, .detail = "disconnected"}}};

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error().category, ErrorCategory::read_failed);

    result.error().detail = "retry required";

    const auto& view = std::as_const(result);

    EXPECT_EQ(&view.error(), &result.error());
    EXPECT_EQ(view.error().detail, "retry required");
    EXPECT_THROW(view.value(), std::bad_variant_access);
    EXPECT_EQ(result.error().category, ErrorCategory::read_failed);
}

} // namespace
