/**
 * @file
 * @brief Read-only presentation of track metadata, artwork, and availability.
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

#include "track_inspector.hpp"

#include <gui/controls/clock.hpp>
#include <platform/qt/qt_path_adapter.hpp>

#include <library/location.hpp>

#include <QFormLayout>
#include <QVBoxLayout>

#include <filesystem>

namespace wiola::gui {

namespace {

QLabel* value(QWidget* parent, const QString& name)
{
    QLabel* label = new QLabel{parent};
    label->setObjectName(name);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setWordWrap(true);

    return label;
}

QString fallback(const std::string& text, const QString& empty)
{
    return text.empty() ? empty : QString::fromStdString(text);
}

} // namespace

TrackInspector::TrackInspector(QWidget* parent)
    : QWidget{parent}
    , artwork_{*this, {.cover_size = 96}}
{
    setObjectName("trackInspector");
    setAccessibleName("Track inspector");
    setMinimumWidth(200);

    artwork_label_ = new QLabel{this};
    artwork_label_->setObjectName("inspectorArtwork");
    artwork_label_->setAlignment(Qt::AlignCenter);

    title_ = value(this, "inspectorTitle");
    artist_ = value(this, "inspectorArtist");
    album_ = value(this, "inspectorAlbum");
    location_ = value(this, "inspectorLocation");
    duration_ = value(this, "inspectorDuration");
    availability_ = value(this, "inspectorAvailability");

    QFont heading = title_->font();
    heading.setBold(true);
    title_->setFont(heading);

    QFormLayout* fields = new QFormLayout;
    fields->addRow("Title", title_);
    fields->addRow("Artist", artist_);
    fields->addRow("Album", album_);
    fields->addRow("Location", location_);
    fields->addRow("Duration", duration_);
    fields->addRow("File", availability_);

    custom_tags_ = new CustomTagEditor{this};
    standard_metadata_ = new StandardMetadataEditor{this};
    conflicts_ = new MetadataConflictPanel{this};

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->addWidget(artwork_label_);
    layout->addLayout(fields);
    layout->addWidget(standard_metadata_);
    layout->addWidget(conflicts_);
    layout->addWidget(custom_tags_);
    layout->addStretch();

    show_track(nullptr);
}

void TrackInspector::show_track(const engine::Track* track)
{
    if (track == nullptr) {
        artwork_label_->setPixmap(artwork_.cover_of({}, {}));
        title_->setText("No track selected");
        artist_->clear();
        album_->clear();
        location_->clear();
        duration_->clear();
        availability_->clear();
        availability_->setProperty("missing", false);
        return;
    }

    std::error_code error;
    const bool missing = !std::filesystem::exists(track->path(), error);

    artwork_label_->setPixmap(artwork_.cover_of(
        {.media = library::media_id_for(track->path()).value, .revision = track->revision()},
        track->artwork()
    ));
    title_->setText(fallback(track->title(), "Unknown title"));
    artist_->setText(fallback(track->artist(), "Unknown artist"));
    album_->setText(fallback(track->album(), "Unknown album"));
    location_->setText(platform::qt::QtPathAdapter::to_qt(track->path()));
    duration_->setText(track->duration() ? as_clock(*track->duration()) : QString{"Unknown"});
    availability_->setText(missing ? "Missing" : "Available");
    availability_->setProperty("missing", missing);
}

void TrackInspector::show_custom_tags(const library::TrackRecord* track)
{
    custom_tags_->show_track(track);
}

void TrackInspector::show_standard_metadata(const std::vector<const library::TrackRecord*>& tracks)
{
    standard_metadata_->show_tracks(tracks);
}

} // namespace wiola::gui
