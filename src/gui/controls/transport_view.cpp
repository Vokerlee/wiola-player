/**
 * @file
 * @brief A length of time, written the way a listener reads it.
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

#include "transport_view.hpp"

#include "clock.hpp"
#include "seek_bar.hpp"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <gui/appearance/presentation_tokens.hpp>
#include <gui/layout/view_registry.hpp>
#include <gui/shell/action_button_binding.hpp>
#include <media/local/resource.hpp>
#include <qt/paths.hpp>
#include <stdexcept>

namespace wiola::gui {

TransportView::TransportView(PlayerActions& actions, QWidget* parent)
    : QGroupBox{"Now playing", parent}
    , actions_{actions}
{
    const auto tokens = PresentationTokens{}.player;
    auto* body = new QVBoxLayout{this};
    title_ = new QLabel{"no track", this};
    title_->setTextFormat(Qt::PlainText);
    title_->setAccessibleName("Current track");
    auto font = title_->font();
    font.setPointSize(font.pointSize() + tokens.track_name_point_size_step);
    font.setBold(true);
    title_->setFont(font);
    seek_ = new SeekBar{this};
    elapsed_ = new QLabel{this};
    duration_ = new QLabel{this};
    elapsed_->setAccessibleName("Elapsed time");
    duration_->setAccessibleName("Total duration");
    duration_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    body->addWidget(title_);
    body->addWidget(seek_);
    auto* times = new QHBoxLayout;
    times->addWidget(elapsed_);
    times->addWidget(duration_);
    body->addLayout(times);
    auto* controls = new QHBoxLayout;
    body->addLayout(controls);
    for (const auto& descriptor : action_descriptors()) {
        auto* button = new QPushButton{this};
        const bool icon = descriptor.icon.has_value();
        new ActionButtonBinding{*button, *actions_.action(descriptor.id),
            icon ? ActionButtonPresentation::icon_only : ActionButtonPresentation::text};
        if (icon)
            button->setMinimumWidth(descriptor.id == PlayerAction::play ? tokens.play_button_width
                                                                        : tokens.icon_button_width);
        if (descriptor.id == PlayerAction::equalizer) {
            button->setAccessibleName("Equalizer");
            button->setToolTip("Open equalizer");
        }
        buttons_[static_cast<std::size_t>(descriptor.id)] = button;
        if (descriptor.id == PlayerAction::previous || descriptor.id == PlayerAction::play ||
            descriptor.id == PlayerAction::next || descriptor.id == PlayerAction::stop)
            controls->addWidget(button);
        else
            button->hide();
    }
    connect(seek_, &SeekBar::seek_requested, this, &TransportView::seek_requested);
    connect(seek_, &SeekBar::dragged, this, &TransportView::show_time);
}

void TransportView::register_views(ViewRegistry& registry)
{
    const auto add = [&](const char* key, QWidget* widget, LayoutComponentKind kind,
                         bool structure = false) {
        if (!registry.add(component_view(key), widget, kind, structure))
            throw std::logic_error{"duplicate transport view"};
    };
    add("transportSection", this, LayoutComponentKind::group, true);
    add("trackLabel", title_, LayoutComponentKind::status);
    add("positionBar", seek_, LayoutComponentKind::transport);
    add("elapsedTime", elapsed_, LayoutComponentKind::status);
    add("totalDuration", duration_, LayoutComponentKind::status);
    for (const auto& descriptor : legacy_view_descriptors())
        if (descriptor.action)
            add(descriptor.key, buttons_[static_cast<std::size_t>(*descriptor.action)],
                LayoutComponentKind::button);
}

void TransportView::present(const app::PlayerPresentation& state)
{
    const bool changed = state.playback.selection_revision != state_.playback.selection_revision ||
        state.playback.track != state_.playback.track ||
        state.playback.track.label() != state_.playback.track.label();
    state_ = state;
    if (changed)
        seek_->set_fraction(0);
    const auto& playback = state_.playback;
    actions_.set_playing(playback.loaded && playback.playing);
    actions_.set_repeat(state.repeat);
    actions_.set_shuffled(state.shuffled);
    for (const auto action :
        {PlayerAction::play, PlayerAction::stop, PlayerAction::previous, PlayerAction::next})
        actions_.set_available(action, playback.loaded);
    seek_->setEnabled(playback.loaded && playback.seekable && playback.total_time &&
        *playback.total_time > units::Time{});
    QString title{"no track"};
    if (playback.loaded) {
        const auto path = media::local_path(playback.track);
        title = path ? platform::qt::QtPathAdapter::to_qt(path->filename())
                     : QString::fromStdString(playback.track.label());
    }
    title_->setText(title);
    show_time();
}

void TransportView::show_time()
{
    const auto& playback = state_.playback;
    if (!playback.loaded) {
        elapsed_->clear();
        duration_->clear();
        seek_->set_fraction(0);
        return;
    }
    const auto total = playback.total_time;
    const auto shown =
        seek_->dragging() && total ? *total * seek_->fraction() : playback.time_played;
    elapsed_->setText(as_clock(shown));
    duration_->setText(total ? as_clock(*total) : QString{"--:--"});
    if (total && *total > units::Time{})
        seek_->set_fraction(shown.get<units::Sec>() / total->get<units::Sec>());
}

} // namespace wiola::gui
