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

#include <algorithm>
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
    save_file_ = new QPushButton{"Apply and save to file", this};
    save_file_->setObjectName("saveMetadataToFile");
    save_file_->setAccessibleName("Apply changes and save embedded metadata to file");
    save_file_->setToolTip("Writes changed fields when the format supports them. Application "
                           "overrides remain if file writing fails.");
    commands->addWidget(save_file_);
    file_status_ = new QLabel{this};
    file_status_->setObjectName("metadataFileStatus");
    file_status_->setWordWrap(true);
    file_status_->setText("File writing is explicit. Format support is checked before writing.");
    connect(save_file_, &QPushButton::clicked, this, [this] {
        if (file_observer_ && dirty() && valid_numbers()) {
            session_.set_draft(patch());
            file_observer_(session_);
        }
    });
    commands->addStretch();

    QVBoxLayout* layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(fields);
    layout->addLayout(commands);
    layout->addWidget(status_);
    layout->addWidget(file_status_);

    for (QLineEdit* input : {title_, artist_, album_, genre_, year_, comment_, composer_,
             track_number_, disc_number_}) {
        connect(input, &QLineEdit::textChanged, this, [this, input] {
            if (populating_)
                return;

            touched_.insert(input);
            feedback_.clear();
            if (valid_numbers())
                session_.set_draft(patch());
            update_actions();
        });
    }

    connect(apply_, &QPushButton::clicked, this, [this] {
        if (observer_ && dirty() && valid_numbers()) {
            session_.set_draft(patch());
            observer_(session_);
        }
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { discard(); });

    show_track(nullptr);
}

void StandardMetadataEditor::show_track(const library::TrackRecord* track)
{
    show_tracks(track != nullptr ? std::vector<const library::TrackRecord*>{track}
                                 : std::vector<const library::TrackRecord*>{});
}

void StandardMetadataEditor::show_tracks(const std::vector<const library::TrackRecord*>& tracks)
{
    show_session(app::MetadataEditSession::capture(tracks));
}

void StandardMetadataEditor::show_session(app::MetadataEditSession next)
{
    offered_ = std::move(next);
    if (retains_draft()) {
        if (!session_.same_targets(offered_))
            show_status(
                "Draft retained for the original selection. Apply to that selection, or Cancel to "
                "inspect the current selection. External changes are checked on Apply."
            );
        return;
    }
    if (initialized_ && session_.same_targets(offered_))
        return;
    discard();
}

void StandardMetadataEditor::discard()
{
    session_ = offered_;
    initialized_ = true;
    mixed_.clear();
    const auto mix = [&](QLineEdit* field, app::StandardMetadataField id) {
        if (session_.mixed(id))
            mixed_.insert(field);
    };
    mix(title_, app::StandardMetadataField::title);
    mix(artist_, app::StandardMetadataField::artist);
    mix(album_, app::StandardMetadataField::album);
    mix(genre_, app::StandardMetadataField::genre);
    mix(year_, app::StandardMetadataField::year);
    mix(comment_, app::StandardMetadataField::comment);
    mix(composer_, app::StandardMetadataField::composer);
    mix(track_number_, app::StandardMetadataField::track_number);
    mix(disc_number_, app::StandardMetadataField::disc_number);
    feedback_.clear();
    populate();
}

library::ResolvedMetadata StandardMetadataEditor::current() const
{
    library::ResolvedMetadata current = session_.common();
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
    return session_.valid() && !touched_.empty() &&
        (current() != session_.common() ||
            std::ranges::any_of(touched_, [this](auto* field) { return mixed_.contains(field); }));
}

void StandardMetadataEditor::populate()
{
    populating_ = true;
    touched_.clear();

    for (QLineEdit* input :
        {title_, artist_, album_, genre_, year_, comment_, composer_, track_number_, disc_number_})
        input->setPlaceholderText(mixed_.contains(input) ? "Mixed values" : QString{});

    title_->setText(qt(session_.common().title));
    artist_->setText(qt(session_.common().artist));
    album_->setText(qt(session_.common().album));
    genre_->setText(qt(session_.common().genre));
    year_->setText(number(session_.common().year));
    comment_->setText(qt(session_.common().comment));
    composer_->setText(qt(session_.common().composer));
    track_number_->setText(number(session_.common().track_number));
    disc_number_->setText(number(session_.common().disc_number));
    populating_ = false;
    update_actions();
}

void StandardMetadataEditor::update_actions()
{
    for (QLineEdit* input :
        {title_, artist_, album_, genre_, year_, comment_, composer_, track_number_, disc_number_})
        input->setEnabled(session_.valid());

    const bool changed = dirty();
    apply_->setEnabled(changed && valid_numbers());
    save_file_->setEnabled(changed && valid_numbers());
    cancel_->setEnabled(!touched_.empty());

    if (!session_.valid())
        status_->setText("Select a track to edit standard metadata.");
    else if (!feedback_.isEmpty())
        status_->setText(feedback_);
    else if (!valid_numbers())
        status_->setText("Year and track/disc numbers must be non-negative integers.");
    else if (changed)
        status_->setText("Unsaved application changes. Embedded file data is unchanged.");
    else if (session_.overrides_present())
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
