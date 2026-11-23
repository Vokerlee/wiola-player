/**
 * @file
 * @brief Saved playlist management controls and persistence-state presentation.
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

#include "saved_playlist_manager.hpp"

#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace wiola::gui {

SavedPlaylistManager::SavedPlaylistManager(const library::LibraryRepository& repository,
    app::SavedPlaylistCommands& commands, LibraryNavigation& navigation, QWidget* parent)
    : QWidget{parent}
    , repository_{repository}
    , commands_{commands}
    , navigation_{navigation}
{
    setObjectName("savedPlaylistManager");

    create_button_ = new QPushButton{"Create", this};
    rename_button_ = new QPushButton{"Rename", this};
    duplicate_button_ = new QPushButton{"Duplicate", this};
    remove_button_ = new QPushButton{"Delete", this};
    create_button_->setObjectName("createPlaylist");
    rename_button_->setObjectName("renamePlaylist");
    duplicate_button_->setObjectName("duplicatePlaylist");
    remove_button_->setObjectName("deletePlaylist");

    save_state_ = new QLabel{"All changes saved", this};
    save_state_->setObjectName("playlistSaveState");
    save_state_->setWordWrap(true);
    save_state_->setAccessibleName("Playlist save state");

    QGridLayout* buttons = new QGridLayout;
    buttons->addWidget(create_button_, 0, 0);
    buttons->addWidget(rename_button_, 0, 1);
    buttons->addWidget(duplicate_button_, 1, 0);
    buttons->addWidget(remove_button_, 1, 1);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(save_state_);

    connect(create_button_, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, "Create playlist", "Playlist name",
            QLineEdit::Normal, {}, &accepted);

        if (accepted)
            create(name.toStdString());
    });

    connect(rename_button_, &QPushButton::clicked, this, [this] {
        const auto id = selected();
        if (!id) {
            save_state_->setText("Select a playlist first");
            return;
        }

        bool accepted = false;
        const QString current = QString::fromStdString(repository_.playlist(*id)->name);
        const QString name = QInputDialog::getText(this, "Rename playlist", "Playlist name",
            QLineEdit::Normal, current, &accepted);

        if (accepted)
            rename_selected(name.toStdString());
    });

    connect(duplicate_button_, &QPushButton::clicked, this, [this] { duplicate_selected(); });

    connect(remove_button_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Delete playlist", "Delete the selected saved playlist?") ==
            QMessageBox::Yes)
            remove_selected();
    });
}

bool SavedPlaylistManager::create(const std::string& name)
{
    if (QString::fromStdString(name).trimmed().isEmpty()) {
        save_state_->setText("Enter a playlist name");
        return false;
    }

    const library::PlaylistId id = commands_.create(name);
    return finish(id.valid(), id);
}

bool SavedPlaylistManager::rename_selected(const std::string& name)
{
    const auto id = selected();
    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    if (QString::fromStdString(name).trimmed().isEmpty()) {
        save_state_->setText("Enter a playlist name");
        return false;
    }

    return finish(commands_.rename(*id, name), *id);
}

bool SavedPlaylistManager::duplicate_selected()
{
    const auto id = selected();
    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    const library::PlaylistId duplicate = commands_.duplicate(*id);
    return finish(duplicate.valid(), duplicate);
}

bool SavedPlaylistManager::remove_selected()
{
    const auto id = selected();
    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    return finish(commands_.remove(*id));
}

std::optional<library::PlaylistId> SavedPlaylistManager::selected() const
{
    return navigation_.selected_playlist();
}

bool SavedPlaylistManager::finish(bool succeeded, const library::PlaylistId& id)
{
    if (!succeeded) {
        show_save_failed();
        return false;
    }

    navigation_.show_repository(repository_);

    if (id.valid())
        navigation_.select_playlist(id);

    save_state_->setText("All changes saved");

    if (changed_observer_)
        changed_observer_();

    return true;
}

void SavedPlaylistManager::on_changed(std::function<void()> observer)
{
    changed_observer_ = std::move(observer);
}

void SavedPlaylistManager::show_save_failed()
{
    save_state_->setText("Save failed; changes restored");
}

} // namespace wiola::gui
