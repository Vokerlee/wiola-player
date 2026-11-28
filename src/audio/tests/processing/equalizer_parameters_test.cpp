/**
 * @file
 * @brief Equalizer parameter policy ownership contracts.
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

#include <processing/equalizer_parameters.hpp>

#include <gtest/gtest.h>

namespace {

using wiola::audio::EqualizerConfig;
using wiola::audio::EqualizerParameters;
using wiola::units::Frequency;

TEST(EqualizerParameters, RetainsPolicySuppliedAsATemporary)
{
    EqualizerParameters parameters{{}, EqualizerConfig{.max_band_gain_db = 3.0F}};

    parameters.set_band_gain(0, 12.0F);

    EXPECT_FLOAT_EQ(parameters.band_gain(0), 3.0F);
    EXPECT_FLOAT_EQ(parameters.preamp(), -3.0F);
    EXPECT_EQ(parameters.band_center(0), Frequency{31.25});
}

TEST(EqualizerParameters, CallerMutationsCannotChangeTheValidatedPolicy)
{
    EqualizerConfig config{
        .layout = {.first = Frequency{100.0}, .count = 3},
        .max_band_gain_db = 6.0F
    };
    EqualizerParameters parameters{{}, config};

    config.layout.first = Frequency{800.0};
    config.layout.count = 1;
    config.max_band_gain_db = 1.0F;

    parameters.set_band_gain(2, 4.0F);

    EXPECT_EQ(parameters.num_bands(), 3U);
    EXPECT_EQ(parameters.band_center(0), Frequency{100.0});
    EXPECT_FLOAT_EQ(parameters.band_gain(2), 4.0F);
    EXPECT_FLOAT_EQ(parameters.preamp(), -4.0F);
}

} // namespace
