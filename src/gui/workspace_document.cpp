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

#include <QVBoxLayout>

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
    table_ = new TrackTableView{this};
    table_->setAccessibleName("Workspace tracks");

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading_);
    layout->addWidget(status_);
    layout->addWidget(table_);
}

void WorkspaceDocument::begin(const library::WorkspaceRecord& workspace, std::uint64_t generation)
{
    workspace_ = workspace.id;
    generation_ = generation;
    rows_.clear();
    heading_->setText(QtPathAdapter::to_qt(workspace.root));
    status_->setText("Scanning...");
    table_->show_tracks(rows_.tracks());
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
    status_->setText(QString{"%1 tracks"}.arg(rows_.tracks().size()));

    if (batch.status == app::WorkspaceScanStatus::failed)
        status_->setText(status_->text() + " — scan failed");
    else if (batch.status == app::WorkspaceScanStatus::cancelled)
        status_->setText(status_->text() + " — cancelled");

    return true;
}

std::vector<engine::QueueEntryId> WorkspaceDocument::chosen_tracks() const
{
    return table_->chosen_tracks();
}

} // namespace wiola::gui
