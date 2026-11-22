/**
 * @file
 * @brief Dirty-aware standard metadata editor with validation and cancellation.
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

#include "standard_metadata_editor.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <charconv>
#include <type_traits>

namespace wiola::gui {

namespace {

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

QString number(std::optional<std::uint32_t> value)
{
    return value ? QString::number(*value) : QString{};
}

std::optional<std::uint32_t> number(const QString& text)
{
    const std::string value = text.toStdString();
    std::uint32_t parsed{0};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);

    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size())
        return std::nullopt;

    return parsed;
}

bool valid_number(const QString& text)
{
    return text.isEmpty() || number(text).has_value();
}

QLineEdit* field(QWidget* parent, const char* name, const QString& placeholder)
{
    QLineEdit* input = new QLineEdit{parent};
    input->setObjectName(name);
    input->setAccessibleName(placeholder);
    input->setPlaceholderText(placeholder);
    input->setMaxLength(1024);
    return input;
}

bool any_override(const library::StandardFieldOverrides& overrides)
{
    return overrides.title || overrides.artist || overrides.album || overrides.genre ||
        overrides.comment || overrides.composer || overrides.year || overrides.track_number ||
        overrides.disc_number;
}

} // namespace

StandardMetadataEditor::StandardMetadataEditor(QWidget* parent)
    : QWidget{parent}
{
    setObjectName("standardMetadataEditor");

    status_ = new QLabel{this};
    status_->setObjectName("standardMetadataStatus");
    status_->setWordWrap(true);

    title_ = field(this, "standardTitle", "Title");
    artist_ = field(this, "standardArtist", "Artist");
    album_ = field(this, "standardAlbum", "Album");
    genre_ = field(this, "standardGenre", "Genre");
    year_ = field(this, "standardYear", "Year");
    comment_ = field(this, "standardComment", "Comment");
    composer_ = field(this, "standardComposer", "Composer");
    track_number_ = field(this, "standardTrackNumber", "Track number");
    disc_number_ = field(this, "standardDiscNumber", "Disc number");

    apply_ = new QPushButton{"Apply", this};
    apply_->setObjectName("applyStandardMetadata");
    apply_->setAccessibleName("Apply standard metadata changes");

    cancel_ = new QPushButton{"Cancel", this};
    cancel_->setObjectName("cancelStandardMetadata");
    cancel_->setAccessibleName("Cancel standard metadata changes");

    QFormLayout* fields = new QFormLayout;
    fields->addRow("Title", title_);
    fields->addRow("Artist", artist_);
    fields->addRow("Album", album_);
    fields->addRow("Genre", genre_);
    fields->addRow("Year", year_);
    fields->addRow("Comment", comment_);
    fields->addRow("Composer", composer_);
    fields->addRow("Track", track_number_);
    fields->addRow("Disc", disc_number_);

    QHBoxLayout* commands = new QHBoxLayout;
    commands->addWidget(apply_);
    commands->addWidget(cancel_);
    commands->addStretch();

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(fields);
    layout->addLayout(commands);
    layout->addWidget(status_);

    for (QLineEdit* input : {title_, artist_, album_, genre_, year_, comment_, composer_,
             track_number_, disc_number_}) {
        connect(input, &QLineEdit::textChanged, this, [this, input] {
            if (populating_)
                return;

            touched_.insert(input);
            feedback_.clear();
            update_actions();
        });
    }

    connect(apply_, &QPushButton::clicked, this, [this] {
        if (observer_ && dirty() && valid_numbers())
            observer_(media_, patch());
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        feedback_.clear();
        populate();
    });

    show_track(nullptr);
}

void StandardMetadataEditor::show_track(const library::TrackRecord* track)
{
    show_tracks(track != nullptr ? std::vector<const library::TrackRecord*>{track}
                                 : std::vector<const library::TrackRecord*>{});
}

void StandardMetadataEditor::show_tracks(const std::vector<const library::TrackRecord*>& tracks)
{
    std::vector<library::ResolvedMetadata> resolved;
    std::string next_key;
    library::MediaId next_media;
    bool next_overrides = false;

    for (const library::TrackRecord* track : tracks) {
        if (track == nullptr)
            continue;

        if (!next_media.valid())
            next_media = track->id;

        next_key += track->id.value + ":" + std::to_string(track->metadata_revision) + ";";
        next_overrides = next_overrides || any_override(track->overrides);
        resolved.push_back(library::resolve_metadata(*track));
    }

    if (initialized_ && selection_key_ == next_key)
        return;

    media_ = next_media;
    selection_key_ = next_key;
    initialized_ = true;
    overrides_present_ = next_overrides;
    original_ = resolved.empty() ? library::ResolvedMetadata{} : resolved.front();
    mixed_.clear();

    for (std::size_t index = 1; index < resolved.size(); ++index) {
        const auto& current = resolved[index];
        const auto mix = [this](QLineEdit* field, auto& original, const auto& value) {
            if (original != value) {
                original = std::decay_t<decltype(original)>{};
                mixed_.insert(field);
            }
        };

        mix(title_, original_.title, current.title);
        mix(artist_, original_.artist, current.artist);
        mix(album_, original_.album, current.album);
        mix(genre_, original_.genre, current.genre);
        mix(year_, original_.year, current.year);
        mix(comment_, original_.comment, current.comment);
        mix(composer_, original_.composer, current.composer);
        mix(track_number_, original_.track_number, current.track_number);
        mix(disc_number_, original_.disc_number, current.disc_number);
    }

    feedback_.clear();
    populate();
}

library::ResolvedMetadata StandardMetadataEditor::current() const
{
    library::ResolvedMetadata current = original_;
    current.title = title_->text().toStdString();
    current.artist = artist_->text().toStdString();
    current.album = album_->text().toStdString();
    current.genre = genre_->text().toStdString();
    current.year = number(year_->text());
    current.comment = comment_->text().toStdString();
    current.composer = composer_->text().toStdString();
    current.track_number = number(track_number_->text());
    current.disc_number = number(disc_number_->text());
    return current;
}

library::StandardMetadataPatch StandardMetadataEditor::patch() const
{
    const library::ResolvedMetadata values = current();
    library::StandardMetadataPatch result;
    const auto assign = [this](QLineEdit* field, auto& target, const auto& value) {
        if (!touched_.contains(field))
            return;
        using Edit = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<typename Edit::Value, std::string>)
            target = value;
        else
            target = value ? Edit{*value} : Edit::clear();
    };

    assign(title_, result.title, values.title);
    assign(artist_, result.artist, values.artist);
    assign(album_, result.album, values.album);
    assign(genre_, result.genre, values.genre);
    assign(year_, result.year, values.year);
    assign(comment_, result.comment, values.comment);
    assign(composer_, result.composer, values.composer);
    assign(track_number_, result.track_number, values.track_number);
    assign(disc_number_, result.disc_number, values.disc_number);
    return result;
}

bool StandardMetadataEditor::valid_numbers() const
{
    return valid_number(year_->text()) && valid_number(track_number_->text()) &&
        valid_number(disc_number_->text());
}

bool StandardMetadataEditor::dirty() const
{
    return media_.valid() && !touched_.empty() && current() != original_;
}

void StandardMetadataEditor::populate()
{
    populating_ = true;
    touched_.clear();

    for (QLineEdit* input :
        {title_, artist_, album_, genre_, year_, comment_, composer_, track_number_, disc_number_})
        input->setPlaceholderText(mixed_.contains(input) ? "Mixed values" : QString{});

    title_->setText(qt(original_.title));
    artist_->setText(qt(original_.artist));
    album_->setText(qt(original_.album));
    genre_->setText(qt(original_.genre));
    year_->setText(number(original_.year));
    comment_->setText(qt(original_.comment));
    composer_->setText(qt(original_.composer));
    track_number_->setText(number(original_.track_number));
    disc_number_->setText(number(original_.disc_number));
    populating_ = false;
    update_actions();
}

void StandardMetadataEditor::update_actions()
{
    for (QLineEdit* input :
        {title_, artist_, album_, genre_, year_, comment_, composer_, track_number_, disc_number_})
        input->setEnabled(media_.valid());

    const bool changed = dirty();
    apply_->setEnabled(changed && valid_numbers());
    cancel_->setEnabled(!touched_.empty());

    if (!media_.valid())
        status_->setText("Select a track to edit standard metadata.");
    else if (!feedback_.isEmpty())
        status_->setText(feedback_);
    else if (!valid_numbers())
        status_->setText("Year and track/disc numbers must be non-negative integers.");
    else if (changed)
        status_->setText("Unsaved application changes. Embedded file data is unchanged.");
    else if (overrides_present_)
        status_->setText("Application overrides are active. Embedded file data is unchanged.");
    else
        status_->setText("Embedded values. Edits are stored in the application.");
}

void StandardMetadataEditor::show_status(const QString& status)
{
    feedback_ = status;
    update_actions();
}

void StandardMetadataEditor::on_apply(Observer observer)
{
    observer_ = std::move(observer);
}

} // namespace wiola::gui
