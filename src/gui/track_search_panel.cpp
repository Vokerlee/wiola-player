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

    reset_ = new QPushButton{"Reset search", this};
    reset_->setObjectName("resetTrackSearch");
    reset_->setAccessibleName("Reset all search fields");

    results_ = new QLabel{this};
    results_->setObjectName("trackSearchResults");
    results_->setAccessibleName("Search result count");

    QFormLayout* fields = new QFormLayout;
    const auto add_field = [fields](const QString& label, QLineEdit* input, QPushButton* clear) {
        QHBoxLayout* row = new QHBoxLayout;
        row->addWidget(input);
        row->addWidget(clear);
        fields->addRow(label, row);
    };

    add_field("Title", title_, clear_title_);
    add_field("Artist", artist_, clear_artist_);
    add_field("Album", album_, clear_album_);

    QHBoxLayout* footer = new QHBoxLayout;
    footer->addWidget(results_);
    footer->addStretch();
    footer->addWidget(reset_);

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(fields);
    layout->addLayout(footer);

    const auto changed = [this] {
        if (observer_)
            observer_(query());
    };

    connect(title_, &QLineEdit::textChanged, this, changed);
    connect(artist_, &QLineEdit::textChanged, this, changed);
    connect(album_, &QLineEdit::textChanged, this, changed);
    connect(clear_title_, &QPushButton::clicked, title_, &QLineEdit::clear);
    connect(clear_artist_, &QPushButton::clicked, artist_, &QLineEdit::clear);
    connect(clear_album_, &QPushButton::clicked, album_, &QLineEdit::clear);
    connect(reset_, &QPushButton::clicked, this, &TrackSearchPanel::reset);

    show_results(0, 0);
}

library::SearchQuery TrackSearchPanel::query() const
{
    return library::normalized_search_query({.title = title_->text().toStdString(),
        .artist = artist_->text().toStdString(),
        .album = album_->text().toStdString(),
        .tags = {}});
}

void TrackSearchPanel::show_results(std::size_t visible, std::size_t total)
{
    results_->setText(QString{"%1 of %2 tracks"}.arg(visible).arg(total));
}

void TrackSearchPanel::on_changed(Observer observer)
{
    observer_ = std::move(observer);
}

void TrackSearchPanel::reset()
{
    title_->clear();
    artist_->clear();
    album_->clear();
}

} // namespace wiola::gui
