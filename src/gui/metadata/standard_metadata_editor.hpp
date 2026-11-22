/**
 * @file
 * @brief Dirty-aware standard metadata editor with validation and cancellation.
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

#include <app/metadata/metadata_edit_session.hpp>
#include <library/metadata.hpp>
#include <library/metadata_patch.hpp>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <functional>
#include <set>
#include <vector>

namespace wiola::gui {

class StandardMetadataEditor final : public QWidget {
public:
    using Observer = std::function<void(const app::MetadataEditSession&)>;

    explicit StandardMetadataEditor(QWidget* parent = nullptr);

    void show_track(const library::TrackRecord* track);
    void show_tracks(const std::vector<const library::TrackRecord*>& tracks);
    void show_session(app::MetadataEditSession session);
    void discard();

    [[nodiscard]] bool retains_draft() const { return !touched_.empty(); }

    [[nodiscard]] const app::MetadataEditSession& edit_session() const { return session_; }

    void show_status(const QString& status);
    void on_apply(Observer observer);

    void on_file_write(Observer observer) { file_observer_ = std::move(observer); }

    void show_file_status(const QString& status) { file_status_->setText(status); }

private:
    [[nodiscard]] library::ResolvedMetadata current() const;
    [[nodiscard]] library::StandardMetadataPatch patch() const;
    [[nodiscard]] bool valid_numbers() const;
    [[nodiscard]] bool dirty() const;
    void populate();
    void update_actions();

    app::MetadataEditSession session_;
    app::MetadataEditSession offered_;
    bool initialized_{false};
    bool populating_{false};
    std::set<QLineEdit*> touched_;
    std::set<QLineEdit*> mixed_;
    QString feedback_;
    QLabel* status_{nullptr};
    QLineEdit* title_{nullptr};
    QLineEdit* artist_{nullptr};
    QLineEdit* album_{nullptr};
    QLineEdit* genre_{nullptr};
    QLineEdit* year_{nullptr};
    QLineEdit* comment_{nullptr};
    QLineEdit* composer_{nullptr};
    QLineEdit* track_number_{nullptr};
    QLineEdit* disc_number_{nullptr};
    QPushButton* apply_{nullptr};
    QPushButton* cancel_{nullptr};
    Observer observer_;
    Observer file_observer_;
    QPushButton* save_file_{nullptr};
    QLabel* file_status_{nullptr};
};

} // namespace wiola::gui
