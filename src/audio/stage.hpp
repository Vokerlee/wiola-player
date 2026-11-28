/**
 * @file
 * @brief One step of the shaping between a decoder and a device.
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

#include <wiola/foundation/prepared_change.hpp>
#include <wiola/media/stream_spec.hpp>

#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

namespace wiola::audio {

/**
 * One step of the shaping a chain does, run in the order the chain holds them.
 *
 * Shaping happens on the thread that plays, so `process` does what it does and nothing else: no
 * lock to wait on, no allocation, no reaching for a file. What a listener asks for arrives by
 * whatever means the step itself provides, which for the ones here is an atomic.
 */
class Stage {
    class RetainedPreparation;

public:
    Stage() = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;

    virtual ~Stage() = default;

    /// Owned candidate for one stage or chain. The public prepare facade retains its stage.
    /// Destruction without commit discards the candidate and preserves active processing.
    class Preparation {
    public:
        Preparation(const Preparation&) = delete;
        Preparation& operator=(const Preparation&) = delete;
        Preparation(Preparation&&) = delete;
        Preparation& operator=(Preparation&&) = delete;
        virtual ~Preparation() = default;

        /// Control thread, after callbacks have quiesced. No allocation or failure;
        /// repeated commits are inert. Destroy the preparation on the control thread too.
        [[nodiscard]] bool ready() const noexcept { return !committed_ && can_activate(); }

        bool commit() noexcept
        {
            if (!ready())
                return false;

            committed_ = true;
            activate();
            return true;
        }

    protected:
        Preparation() = default;

    private:
        virtual bool can_activate() const noexcept { return true; }

        virtual void activate() noexcept = 0;
        bool committed_{false};
    };

    /// Retains the stage and binds its private candidate to the current configuration version.
    /// Control thread; preparation may overlap rendering. Stateless stages return null.
    [[nodiscard]] static std::unique_ptr<Preparation> prepare(core::Lease<Stage> stage,
        pcm::StreamSpec spec);

    /// Immediate preparation/commit with output stopped. A rejected candidate changes nothing.
    static void configure(core::Lease<Stage> stage, pcm::StreamSpec spec);

    /// Shapes `samples` in place.
    virtual void process(std::span<float> samples) noexcept = 0;

private:
    /// Driver hook: allocate candidate storage without changing active processing or reentering
    /// configuration. Candidate activation must not invoke callbacks that reenter configuration.
    virtual std::unique_ptr<Preparation> prepare_change(pcm::StreamSpec spec) = 0;
    core::ChangeVersion version_;
};

class Stage::RetainedPreparation final : public Stage::Preparation {
public:
    RetainedPreparation(core::Lease<Stage> stage, std::unique_ptr<Preparation> candidate)
        : change_{stage, &Stage::version_, std::move(candidate),
              [](Stage&, auto& prepared) noexcept { static_cast<void>(prepared->commit()); }}
    {
    }

private:
    bool can_activate() const noexcept override
    {
        return change_.ready() && change_.candidate()->ready();
    }

    void activate() noexcept override { static_cast<void>(change_.commit()); }

    core::PreparedChange<Stage, std::unique_ptr<Preparation>> change_;
};

inline std::unique_ptr<Stage::Preparation> Stage::prepare(core::Lease<Stage> stage,
    pcm::StreamSpec spec)
{
    auto candidate = stage->prepare_change(spec);
    if (!candidate)
        return nullptr;

    return std::make_unique<RetainedPreparation>(stage, std::move(candidate));
}

inline void Stage::configure(core::Lease<Stage> stage, pcm::StreamSpec spec)
{
    if (auto prepared = prepare(stage, spec); prepared && !prepared->commit())
        throw std::logic_error{"prepared processing configuration is no longer current"};
}

} // namespace wiola::audio
