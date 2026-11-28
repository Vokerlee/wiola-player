/**
 * @file
 * @brief Fixed processing topology and final output sanitizing.
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

#include "processing_chain.hpp"

#include <algorithm>
#include <utility>

namespace wiola::audio {

namespace {

class PreparedChain final : public Stage::Preparation {
public:
    explicit PreparedChain(std::vector<std::unique_ptr<Stage::Preparation>> stages) noexcept
        : stages_{std::move(stages)}
    {
    }

private:
    bool can_activate() const noexcept override
    {
        return std::ranges::all_of(stages_, [](const auto& stage) { return stage->ready(); });
    }

    void activate() noexcept override
    {
        for (const auto& stage : stages_)
            stage->commit();
    }

    std::vector<std::unique_ptr<Stage::Preparation>> stages_;
};

} // namespace

ProcessingChain::ProcessingChain(std::initializer_list<core::Lease<Stage>> stages)
    : ProcessingChain(std::span{stages.begin(), stages.size()})
{
}

ProcessingChain::ProcessingChain(std::span<const core::Lease<Stage>> stages)
    : stages_{stages.begin(), stages.end()}
{
}

std::unique_ptr<Stage::Preparation> ProcessingChain::prepare_change(pcm::StreamSpec spec)
{
    std::vector<std::unique_ptr<Stage::Preparation>> prepared;
    prepared.reserve(stages_.size());

    for (const auto& stage : stages_) {
        if (auto candidate{Stage::prepare(stage, spec)})
            prepared.push_back(std::move(candidate));
    }

    if (prepared.empty())
        return nullptr;

    return std::make_unique<PreparedChain>(std::move(prepared));
}

void ProcessingChain::process(std::span<float> samples) noexcept
{
    for (const auto& stage : stages_)
        stage->process(samples);

    sanitizer_.process(samples);
}

} // namespace wiola::audio
