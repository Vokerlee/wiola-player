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

#include <qt/widgets/documents/playlist_name.hpp>
#include <wiola/app/library/playlist_name.hpp>
#include <wiola/app/library/saved_playlist_commands.hpp>

#include <QAction>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>

namespace wiola::gui {

SavedPlaylistManager::SavedPlaylistManager(const library::LibraryRepository& repository,
    app::SavedPlaylistCommands& commands, LibraryNavigation& navigation, QWidget* parent)
    : QWidget{parent}
    , repository_{repository}
    , commands_{commands}
    , navigation_{navigation}
{
    setObjectName("savedPlaylistManager");

    create_button_ = new QPushButton{"+ New playlist", this};

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

    for (auto* button : {rename_button_, duplicate_button_, remove_button_}) {
        button->hide();

        auto* action = new QAction{button->text() + " playlist", this};
        navigation_.addAction(action);
        connect(action, &QAction::triggered, button, &QPushButton::click);

        const auto update = [this, action] {
            action->setEnabled(selected().has_value());
        };
        connect(&navigation_, &QTreeWidget::currentItemChanged, this, update);

        update();
    }

    navigation_.setContextMenuPolicy(Qt::ActionsContextMenu);

    auto* create_action = new QAction{"New playlist…", this};
    create_action->setShortcut(QKeySequence{"Ctrl+Shift+N"});
    create_action->setShortcutContext(Qt::WindowShortcut);
    addAction(create_action);
    navigation_.addAction(create_action);
    connect(create_action, &QAction::triggered, create_button_, &QPushButton::click);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(save_state_);
    save_state_->hide();

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
        const QString current = playlist_name(*repository_.playlist(*id));
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
    if (!app::PlaylistName::parse(name)) {
        save_state_->setText("Enter a playlist name");
        return false;
    }

    save_state_->show();

    const QPointer<SavedPlaylistManager> owner{this};
    const auto created = commands_.create(name);

    return owner ? finish(created.commit, created.id) : static_cast<bool>(created.commit);
}

bool SavedPlaylistManager::rename_selected(const std::string& name)
{
    const auto id = selected();

    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    if (!app::PlaylistName::parse(name)) {
        save_state_->setText("Enter a playlist name");
        return false;
    }

    const QPointer<SavedPlaylistManager> owner{this};
    const auto committed = commands_.rename(*id, name);

    return owner ? finish(committed, *id) : static_cast<bool>(committed);
}

bool SavedPlaylistManager::duplicate_selected()
{
    const auto id = selected();

    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    const QPointer<SavedPlaylistManager> owner{this};
    const auto duplicate = commands_.duplicate(*id);

    return owner ? finish(duplicate.commit, duplicate.id) : static_cast<bool>(duplicate.commit);
}

bool SavedPlaylistManager::remove_selected()
{
    const auto id = selected();

    if (!id) {
        save_state_->setText("Select a playlist first");
        return false;
    }

    const QPointer<SavedPlaylistManager> owner{this};
    const auto committed = commands_.remove(*id);

    return owner ? finish(committed) : static_cast<bool>(committed);
}

std::optional<library::PlaylistId> SavedPlaylistManager::selected() const
{
    return navigation_.selected_playlist();
}

bool SavedPlaylistManager::finish(library::CommitResult committed, const library::PlaylistId& id)
{
    if (!committed) {
        show_save_failed();
        return false;
    }

    const QPointer<SavedPlaylistManager> owner{this};

    navigation_.show_repository(repository_);

    if (!owner)
        return true;

    if (id.valid())
        navigation_.select_playlist(id);

    if (!owner)
        return true;

    if (committed.revision != 0) {
        displayed_revision_ = committed.revision;
        save_state_->setText(committed.durable() ? "All changes saved"
                                                 : "Changes accepted; saving library");

        if (owner)
            save_state_->setVisible(!committed.durable());

        if (!owner)
            return true;

        if (committed.receipt)
            committed.receipt->observe([owner = QPointer<SavedPlaylistManager>{this}](auto event) {
                if (owner && owner->displayed_revision_ == event.revision) {
                    owner->save_state_->setText(event.durable
                            ? "All changes saved"
                            : "Changes remain in memory; retry saving library");
                    if (owner)
                        owner->save_state_->setVisible(!event.durable);
                }
            });
    }

    if (!owner)
        return true;

    const auto observer = changed_observer_;

    if (observer)
        (*observer)();

    return true;
}

void SavedPlaylistManager::on_changed(std::function<void()> observer)
{
    auto replacement =
        observer ? std::make_shared<std::function<void()>>(std::move(observer)) : nullptr;
    auto retired = std::move(changed_observer_);

    changed_observer_ = std::move(replacement);
}

void SavedPlaylistManager::show_save_failed()
{
    save_state_->setText("Save failed; changes restored");
    save_state_->show();
}

} // namespace wiola::gui
