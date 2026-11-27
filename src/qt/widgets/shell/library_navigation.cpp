/**
 * @file
 * @brief Play queue, saved-playlist and workspace navigation.
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

#include <qt/widgets/appearance/presentation_tokens.hpp>
#include <qt/widgets/appearance/theme.hpp>
#include <qt/widgets/documents/playlist_name.hpp>

#include <QSignalBlocker>

#include <utility>

namespace wiola::gui {

namespace {

constexpr int kind_role = Qt::UserRole;
constexpr int identity_role = Qt::UserRole + 1;
constexpr int name_role = Qt::UserRole + 2;

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
    item->setData(0, name_role, label);

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

    queue_ = new QTreeWidgetItem{this, {"Play queue"}};
    queue_->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    queue_->setToolTip(0, "Tracks queued for playback");

    const auto open_queue = [this](QTreeWidgetItem* item, int) {
        if (item == queue_ && queue_observer_)
            queue_observer_();
    };
    connect(this, &QTreeWidget::itemClicked, this, open_queue);
    connect(this, &QTreeWidget::itemActivated, this, open_queue);

    playlists_ = group(*this, "Playlists");
    workspaces_ = group(*this, "Folders");

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

void LibraryNavigation::on_queue_chosen(std::function<void()> observer)
{
    queue_observer_ = std::move(observer);
}

void LibraryNavigation::on_playlist_chosen(std::function<void(library::PlaylistId)> observer)
{
    playlist_observer_ = std::move(observer);
}

void LibraryNavigation::on_workspace_chosen(std::function<void(library::WorkspaceId)> observer)
{
    workspace_observer_ = std::move(observer);
}

std::optional<library::PlaylistId> LibraryNavigation::selected_playlist() const
{
    const QTreeWidgetItem* current = currentItem();

    if (current == nullptr || current->parent() != playlists_)
        return {};

    return library::PlaylistId{current->data(0, identity_role).toString().toStdString()};
}

bool LibraryNavigation::select_playlist(const library::PlaylistId& id)
{
    for (int row = 0; row < playlists_->childCount(); ++row) {
        QTreeWidgetItem* candidate = playlists_->child(row);

        if (candidate->data(0, identity_role).toString().toStdString() != id.value)
            continue;

        setCurrentItem(candidate);

        return true;
    }

    return false;
}

void LibraryNavigation::show_repository(const library::LibraryRepository& repository)
{
    const auto selected = selected_playlist();
    const QSignalBlocker blocked{this};

    while (playlists_->childCount() != 0)
        delete playlists_->takeChild(0);

    for (const library::PlaylistRecord& playlist : repository.playlists())
        entry(*playlists_, playlist_name(playlist), EntryKind::playlist_entry,
            QString::fromStdString(playlist.id.value));

    show_activity();

    if (selected)
        static_cast<void>(select_playlist(*selected));
}

void LibraryNavigation::set_active_playlist(std::optional<library::PlaylistId> id)
{
    if (active_ == id && activity_palette_ == palette().cacheKey())
        return;

    activity_palette_ = palette().cacheKey();
    active_ = std::move(id);
    show_activity();
}

void LibraryNavigation::show_activity()
{
    const auto icon = active_
        ? themed_standard_icon(*this, PresentationTokens{}.playlist.activity_icon,
              QPalette::Highlight)
        : QIcon{};
    for (int row = 0; row < playlists_->childCount(); ++row) {
        auto* item = playlists_->child(row);
        const bool active =
            active_ && active_->value == item->data(0, identity_role).toString().toStdString();
        auto font = this->font();
        font.setBold(active);
        item->setFont(0, font);
        item->setText(0, item->data(0, name_role).toString());
        item->setIcon(0, active ? icon : QIcon{});
        item->setToolTip(0, active ? "Active playlist: contains the current track" : "");
    }
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
