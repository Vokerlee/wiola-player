/**
 * @file
 * @brief Saved-playlist and workspace navigation using repository identities.
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

#include "library_navigation.hpp"

#include <utility>

namespace wiola::gui {

namespace {

constexpr int kind_role = Qt::UserRole;
constexpr int identity_role = Qt::UserRole + 1;

enum EntryKind {
    playlist_entry,
    workspace_entry,
};

QTreeWidgetItem* group(QTreeWidget& tree, const QString& label)
{
    QTreeWidgetItem* item = new QTreeWidgetItem{&tree, {label}};
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);

    return item;
}

QTreeWidgetItem* entry(QTreeWidgetItem& group, const QString& label, EntryKind kind,
    const QString& identity)
{
    QTreeWidgetItem* item = new QTreeWidgetItem{&group, {label}};
    item->setData(0, kind_role, kind);
    item->setData(0, identity_role, identity);

    return item;
}

} // namespace

LibraryNavigation::LibraryNavigation(QWidget* parent)
    : QTreeWidget{parent}
{
    setObjectName("libraryNavigation");
    setAccessibleName("Library navigation");
    setHeaderHidden(true);
    setMinimumWidth(160);
    setSelectionMode(QAbstractItemView::SingleSelection);

    playlists_ = group(*this, "Playlists");
    workspaces_ = group(*this, "Workspaces");

    connect(this, &QTreeWidget::currentItemChanged,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current == nullptr)
                return;

            const QString identity = current->data(0, identity_role).toString();
            if (identity.isEmpty())
                return;

            switch (current->data(0, kind_role).toInt()) {
            case EntryKind::playlist_entry:
                if (playlist_observer_)
                    playlist_observer_(library::PlaylistId{identity.toStdString()});
                break;

            case EntryKind::workspace_entry:
                if (workspace_observer_)
                    workspace_observer_(library::WorkspaceId{identity.toStdString()});
                break;
            }
        });
}

void LibraryNavigation::on_playlist_chosen(std::function<void(library::PlaylistId)> observer)
{
    playlist_observer_ = std::move(observer);
}

void LibraryNavigation::on_workspace_chosen(std::function<void(library::WorkspaceId)> observer)
{
    workspace_observer_ = std::move(observer);
}

void LibraryNavigation::show_repository(const library::LibraryRepository& repository)
{
    while (playlists_->childCount() != 0)
        delete playlists_->takeChild(0);

    for (const library::PlaylistRecord& playlist : repository.playlists())
        entry(*playlists_, QString::fromStdString(playlist.name), EntryKind::playlist_entry,
            QString::fromStdString(playlist.id.value));
}

void LibraryNavigation::show_workspaces(const std::vector<WorkspaceNavigationEntry>& workspaces)
{
    while (workspaces_->childCount() != 0)
        delete workspaces_->takeChild(0);

    for (const WorkspaceNavigationEntry& workspace : workspaces)
        entry(*workspaces_, QString::fromStdString(workspace.name), EntryKind::workspace_entry,
            QString::fromStdString(workspace.id.value));
}

} // namespace wiola::gui
