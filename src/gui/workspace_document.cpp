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

#include "qt_path_adapter.hpp"
#include "tag_filter_panel.hpp"
#include "track_search_panel.hpp"

#include <library/location.hpp>
#include <library/metadata.hpp>

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

    connect(table_, &TrackTableView::track_chosen, [this](engine::QueueEntryId) {
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
    workspace_ = workspace.id;
    generation_ = generation;
    rows_.clear();
    heading_->setText(QtPathAdapter::to_qt(workspace.root));
    recursive_->setChecked(workspace.recursive);
    status_->setText("Scanning...");
    table_->show_tracks(rows_.tracks());
    set_busy(true);
}

bool WorkspaceDocument::show_batch(const app::WorkspaceScanBatch& batch)
{
    if (batch.workspace != workspace_ || batch.generation != generation_)
        return false;

    std::vector<engine::Track> discovered;
    discovered.reserve(batch.tracks.size());

    for (const std::filesystem::path& path : batch.tracks)
        discovered.push_back(engine::Track::of(path));

    rows_.add(std::move(discovered));
    table_->show_tracks(rows_.tracks());
    update_results();
    status_->setText(QString{"%1 tracks"}.arg(rows_.tracks().size()));
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
    const library::TrackRecord* record = repository.track(media);
    if (record == nullptr)
        return false;

    const library::ResolvedMetadata resolved = library::resolve_metadata(*record);
    bool matched = false;

    for (const engine::Track& track : rows_.tracks()) {
        if (library::media_id_for(track.path()) != media)
            continue;

        static_cast<void>(rows_.describe(track.id(),
            engine::TrackMetadata{.title = resolved.title,
                .artist = resolved.artist,
                .album = resolved.album,
                .duration = resolved.duration,
                .genre = resolved.genre,
                .year = resolved.year,
                .comment = resolved.comment,
                .composer = resolved.composer,
                .track_number = resolved.track_number,
                .disc_number = resolved.disc_number}));
        matched = true;
    }

    if (matched)
        table_->show_tracks(rows_.tracks());

    table_->show_library(repository);
    return matched;
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
    status_->setText(QString{"%1 tracks — cancelled"}.arg(rows_.tracks().size()));
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

std::vector<engine::QueueEntryId> WorkspaceDocument::chosen_tracks() const
{
    return table_->chosen_tracks();
}

std::vector<std::filesystem::path> WorkspaceDocument::chosen_paths() const
{
    std::vector<std::filesystem::path> paths;

    for (const engine::QueueEntryId id : chosen_tracks()) {
        const auto row = rows_.index_of(id);
        if (row)
            paths.push_back(rows_.tracks()[*row].path());
    }

    return paths;
}

std::vector<std::filesystem::path> WorkspaceDocument::displayed_paths() const
{
    std::vector<std::filesystem::path> paths;

    for (const engine::QueueEntryId id : table_->displayed_tracks()) {
        const auto row = rows_.index_of(id);
        if (row)
            paths.push_back(rows_.tracks()[*row].path());
    }

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
