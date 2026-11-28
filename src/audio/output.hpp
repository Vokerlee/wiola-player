/**
 * @file
 * @brief Where playback runs.
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

#include <wiola/foundation/handle.hpp>
#include <wiola/foundation/lease_handle.hpp>
#include <wiola/media/stream_spec.hpp>

#include <cstddef>
#include <optional>
#include <utility>

namespace wiola::audio {

class OutputConnection;

/// Somewhere frames are played, which is started and stopped and says how much has been heard.
class Output {
public:
    class Quiescence;

    /**
     * The end that starts and stops it, for the one thread that drives the device.
     *
     * Handed out rather than reachable from the output itself, so that a thread which only asks
     * what has been played cannot start or stop what another thread is driving: two threads
     * doing so leaves the device in a state neither of them asked for, and a device is not a
     * thing that can be asked which it is in.
     *
     * A held control retains the complete output ownership node and its declared dependencies.
     * Moving it hands the device to another thread. What is moved from, and any control taken
     * while another is still held, does nothing at all.
     *
     * Letting one go gives the device back, so the next thing to drive it can take one: a device
     * outlives the tracks played through it, and each of them drives it in turn. Taking and
     * giving back are for whoever owns the device, never for the thread that plays.
     */
    class Control final : public core::LeaseHandle<Output> {
    public:
        Control(Control&& other) noexcept = default;

        Control& operator=(Control&& other) noexcept
        {
            if (this != &other) {
                release();
                LeaseHandle::operator=(std::move(other));
            }

            return *this;
        }

        /// Gives the device back, so that something else may drive it.
        ~Control() { release(); }

        /// Begins playing what the output pulls. False when there is none to be had.
        [[nodiscard]] bool start() noexcept;

        /// Halts playing and waits for an active callback to return. Once this returns, no callback
        /// is using the source, which may then be replaced or destroyed. Starting again is the
        /// caller's to ask for.
        void stop() noexcept;

    private:
        friend class Output;
        friend class Quiescence;

        using LeaseHandle::LeaseHandle;

        bool controls(const core::Lease<Output>& output) const noexcept
        {
            return owner() == std::addressof(output.get());
        }

        /// Gives back whatever it holds, and holds nothing afterwards.
        void release() noexcept
        {
            const auto retained = retain_owner();
            clear();

            if (retained)
                (*retained)->control_taken_.release();
        }
    };

    /// A stopped output's exclusive claim. Holding it prevents restart during rebinding.
    /// Only an actual stop operation can issue it; moving transfers that permission.
    class Quiescence final {
    public:
        Quiescence(const Quiescence&) = delete;
        Quiescence& operator=(const Quiescence&) = delete;
        Quiescence(Quiescence&&) noexcept = default;
        Quiescence& operator=(Quiescence&&) noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return static_cast<bool>(control_);
        }

        [[nodiscard]] bool matches(const core::Lease<Output>& output) const noexcept
        {
            return control_.controls(output);
        }

    private:
        friend class Output;
        friend class OutputConnection;

        explicit Quiescence(Control control) noexcept
            : control_{std::move(control)}
        {
        }

        Control control_;
    };

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    virtual ~Output() = default;

    /// The end that drives it. There is one at a time: while a control is held, another taken is
    /// inert, so a second driver is a device that will not start rather than two threads starting
    /// one. Letting the held one go frees the device for the next.
    [[nodiscard]] static Control acquire(core::Lease<Output> output) noexcept;

    /// Acquire a free device or transfer an existing claim, stop and await callbacks, then
    /// retain exclusive wiring permission. An unavailable/inert claim returns no permission.
    [[nodiscard]] static std::optional<Quiescence> quiesce(core::Lease<Output> output) noexcept;
    [[nodiscard]] static std::optional<Quiescence> quiesce(Control&& active) noexcept;

    [[nodiscard]] virtual bool running() const noexcept = 0;

    /// Frames of the source that have been played. Silence emitted because the source had nothing
    /// to give does not count: this measures material, not elapsed time, so a position taken from
    /// it stands still while a source is starved instead of running on without it.
    [[nodiscard]] virtual pcm::Frames frames_played() const noexcept = 0;

protected:
    Output() = default;

private:
    /// Driving it is the control's, above. Overridden here, called only from there.
    [[nodiscard]] virtual bool start() noexcept = 0;
    virtual void stop() noexcept = 0;

    core::Claimable control_taken_;
};

inline bool Output::Control::start() noexcept
{
    const auto retained = retain_owner();
    return retained && (*retained)->start();
}

inline void Output::Control::stop() noexcept
{
    const auto retained = retain_owner();
    if (retained)
        (*retained)->stop();
}

inline Output::Control Output::acquire(core::Lease<Output> output) noexcept
{
    return output->control_taken_.claim() ? Control{output} : Control{};
}

inline std::optional<Output::Quiescence> Output::quiesce(core::Lease<Output> output) noexcept
{
    return quiesce(acquire(output));
}

inline std::optional<Output::Quiescence> Output::quiesce(Control&& active) noexcept
{
    if (!active)
        return std::nullopt;

    // Publish the source as inert before a backend stop can reenter its previous owner.
    Control retained{std::move(active)};
    retained.stop();
    return Quiescence{std::move(retained)};
}

} // namespace wiola::audio
