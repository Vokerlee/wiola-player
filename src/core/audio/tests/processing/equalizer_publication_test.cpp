/**
 * @file
 * @brief Contract tests for prepared-bank ownership and publication.
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

#include <processing/equalizer_publication.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <thread>

namespace {

using wiola::audio::EqualizerPublication;

EqualizerPublication::Bank prepared(std::size_t generation)
{
    EqualizerPublication::Bank bank;
    const auto marker = static_cast<float>(generation);
    bank.preamp = marker;
    bank.num_bands = generation % wiola::audio::BandLayout::max_count;
    bank.enabled = generation % 2 == 0;
    bank.shaping = generation % 3 == 0;
    for (auto& coefficient : bank.coefficients)
        coefficient = {.b0 = marker, .b1 = marker, .b2 = marker, .a1 = marker, .a2 = marker};
    return bank;
}

bool coherent(const EqualizerPublication::Bank& bank)
{
    if (!std::isfinite(bank.preamp) || bank.preamp < 0.0F)
        return false;
    return static_cast<float>(bank.num_bands) ==
        std::fmod(bank.preamp, static_cast<float>(wiola::audio::BandLayout::max_count)) &&
        bank.enabled == (std::fmod(bank.preamp, 2.0F) == 0.0F) &&
        bank.shaping == (std::fmod(bank.preamp, 3.0F) == 0.0F) &&
        std::all_of(bank.coefficients.begin(), bank.coefficients.end(), [&](const auto& value) {
            return value.b0 == bank.preamp && value.b1 == bank.preamp && value.b2 == bank.preamp &&
                value.a1 == bank.preamp && value.a2 == bank.preamp;
        });
}

} // namespace

TEST(EqualizerPublication, StartsWithAnEmptyFlatBank)
{
    EqualizerPublication publication;
    const auto& bank = publication.acquire();
    EXPECT_EQ(bank.num_bands, 0U);
    EXPECT_FALSE(bank.shaping);
    EXPECT_TRUE(bank.enabled);
    EXPECT_FLOAT_EQ(bank.preamp, 1.0F);
}

TEST(EqualizerPublication, CoalescesPendingBanksAndRetainsTheLastReadWithoutUpdates)
{
    EqualizerPublication publication;
    publication.publish(prepared(1));
    publication.publish(prepared(2));
    publication.publish(prepared(3));
    const auto& latest = publication.acquire();
    EXPECT_FLOAT_EQ(latest.preamp, 3.0F);
    EXPECT_TRUE(coherent(latest));
    EXPECT_FLOAT_EQ(publication.acquire().preamp, 3.0F);
}

TEST(EqualizerPublication, HeldBankSurvivesPublicationsUntilTheNextAcquire)
{
    EqualizerPublication publication;
    publication.publish(prepared(1));
    const auto& held = publication.acquire();
    for (std::size_t generation = 2; generation <= 16; ++generation)
        publication.publish(prepared(generation));
    EXPECT_FLOAT_EQ(held.preamp, 1.0F);
    EXPECT_TRUE(coherent(held));
    const auto& latest = publication.acquire();
    EXPECT_FLOAT_EQ(latest.preamp, 16.0F);
    EXPECT_TRUE(coherent(latest));
}

TEST(EqualizerPublication, ConcurrentAcquisitionNeverTearsOrRegressesAndAdoptsTheFinalBank)
{
    constexpr std::size_t generations{20000};
    EqualizerPublication publication;
    publication.publish(prepared(1));
    static_cast<void>(publication.acquire());
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::jthread writer{[&] {
        start.wait(false, std::memory_order_acquire);
        for (std::size_t generation = 2; generation <= generations; ++generation)
            publication.publish(prepared(generation));
        done.store(true, std::memory_order_release);
    }};
    bool intact{true};
    float last{1.0F};
    start.store(true, std::memory_order_release);
    start.notify_one();
    do {
        const auto& bank = publication.acquire();
        intact = intact && coherent(bank) && bank.preamp >= last;
        last = bank.preamp;
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    const auto& final = publication.acquire();
    EXPECT_TRUE(intact);
    EXPECT_TRUE(coherent(final));
    EXPECT_FLOAT_EQ(final.preamp, static_cast<float>(generations));
}
