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

#include <library/metadata.hpp>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace wiola::gui {

class StandardMetadataEditor final : public QWidget {
public:
    using Observer = std::function<void(library::MediaId, library::StandardFieldOverrides)>;

    explicit StandardMetadataEditor(QWidget* parent = nullptr);

    void show_track(const library::TrackRecord* track);
    void show_status(const QString& status);
    void on_apply(Observer observer);

private:
    [[nodiscard]] library::ResolvedMetadata current() const;
    [[nodiscard]] library::StandardFieldOverrides overrides() const;
    [[nodiscard]] bool valid_numbers() const;
    [[nodiscard]] bool dirty() const;
    void populate();
    void update_actions();

    library::MediaId media_;
    library::ResolvedMetadata original_;
    std::uint64_t revision_{0};
    bool initialized_{false};
    bool overrides_present_{false};
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
};

} // namespace wiola::gui
