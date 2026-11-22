/**
 * @file
 * @brief Reusable track table with stable selection and user-visible column controls.
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
#include "track_table_model.hpp"
#endif

#include <QKeyEvent>
#include <QTableView>

#include <optional>
#include <vector>

class QAction;

namespace wiola::gui {

class TrackTableView final : public QTableView {
    Q_OBJECT

public:
    explicit TrackTableView(QWidget* parent = nullptr);

    void show_rows(const std::vector<TrackRow>& tracks, std::optional<TrackRowId> current = {});
    void show_library(const library::LibraryRepository& repository);
    void sort_by(const std::vector<TrackSortKey>& keys);
    void search_by(const library::SearchQuery& query);
    void filter_by(const std::vector<library::TagFilter>& filters);
    [[nodiscard]] std::size_t visible_count() const;
    [[nodiscard]] std::size_t total_count() const;
    [[nodiscard]] std::vector<TrackRowId> displayed_rows() const;
    [[nodiscard]] std::vector<TrackRowId> chosen_rows() const;

signals:
    void row_chosen(wiola::gui::TrackRowId id);
    void removal_asked();
    void transfer_active_asked();
    void transfer_destination_asked();
    void transfer_move_asked();
    void order_application_asked();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild_tag_actions();
    void update_tag_columns();
    void sort_column(int column, bool extend);
    void update_sort_indicator();
    void restore_selection(const std::vector<TrackRowId>& selected,
        const std::optional<TrackRowId>& current);

    TrackTableModel model_;
    std::vector<TrackRow> tracks_;
    std::optional<TrackRowId> current_;
    std::vector<std::string> available_tags_;
    std::vector<std::string> visible_tags_;
    std::vector<TrackSortKey> sort_keys_;
    library::SearchQuery query_;
    std::vector<library::TagFilter> filters_;
    std::vector<QAction*> tag_actions_;
};

} // namespace wiola::gui
