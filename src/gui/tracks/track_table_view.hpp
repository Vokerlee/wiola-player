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
#include "track_commands.hpp"
#include "track_table_model.hpp"
#include <app/library/tag_key_catalog.hpp>
#endif

#include <QKeyEvent>
#include <QTableView>

#include <map>
#include <optional>
#include <vector>

class QAction;

namespace wiola::gui {

class TrackTableView final : public QTableView {
    Q_OBJECT

public:
    explicit TrackTableView(QWidget* parent = nullptr);

    void set_search_index(const app::SearchIndex* index) { model_.set_search_index(index); }

    void set_tag_catalog(const app::TagKeyCatalog* catalog) { tag_catalog_ = catalog; }

    void set_capabilities(TrackCapabilities capabilities);
    void show_rows(const std::vector<TrackRow>& tracks, std::optional<TrackRowId> current = {});
    void show_library(const library::LibraryRepository& repository);
    bool set_view_state(TrackViewState state);

    [[nodiscard]] const TrackViewState& view_state() const noexcept { return model_.view_state(); }

    [[nodiscard]] std::size_t visible_count() const;
    [[nodiscard]] std::size_t total_count() const;
    [[nodiscard]] std::vector<TrackRowId> displayed_rows() const;
    [[nodiscard]] std::vector<TrackRowId> chosen_rows() const;

signals:
    void row_chosen(wiola::gui::TrackRowId id);
    void view_state_changed();
    void removal_asked();
    void transfer_active_asked();
    void transfer_destination_asked();
    void transfer_move_asked();
    void order_application_asked();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void remember_columns();
    void restore_columns();
    void rebuild_tag_actions();
    void update_tag_columns();
    void sort_column(int column, bool extend);
    void update_sort_indicator();
    void restore_selection(const std::vector<TrackRowId>& selected,
        const std::optional<TrackRowId>& current);

    TrackCapabilities capabilities_{TrackCapabilities::all()};
    std::map<TrackCommand, QAction*> command_actions_;
    TrackTableModel model_;
    const app::TagKeyCatalog* tag_catalog_{nullptr};
    std::map<TrackColumnId, TrackColumnLayout> column_layout_;
    std::vector<TrackColumnId> column_order_;
    std::vector<std::string> available_tags_;
    std::vector<std::string> visible_tags_;
    std::vector<QAction*> tag_actions_;
};

} // namespace wiola::gui
