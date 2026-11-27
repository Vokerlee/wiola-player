/**
 * @file
 * @brief Multi-field track search with explicit reset controls.
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

#include "track_search_panel.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace wiola::gui {

TrackSearchPanel::TrackSearchPanel(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("trackSearchPanel");

    title_ = new QLineEdit{this};
    title_->setObjectName("searchTitle");
    title_->setAccessibleName("Search title");
    title_->setPlaceholderText("Title");

    artist_ = new QLineEdit{this};
    artist_->setObjectName("searchArtist");
    artist_->setAccessibleName("Search artist");
    artist_->setPlaceholderText("Artist");

    album_ = new QLineEdit{this};
    album_->setObjectName("searchAlbum");
    album_->setAccessibleName("Search album");
    album_->setPlaceholderText("Album");

    clear_title_ = new QPushButton{"Clear", this};
    clear_title_->setObjectName("clearSearchTitle");
    clear_title_->setAccessibleName("Clear title search");

    clear_artist_ = new QPushButton{"Clear", this};
    clear_artist_->setObjectName("clearSearchArtist");
    clear_artist_->setAccessibleName("Clear artist search");

    clear_album_ = new QPushButton{"Clear", this};
    clear_album_->setObjectName("clearSearchAlbum");
    clear_album_->setAccessibleName("Clear album search");

    tags_ = new QLineEdit{this};
    tags_->setObjectName("searchTags");
    tags_->setAccessibleName("Search custom tag keys and values");
    tags_->setPlaceholderText("Tag key or value");

    clear_tags_ = new QPushButton{"Clear", this};
    clear_tags_->setObjectName("clearSearchTags");
    clear_tags_->setAccessibleName("Clear tag search");

    reset_ = new QPushButton{"Reset search", this};
    reset_->setObjectName("resetTrackSearch");
    reset_->setAccessibleName("Reset all search fields");

    results_ = new QLabel{this};
    results_->setObjectName("trackSearchResults");
    results_->setAccessibleName("Search result count");

    title_->setPlaceholderText("Search track titles…");
    title_->setClearButtonEnabled(true);
    clear_title_->hide();

    more_ = new QToolButton{this};
    more_->setObjectName("advancedTrackSearch");
    more_->setText("More filters");
    more_->setCheckable(true);
    more_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more_->setArrowType(Qt::RightArrow);

    advanced_ = new QWidget{this};

    auto* fields = new QFormLayout{advanced_};
    fields->setContentsMargins(0, 0, 0, 0);

    const auto add_field = [fields](const QString& label, QLineEdit* input, QPushButton* clear) {
        input->setClearButtonEnabled(true);
        clear->hide();
        fields->addRow(label, input);
    };
    add_field("Artist", artist_, clear_artist_);
    add_field("Album", album_, clear_album_);
    add_field("Tags", tags_, clear_tags_);
    advanced_->hide();
    connect(more_, &QToolButton::toggled, this, [this](bool expanded) {
        advanced_->setVisible(expanded);
        more_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });

    auto* search = new QHBoxLayout;
    search->addWidget(title_, 1);
    search->addWidget(more_);
    search->addWidget(reset_);

    auto* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(search);
    layout->addWidget(advanced_);
    layout->addWidget(results_);

    const auto changed = [this] {
        if (observer_)
            observer_(query());
    };

    connect(title_, &QLineEdit::textChanged, this, changed);
    connect(artist_, &QLineEdit::textChanged, this, changed);
    connect(album_, &QLineEdit::textChanged, this, changed);
    connect(tags_, &QLineEdit::textChanged, this, changed);
    connect(clear_tags_, &QPushButton::clicked, tags_, &QLineEdit::clear);
    connect(clear_title_, &QPushButton::clicked, title_, &QLineEdit::clear);
    connect(clear_artist_, &QPushButton::clicked, artist_, &QLineEdit::clear);
    connect(clear_album_, &QPushButton::clicked, album_, &QLineEdit::clear);
    connect(reset_, &QPushButton::clicked, this, &TrackSearchPanel::reset);

    show_results(0, 0);
}

library::SearchQuery TrackSearchPanel::query() const
{
    return {.title = title_->text().toStdString(),
        .artist = artist_->text().toStdString(),
        .album = album_->text().toStdString(),
        .tags = tags_->text().toStdString(),
        .filters = {}};
}

void TrackSearchPanel::show_query(const library::SearchQuery& query)
{
    const QSignalBlocker title{title_}, artist{artist_}, album{album_}, tags{tags_};
    title_->setText(QString::fromStdString(query.title));
    artist_->setText(QString::fromStdString(query.artist));
    album_->setText(QString::fromStdString(query.album));
    tags_->setText(QString::fromStdString(query.tags));
    more_->setChecked(!query.artist.empty() || !query.album.empty() || !query.tags.empty());
}

void TrackSearchPanel::show_results(std::size_t visible, std::size_t total, bool pending)
{
    results_->setText(pending ? QString{"Updating results…"}
                              : QString{"%1 of %2 tracks"}.arg(visible).arg(total));
    results_->setAccessibleDescription(results_->text());
}

void TrackSearchPanel::on_changed(Observer observer)
{
    observer_ = std::move(observer);
}

void TrackSearchPanel::reset()
{
    show_query({});
    if (observer_)
        observer_(query());
}

} // namespace wiola::gui
