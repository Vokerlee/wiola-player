/**
 * @file
 * @brief Desktop runtime composition, observation, and explicit shutdown.
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
#include <qt/application/library/library_session.hpp>
#include <qt/application/playback/playback_preferences.hpp>
#include <wiola/app/playback/playback_service.hpp>
#include <wiola/playback/output_factory.hpp>

#include <functional>
#endif
#include <QObject>
#include <QTimer>

#include <chrono>

namespace wiola::app {

struct PlayerRuntimeConfig {
    std::shared_ptr<media::MediaProvider> provider;
    engine::OutputFactory output;
    std::filesystem::path library_file;
    persistence::qt::AtomicFileStore::OutputFactory library_output;
    std::chrono::milliseconds pump_interval{20};
    std::chrono::milliseconds presentation_interval{100};
};

/// Owns runtime services without depending on any window class. View connections are scoped by
/// their QObject receiver. Quit cancels optional reads/scans before flushing required state.
class PlayerApplication final : public QObject {
    Q_OBJECT
public:
    explicit PlayerApplication(PlayerRuntimeConfig config, QObject* parent = nullptr);
    ~PlayerApplication() override;

    [[nodiscard]] PlaybackService& playback() noexcept { return playback_; }

    [[nodiscard]] LibrarySession& library() noexcept { return library_; }

    [[nodiscard]] PlaybackPreferences& preferences() noexcept { return preferences_; }

    void observe(QObject& receiver, std::function<void(const PlayerPresentation&)> playback,
        std::function<void()> refresh);
    [[nodiscard]] bool shutdown();

signals:
    void pumped(const wiola::app::PlayerPresentation& presentation);
    void refresh_requested();

private:
    PlaybackService playback_;
    LibrarySession library_;
    PlaybackPreferences preferences_{playback_};
    QTimer pump_timer_;
    QTimer refresh_timer_;
    bool shutting_down_{};
};

} // namespace wiola::app
