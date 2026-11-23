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

#include <QEvent>
#include <QFormLayout>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace wiola::gui {

namespace {

QLabel* value(QWidget* parent, const QString& name)
{
    QLabel* label = new QLabel{parent};
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
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
{
    setObjectName("trackInspector");
    setAccessibleName("Track inspector");
    setMinimumWidth(200);

    context_label_ = value(this, "inspectorContext");
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
    layout->addWidget(context_label_);
    layout->addWidget(artwork_label_);
    layout->addLayout(fields);
    const auto disclosure = [this, layout](const char* name, const QString& text,
                                QWidget* content) {
        auto* toggle = new QToolButton{this};
        toggle->setObjectName(name);
        toggle->setText(text);
        toggle->setAccessibleName(text);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setCheckable(true);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(toggle);
        layout->addWidget(content);
        content->hide();
        connect(toggle, &QToolButton::toggled, content, [toggle, content](bool expanded) {
            content->setVisible(expanded);
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
        return toggle;
    };
    metadata_toggle_ =
        disclosure("metadataDisclosure", "Edit application metadata", standard_metadata_);
    layout->addWidget(conflicts_);
    tags_toggle_ = disclosure("customTagsDisclosure", "Edit shared custom tags", custom_tags_);
    layout->addStretch();

    connect(standard_metadata_->findChild<QPushButton*>("cancelStandardMetadata"),
        &QPushButton::clicked, this, [this] {
            if (pending_context_) {
                auto next = std::move(*pending_context_);
                pending_context_.reset();
                show_context(std::move(next));
            }
        });
    show_context({});
}

void TrackInspector::show_context(InspectorContext next)
{
    std::vector<const library::TrackRecord*> records;
    for (const auto& record : next.editable)
        records.push_back(&record);
    standard_metadata_->show_tracks(records);
    if (standard_metadata_->retains_draft()) {
        metadata_toggle_->setChecked(true);
        pending_context_ = std::move(next);
        context_label_->setText(
            "Editing original selection — draft retained. Cancel to inspect the current selection."
        );
        return;
    }
    context_ = std::move(next);
    pending_context_.reset();
    const bool single = context_.tracks.size() == 1;
    const auto* editable =
        single && context_.editable.size() == 1 ? &context_.editable.front() : nullptr;
    custom_tags_->show_track(editable);
    context_label_->setText(context_.origin == InspectorContext::Origin::playing
            ? "Now Playing — no explicit selection"
            : context_.origin == InspectorContext::Origin::selection
            ? QString{"Selection — %1 track(s)"}.arg(context_.tracks.size())
            : "No selection");
    if (!single) {
        artwork_label_->setPixmap(artwork_->cover(*this, {}, media::ArtworkReference{},
            {.cover_size = 96}));
        title_->setText(context_.tracks.empty()
                ? "No track selected"
                : QString{"%1 selected tracks — mixed values"}.arg(context_.tracks.size()));
        artist_->clear();
        album_->clear();
        location_->clear();
        duration_->clear();
        availability_->clear();
        availability_->setProperty("missing", false);
        return;
    }
    const auto& track = context_.tracks.front();
    show_artwork();
    title_->setText(fallback(track.metadata.title, "Unknown title"));
    artist_->setText(fallback(track.metadata.artist, "Unknown artist"));
    album_->setText(fallback(track.metadata.album, "Unknown album"));
    location_->setText(platform::qt::QtPathAdapter::to_qt(track.location));
    duration_->setText(track.metadata.duration ? as_clock(*track.metadata.duration)
                                               : QString{"Unknown"});
    const bool known = editable != nullptr;
    availability_->setText(known ? (track.missing ? "Missing" : "Available") : "Not checked");
    availability_->setProperty("missing", known && track.missing);
}

void TrackInspector::show_artwork()
{
    if (!artwork_label_)
        return;
    if (context_.tracks.size() != 1)
        artwork_label_->setPixmap(artwork_->cover(*this, {}, media::ArtworkReference{},
            {.cover_size = 96}));
    else {
        const auto& row = context_.tracks.front();
        artwork_label_->setPixmap(artwork_->cover(*this, row.media, row.artwork, {.cover_size = 96},
            [this, id = row.media] {
                if (artwork_request_)
                    artwork_request_(id);
            }));
    }
}

void TrackInspector::refresh_artwork(const library::MediaId& media, media::ArtworkReference artwork)
{
    for (auto& row : context_.tracks)
        if (row.media == media &&
            (artwork.availability() != media::ArtworkAvailability::failed || !row.artwork))
            row.artwork = artwork;
    show_artwork();
}

void TrackInspector::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        show_artwork();
}

} // namespace wiola::gui
