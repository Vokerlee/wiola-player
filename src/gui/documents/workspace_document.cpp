/**
 * @file
 * @brief Incrementally populated folder workspace document.
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

#include "workspace_document.hpp"

#include <gui/tracks/tag_filter_panel.hpp>
#include <gui/tracks/track_search_panel.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <library/location.hpp>

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <utility>

namespace wiola::gui {

WorkspaceDocument::WorkspaceDocument(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("workspaceDocument");
    heading_ = new QLabel{this};
    heading_->setObjectName("workspaceHeading");
    QFont font = heading_->font();
    font.setBold(true);
    heading_->setFont(font);

    status_ = new QLabel{this};
    status_->setObjectName("workspaceStatus");

    recursive_ = new QCheckBox{"Include subfolders", this};
    recursive_->setObjectName("workspaceRecursive");
    rescan_ = new QPushButton{"Rescan", this};
    rescan_->setObjectName("rescanWorkspace");
    cancel_ = new QPushButton{"Cancel", this};
    cancel_->setObjectName("cancelWorkspaceScan");
    progress_ = new QProgressBar{this};
    progress_->setObjectName("workspaceProgress");
    progress_->setTextVisible(false);

    play_ = new QPushButton{"Play selection", this};
    play_->setObjectName("playWorkspaceSelection");
    append_ = new QPushButton{"Add to Now Playing", this};
    append_->setObjectName("appendWorkspaceSelection");

    table_ = new TrackTableView{this};
    table_->setAccessibleName("Workspace tracks");
    search_ = new TrackSearchPanel{this};
    search_->on_changed([this](library::SearchQuery query) {
        table_->search_by(query);
        update_results();
    });
    filters_ = new TagFilterPanel{this};
    filters_->on_changed([this](std::vector<library::TagFilter> filters) {
        table_->filter_by(filters);
        update_results();
    });

    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget(recursive_);
    controls->addWidget(rescan_);
    controls->addWidget(cancel_);

    QHBoxLayout* playback = new QHBoxLayout;
    playback->addWidget(play_);
    playback->addWidget(append_);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading_);
    layout->addLayout(controls);
    layout->addLayout(playback);
    layout->addWidget(progress_);
    layout->addWidget(status_);
    layout->addWidget(search_);
    layout->addWidget(filters_);
    layout->addWidget(table_);

    connect(rescan_, &QPushButton::clicked, [this] {
        if (rescan_observer_)
            rescan_observer_(recursive_->isChecked());
    });

    connect(cancel_, &QPushButton::clicked, [this] {
        if (cancel_observer_)
            cancel_observer_();
    });

    connect(play_, &QPushButton::clicked, [this] {
        if (play_observer_)
            play_observer_();
    });

    connect(append_, &QPushButton::clicked, [this] {
        if (append_observer_)
            append_observer_();
    });

    connect(table_, &TrackTableView::row_chosen, [this](TrackRowId) {
        if (play_observer_)
            play_observer_();
    });

    connect(table_, &TrackTableView::transfer_active_asked, [this] {
        if (transfer_active_observer_)
            transfer_active_observer_();
    });

    connect(table_, &TrackTableView::transfer_destination_asked, [this] {
        if (transfer_destination_observer_)
            transfer_destination_observer_();
    });

    connect(table_, &TrackTableView::order_application_asked, [this] {
        if (apply_order_observer_)
            apply_order_observer_();
    });

    set_busy(false);
}

void WorkspaceDocument::begin(const library::WorkspaceRecord& workspace, std::uint64_t generation)
{
    if (!source_.begin(workspace, generation))
        return;
    heading_->setText(platform::qt::QtPathAdapter::to_qt(workspace.root));
    recursive_->setChecked(workspace.recursive);
    status_->setText("Scanning...");
    table_->show_rows(source_.rows());
    set_busy(true);
}

bool WorkspaceDocument::show_batch(const app::WorkspaceScanBatch& batch,
    const library::LibraryRepository* repository)
{
    if (!source_.apply(batch, repository))
        return false;
    if (repository)
        table_->show_library(*repository);
    table_->show_rows(source_.rows());
    update_results();
    status_->setText(QString{"%1 tracks"}.arg(source_.rows().size()));
    set_busy(batch.status == app::WorkspaceScanStatus::running);

    if (batch.status == app::WorkspaceScanStatus::failed) {
        status_->setText(status_->text() + " — scan failed");
        status_->setProperty("status", "error");
    } else if (batch.status == app::WorkspaceScanStatus::cancelled) {
        status_->setText(status_->text() + " — cancelled");
        status_->setProperty("status", "ready");
    }

    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    return true;
}

bool WorkspaceDocument::show_media_revision(const library::LibraryRepository& repository,
    const library::MediaId& media)
{
    const bool changed = source_.update_media(repository, media);
    table_->show_library(repository);
    if (changed)
        table_->show_rows(source_.rows());
    update_results();
    return changed;
}

void WorkspaceDocument::show_library(const library::LibraryRepository& repository)
{
    table_->show_library(repository);
    update_results();
}

void WorkspaceDocument::update_results()
{
    search_->show_results(table_->visible_count(), table_->total_count());
}

void WorkspaceDocument::show_cancelled()
{
    source_.cancel();
    status_->setText(QString{"%1 tracks — cancelled"}.arg(source_.rows().size()));
    set_busy(false);
}

void WorkspaceDocument::set_busy(bool busy)
{
    progress_->setRange(0, busy ? 0 : 1);
    rescan_->setEnabled(!busy);
    cancel_->setEnabled(busy);
    status_->setProperty("status", busy ? "busy" : "ready");
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

void WorkspaceDocument::on_rescan_requested(std::function<void(bool recursive)> observer)
{
    rescan_observer_ = std::move(observer);
}

void WorkspaceDocument::on_cancel_requested(std::function<void()> observer)
{
    cancel_observer_ = std::move(observer);
}

std::vector<TrackRowId> WorkspaceDocument::chosen_rows() const
{
    return table_->chosen_rows();
}

std::vector<SelectedTrack> WorkspaceDocument::selection() const
{
    std::vector<SelectedTrack> result;
    for (const auto& id : table_->chosen_rows())
        if (const auto* row = source_.row(id))
            result.push_back({row->media, row->id, row->location});
    return result;
}

std::vector<std::filesystem::path> WorkspaceDocument::chosen_paths() const
{
    std::vector<std::filesystem::path> paths;
    for (const auto& selected : selection())
        if (!selected.location.empty())
            paths.push_back(selected.location);
    return paths;
}

std::vector<std::filesystem::path> WorkspaceDocument::displayed_paths() const
{
    std::vector<std::filesystem::path> paths;
    for (const auto& id : table_->displayed_rows())
        if (const auto* row = source_.row(id); row && !row->location.empty())
            paths.push_back(row->location);
    return paths;
}

void WorkspaceDocument::on_apply_order_requested(std::function<void()> observer)
{
    apply_order_observer_ = std::move(observer);
}

void WorkspaceDocument::on_play_requested(std::function<void()> observer)
{
    play_observer_ = std::move(observer);
}

void WorkspaceDocument::on_append_requested(std::function<void()> observer)
{
    append_observer_ = std::move(observer);
}

void WorkspaceDocument::on_transfer_active_requested(std::function<void()> observer)
{
    transfer_active_observer_ = std::move(observer);
}

void WorkspaceDocument::on_transfer_destination_requested(std::function<void()> observer)
{
    transfer_destination_observer_ = std::move(observer);
}

} // namespace wiola::gui
