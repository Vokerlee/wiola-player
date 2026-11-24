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

#include <wiola/audio/processing_chain.hpp>

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
    void activate() noexcept override
    {
        for (const auto& stage : stages_)
            stage->commit();
    }

    std::vector<std::unique_ptr<Stage::Preparation>> stages_;
};

} // namespace

ProcessingChain::ProcessingChain(std::initializer_list<std::reference_wrapper<Stage>> stages)
{
    stages_.reserve(stages.size());
    for (const auto stage : stages)
        stages_.push_back(&stage.get());
}

std::unique_ptr<Stage::Preparation> ProcessingChain::prepare(pcm::StreamSpec spec)
{
    std::vector<std::unique_ptr<Stage::Preparation>> prepared;
    prepared.reserve(stages_.size());
    for (Stage* stage : stages_) {
        if (auto candidate{stage->prepare(spec)})
            prepared.push_back(std::move(candidate));
    }
    if (prepared.empty())
        return nullptr;
    return std::make_unique<PreparedChain>(std::move(prepared));
}

void ProcessingChain::process(std::span<float> samples) noexcept
{
    for (Stage* stage : stages_)
        stage->process(samples);

    sanitizer_.process(samples);
}

} // namespace wiola::audio
