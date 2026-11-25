/**
 * @file
 * @brief Dedicated document presenting the engine's real playback queue.
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

#ifndef Q_MOC_RUN
#include <presentation/documents/queue_rows.hpp>
#endif

#include <QWidget>

#include <functional>
#include <vector>
#include <wiola/library/identity.hpp>

namespace wiola::library {

class LibraryRepository;

}

namespace wiola::gui {

class TrackBrowser;

class NowPlayingDocument final : public QWidget {
    Q_OBJECT

public:
    explicit NowPlayingDocument(QWidget* parent = nullptr);

    [[nodiscard]] TrackBrowser& browser() noexcept { return *browser_; }

    void set_media_resolver(std::function<library::MediaId(const std::filesystem::path&)> resolver)
    {
        source_.set_resolver(std::move(resolver));
    }

    bool show_playlist(const engine::QueueView& playlist);
    void show_library(const library::LibraryRepository& repository);
    bool show_library_changes(const engine::QueueView& playlist,
        const library::LibraryRepository& repository, const library::LibraryChangeBatch& batch);
    [[nodiscard]] std::vector<std::filesystem::path>
    displayed_paths(const engine::QueueView& playlist) const;
    [[nodiscard]] std::vector<engine::QueueEntryId> chosen_tracks() const;

signals:
    void track_chosen(engine::QueueEntryId id);
    void removal_asked();
    void transfer_active_asked();
    void transfer_destination_asked();
    void order_application_asked();

private:
    void present_changes(const QueueRowChanges& changes);
    QueueRows source_;
    const library::LibraryRepository* library_{};
    TrackBrowser* browser_{nullptr};
};

} // namespace wiola::gui
