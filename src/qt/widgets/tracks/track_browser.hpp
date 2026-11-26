/**
 * @file
 * @brief Shared track browsing composition with explicit command capabilities.
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
#include "tag_filter_panel.hpp"
#include "track_search_panel.hpp"
#include "track_table_view.hpp"
#endif
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace wiola::gui {

class TrackBrowser final : public QWidget {
    Q_OBJECT
public:
    TrackBrowser(TrackCapabilities capabilities, QWidget* parent = nullptr);
    ~TrackBrowser() override;
    void show_rows(const std::vector<TrackRow>& rows, std::optional<TrackRowId> current = {},
        const library::LibraryRepository* repository = nullptr);

    [[nodiscard]] gui::BrowserViewState capture_browser_state()
    {
        return table_->capture_browser_state();
    }

    void restore_browser_state(gui::BrowserViewState state)
    {
        set_view_state(state.view);
        table_->restore_browser_state(std::move(state));
    }

    void set_view_state(TrackViewState state);

    [[nodiscard]] const TrackViewState& view_state() const noexcept { return table_->view_state(); }

    void show_library(const library::LibraryRepository& repository);

    void bind_library(const library::LibraryRepository& repository)
    {
        table_->bind_library(repository);
    }

    void update_rows(const std::vector<TrackRow>& rows,
        const library::LibraryRepository* repository = nullptr)
    {
        table_->update_rows(rows, repository);
        update_results();
    }

    [[nodiscard]] TrackTableView& table() noexcept { return *table_; }

    [[nodiscard]] const TrackTableView& table() const noexcept { return *table_; }

signals:
    void row_activated(wiola::gui::TrackRowId id);
    void command_requested(wiola::gui::TrackCommand command);
    void selection_changed();

private:
    void update_results();
    void apply_query(library::SearchQuery query);
    QTimer search_timer_;
    std::optional<library::SearchQuery> pending_query_;
    QPointer<TrackTableView> table_;
    QPointer<TrackSearchPanel> search_;
    QPointer<TagFilterPanel> filters_;
    QObject lifetime_;
};

} // namespace wiola::gui
