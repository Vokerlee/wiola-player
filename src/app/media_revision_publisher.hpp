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

#pragma once

#include "background_metadata.hpp"

#include <QObject>

#include <map>
#include <memory>
#include <mutex>

namespace wiola::app {

class MediaRevisionPublisher final {
public:
    using Observer = BackgroundMetadata::Observer;

    MediaRevisionPublisher(QObject& gui_context, std::shared_ptr<media::MediaProvider> provider,
        Observer observer);
    ~MediaRevisionPublisher();

    [[nodiscard]] bool enqueue(const library::MediaId& media, std::filesystem::path path);

private:
    struct State {
        explicit State(Observer observer)
            : observer{std::move(observer)}
        {
        }

        std::mutex mutex;
        std::map<library::MediaId, std::uint64_t> requests;
        Observer observer;
    };

    static void receive(const std::weak_ptr<State>& state, QObject* context, std::uint64_t request,
        BackgroundMetadataResult result);

    std::shared_ptr<State> state_;
    BackgroundMetadata reader_;
    QObject& context_;
};

} // namespace wiola::app
