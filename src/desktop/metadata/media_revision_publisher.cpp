/**
 * @file
 * @brief Queued, identity-aware publication of background metadata results.
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

#include "media_revision_publisher.hpp"

namespace wiola::app {

MediaRevisionPublisher::MediaRevisionPublisher(QObject& gui_context,
    std::shared_ptr<media::MediaProvider> provider, Observer observer)
    : delivery_{gui_context}
    , state_{std::make_shared<State>(std::move(observer))}
    , reader_{std::move(provider)}
{
}

MediaRevisionPublisher::~MediaRevisionPublisher() = default;

bool MediaRevisionPublisher::enqueue(const library::MediaId& media, std::filesystem::path path)
{
    if (!media.valid())
        return false;

    std::uint64_t request;

    {
        std::lock_guard lock{state_->mutex};
        request = ++state_->requests[media];
    }

    const std::weak_ptr<State> state = state_;
    const bool accepted = reader_.enqueue(media, std::move(path),
        [state, delivery = &delivery_, request](BackgroundMetadataResult result) {
            receive(state, delivery, request, std::move(result));
        });

    if (!accepted) {
        std::lock_guard lock{state_->mutex};
        const auto current = state_->requests.find(media);

        if (current != state_->requests.end() && current->second == request)
            state_->requests.erase(current);
    }

    return accepted;
}

void MediaRevisionPublisher::receive(const std::weak_ptr<State>& weak, QueuedDelivery* delivery,
    std::uint64_t request, BackgroundMetadataResult result)
{
    const auto state = weak.lock();
    if (!state)
        return;

    const library::MediaId media = result.media;

    {
        std::lock_guard lock{state->mutex};
        const auto current = state->requests.find(media);

        if (current == state->requests.end() || current->second != request)
            return;
    }

    static_cast<void>(delivery->post([weak, media, request, result = std::move(result)]() mutable {
        const auto current = weak.lock();
        if (!current)
            return;

        {
            std::lock_guard lock{current->mutex};
            const auto expected = current->requests.find(media);

            if (expected == current->requests.end() || expected->second != request)
                return;

            current->requests.erase(expected);
        }

        current->observer(std::move(result));
    }));
}

} // namespace wiola::app
