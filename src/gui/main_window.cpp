/**
 * @file
 * @brief The window a listener drives one track from.
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

#include "main_window.hpp"

#include "clock.hpp"
#include "player_settings.hpp"
#include "presentation_tokens.hpp"
#include "qt_path_adapter.hpp"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

namespace wiola::gui {

namespace {

constexpr PresentationTokens tokens;

/// What to say about a file that would not open.
QString as_status(engine::OpenResult result)
{
    switch (result) {
    case engine::OpenResult::unsupported:
        return QString{"unsupported format"};

    case engine::OpenResult::damaged:
        return QString{"that file is damaged"};

    case engine::OpenResult::unreadable:
        return QString{"cannot read that file"};

    default:
        return QString{"cannot read that file"};
    }
}

} // namespace

MainWindow::MainWindow(engine::Session& session)
    // A file of its own on both platforms, rather than the registry on one of them.
    : presenter_{session}
    , actions_{*this}
    , settings_{QSettings::IniFormat, QSettings::UserScope, "wiola-player", "settings"}
    , playlist_{presenter_, settings_}
    , volume_{presenter_.volume_control(), settings_}
    , equalizer_{presenter_.equalizer_control(), settings_}
{
    setWindowTitle("Wiola Player");
    setMinimumWidth(tokens.player.window_min_width);

    auto* central = new QWidget{this};
    setCentralWidget(central);

    // Every widget is given this window as its parent, so Qt owns them and destroys them with it.
    open_button_ = new QPushButton{this};
    previous_button_ = new QPushButton{this};
    play_button_ = new QPushButton{this};
    next_button_ = new QPushButton{this};
    stop_button_ = new QPushButton{this};

    connect_action(*open_button_, PlayerAction::open, true);
    connect_action(*previous_button_, PlayerAction::previous, true);
    connect_action(*play_button_, PlayerAction::play, true);
    connect_action(*next_button_, PlayerAction::next, true);
    connect_action(*stop_button_, PlayerAction::stop, true);

    show_play_button(false);

    play_button_->setFixedWidth(tokens.player.play_button_width);
    repeat_button_ = new QPushButton{this};
    shuffle_button_ = new QPushButton{this};
    add_button_ = new QPushButton{this};
    remove_button_ = new QPushButton{this};
    clear_button_ = new QPushButton{this};

    connect_action(*repeat_button_, PlayerAction::repeat);
    connect_action(*shuffle_button_, PlayerAction::shuffle);
    connect_action(*add_button_, PlayerAction::add);
    connect_action(*remove_button_, PlayerAction::remove);
    connect_action(*clear_button_, PlayerAction::clear);

    equalizer_button_ = new QPushButton{this};
    connect_action(*equalizer_button_, PlayerAction::equalizer);

    boost_button_ = new QPushButton{"+", this};
    volume_value_ = new QLabel{this};
    position_bar_ = new SeekBar{this};
    now_playing_ = new NowPlayingDocument{this};
    track_label_ = new QLabel{this};
    elapsed_label_ = new QLabel{this};
    total_label_ = new QLabel{this};
    status_label_ = new QLabel{this};
    volume_slider_ = new QSlider{Qt::Horizontal, this};

    const PlayerSettings restored{QtSettingsAdapter{settings_}.read(volume_.scale(),
        presenter_.equalizer_control().config())};
    volume_.apply_restored(restored.volume_position, restored.boosted);
    equalizer_.apply_restored(restored.equalizer_enabled, restored.band_gains);
    const int position{volume_.position()};

    boost_button_->setCheckable(true);
    boost_button_->setChecked(volume_.boosted());
    boost_button_->setFixedWidth(tokens.player.boost_button_width);
    boost_button_->setToolTip("Past full volume");

    volume_value_->setFixedWidth(tokens.player.volume_value_width);

    volume_slider_->setRange(0, volume_.max_position());
    volume_slider_->setValue(position);

    show_volume();
    volume_slider_->setFixedWidth(tokens.player.volume_slider_width);
    volume_slider_->setToolTip("Volume");

    QFont title{track_label_->font()};
    title.setPointSize(title.pointSize() + tokens.player.track_name_point_size_step);
    title.setBold(true);
    track_label_->setFont(title);

    total_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // What is playing: its name, where it has reached, and how far that is through it.
    auto* times = new QHBoxLayout;
    times->addWidget(elapsed_label_);
    times->addStretch();
    times->addWidget(total_label_);

    // How it plays. Everything here acts on the track that is on.
    auto* transport = new QHBoxLayout;
    transport->addWidget(previous_button_);
    transport->addWidget(play_button_);
    transport->addWidget(next_button_);
    transport->addWidget(stop_button_);
    transport->addStretch();
    transport->addWidget(volume_slider_);
    transport->addWidget(volume_value_);
    transport->addWidget(boost_button_);

    // What the list does. Repeat and shuffle change what plays next rather than how this one
    // plays, which is why they are not up there with the transport.
    auto* list = new QHBoxLayout;
    list->addWidget(open_button_);
    list->addWidget(repeat_button_);
    list->addWidget(shuffle_button_);
    list->addStretch();
    list->addWidget(equalizer_button_);

    auto* layout = new QVBoxLayout{central};
    layout->addWidget(track_label_);
    layout->addWidget(position_bar_);
    layout->addLayout(times);
    layout->addLayout(transport);
    layout->addLayout(list);
    layout->addWidget(now_playing_);

    // What is done to the queue, under the queue it is done to.
    auto* queue = new QHBoxLayout;
    queue->addWidget(add_button_);
    queue->addWidget(remove_button_);
    queue->addWidget(clear_button_);
    queue->addStretch();

    layout->addLayout(queue);

    // Kept in the layout while it says nothing, so a message appearing does not resize the window.
    layout->addWidget(status_label_);

    connect(actions_.action(PlayerAction::open), &QAction::triggered, this,
        &MainWindow::choose_tracks);
    connect(actions_.action(PlayerAction::previous), &QAction::triggered, this,
        &MainWindow::play_previous);
    connect(actions_.action(PlayerAction::next), &QAction::triggered, this, &MainWindow::play_next);
    connect(actions_.action(PlayerAction::play), &QAction::triggered, this,
        &MainWindow::toggle_playback);
    connect(actions_.action(PlayerAction::stop), &QAction::triggered, this,
        &MainWindow::stop_playback);
    connect(actions_.action(PlayerAction::repeat), &QAction::triggered, this,
        &MainWindow::cycle_repeat);
    connect(actions_.action(PlayerAction::shuffle), &QAction::triggered, this,
        &MainWindow::set_shuffled);
    connect(actions_.action(PlayerAction::add), &QAction::triggered, this, &MainWindow::add_tracks);
    connect(actions_.action(PlayerAction::remove), &QAction::triggered, this,
        &MainWindow::remove_chosen);
    connect(actions_.action(PlayerAction::clear), &QAction::triggered, this,
        &MainWindow::clear_queue);
    connect(actions_.action(PlayerAction::equalizer), &QAction::triggered, this,
        &MainWindow::show_equalizer);

    QMenu* file = menuBar()->addMenu("&File");
    file->addAction(actions_.action(PlayerAction::open));
    file->addAction(actions_.action(PlayerAction::add));
    file->addAction(actions_.action(PlayerAction::clear));

    QMenu* playback = menuBar()->addMenu("&Playback");
    playback->addAction(actions_.action(PlayerAction::previous));
    playback->addAction(actions_.action(PlayerAction::play));
    playback->addAction(actions_.action(PlayerAction::next));
    playback->addAction(actions_.action(PlayerAction::stop));
    playback->addSeparator();
    playback->addAction(actions_.action(PlayerAction::repeat));
    playback->addAction(actions_.action(PlayerAction::shuffle));

    QMenu* tools = menuBar()->addMenu("&Tools");
    tools->addAction(actions_.action(PlayerAction::equalizer));

    connect(now_playing_, &NowPlayingDocument::track_chosen, this, &MainWindow::play_chosen);
    connect(now_playing_, &NowPlayingDocument::removal_asked, this, &MainWindow::remove_chosen);

    connect(volume_slider_, &QSlider::valueChanged, this, &MainWindow::set_volume);
    connect(boost_button_, &QPushButton::toggled, this, &MainWindow::set_boosted);

    connect(position_bar_, &SeekBar::seek_requested, this, &MainWindow::seek_to);

    // The clock follows the drag rather than waiting for the next poll.
    connect(position_bar_, &SeekBar::dragged, this, &MainWindow::refresh);

    if (const std::size_t gone{playlist_.restore()}; gone != 0)
        show_status(QString{"%1 queued track(s) are no longer where they were"}.arg(gone));

    // What was put back settles both of these, without either button having been pressed.
    show_list_buttons();

    refresh();
}

MainWindow::~MainWindow() = default;

void MainWindow::open(std::vector<std::filesystem::path> tracks)
{
    if (tracks.empty())
        return;

    const QString first{QtPathAdapter::to_qt(tracks.front().filename())};

    static_cast<void>(presenter_.open(std::move(tracks)));
    said_ = presenter_.presentation().playback.open_result;

    playlist_.save();
    show_status(QString{"opening "} + first + "...");

    // A new list is an unshuffled one, whether or not the listener asked for that.
    show_list_buttons();

    refresh();
}

std::vector<std::filesystem::path> MainWindow::ask_for_tracks()
{
    QStringList patterns;
    for (const std::string& extension : presenter_.playback_extensions())
        patterns.append(QString::fromStdString("*" + extension));

    const QString filter{QString{"Audio (%1);;All files (*)"}.arg(patterns.join(' '))};
    const QStringList chosen{
        QFileDialog::getOpenFileNames(this, "Choose tracks", QString{}, filter)};

    std::vector<std::filesystem::path> tracks;
    tracks.reserve(static_cast<std::size_t>(chosen.size()));

    for (const QString& name : chosen)
        tracks.push_back(QtPathAdapter::from_qt(name));

    return tracks;
}

void MainWindow::choose_tracks()
{
    // Whether the files can be read is said by playing them.
    open(ask_for_tracks());
}

void MainWindow::add_tracks()
{
    std::vector<std::filesystem::path> tracks{ask_for_tracks()};

    if (tracks.empty())
        return;

    presenter_.add(std::move(tracks));
    playlist_.save();
    refresh();
}

void MainWindow::remove_chosen()
{
    for (const engine::QueueEntryId id : now_playing_->chosen_tracks()) {
        const auto index = presenter_.playlist().index_of(id);
        if (index)
            static_cast<void>(presenter_.remove(*index));
    }

    playlist_.save();
    refresh();
}

void MainWindow::clear_queue()
{
    presenter_.clear();
    playlist_.save();

    // Emptying the queue puts the order back as it was given, shuffle button and all.
    show_list_buttons();

    refresh();
}

void MainWindow::play_chosen(engine::QueueEntryId id)
{
    const auto index = presenter_.playlist().index_of(id);
    if (index)
        static_cast<void>(presenter_.play_track(*index));

    refresh();
}

void MainWindow::play_previous()
{
    static_cast<void>(presenter_.previous_track());
    refresh();
}

void MainWindow::play_next()
{
    static_cast<void>(presenter_.next_track());
    refresh();
}

void MainWindow::cycle_repeat()
{
    presenter_.cycle_repeat();

    show_list_buttons();
}

void MainWindow::set_shuffled(bool shuffled)
{
    presenter_.shuffle(shuffled);
    show_list_buttons();
}

void MainWindow::connect_action(QPushButton& button, PlayerAction id, bool icon_only)
{
    QAction* command{actions_.action(id)};

    addAction(command);
    button.setToolTip(command->toolTip());
    button.setAccessibleName(command->text());

    if (icon_only) {
        button.setIcon(command->icon());
        button.setFixedWidth(tokens.player.icon_button_width);
    } else {
        button.setText(command->text());
        button.setCheckable(command->isCheckable());
        button.setChecked(command->isChecked());
    }

    connect(&button, &QPushButton::clicked, command, &QAction::trigger);
}

void MainWindow::show_play_button(bool pausing)
{
    if (showing_pause_ == pausing)
        return;

    showing_pause_ = pausing;
    actions_.set_playing(pausing);

    QAction* command{actions_.action(PlayerAction::play)};

    play_button_->setIcon(command->icon());
    play_button_->setToolTip(command->toolTip());
    play_button_->setAccessibleName(command->text());
}

void MainWindow::show_list_buttons()
{
    const PlayerPresentation presentation{presenter_.presentation()};

    actions_.set_repeat(presentation.repeat);
    actions_.set_shuffled(presentation.shuffled);

    repeat_button_->setText(actions_.action(PlayerAction::repeat)->text());
    shuffle_button_->setChecked(actions_.action(PlayerAction::shuffle)->isChecked());
}

void MainWindow::update_after_pump()
{
    const engine::PlaybackSnapshot playback{presenter_.presentation().playback};
    const engine::OpenResult result{playback.open_result};

    // Said once: a window that repeated itself every turn would overwrite whatever else it has
    // to say.
    if (result != said_ && result != engine::OpenResult::loading) {
        said_ = result;

        const bool opened{result == engine::OpenResult::opened};

        show_status(opened ? QString{} : as_status(result));

        if (opened)
            position_bar_->set_fraction(0.0);
    }

    // The title follows what is playing, so a list moving on to the next track renames the
    // window without anyone asking it to.
    if (playback.track != named_ || presenter_.playlist().position() != named_place_) {
        named_ = playback.track;
        named_place_ = presenter_.playlist().position();

        const QString name{
            named_.empty() ? QString{"no track"} : QtPathAdapter::to_qt(named_.filename())};

        setWindowTitle(named_.empty() ? QString{"Wiola Player"} : name);
        track_label_->setText(name);
        position_bar_->set_fraction(0.0);
        said_fault_ = false;

        // Where the queue stands has moved, which is worth keeping even if nothing was added.
        playlist_.save();
    }

    // A device that goes away is not a listener pressing stop, and is the one thing about
    // playback worth interrupting them for.
    const bool faulted{playback.state == engine::Playback::State::faulted};

    if (faulted && !said_fault_) {
        said_fault_ = true;

        show_status(QString{"lost the playback device"});
    }
}

void MainWindow::toggle_playback()
{
    if (!loaded())
        return;

    // Pausing cannot fail, so a refusal is always an output that would not open.
    const bool pausing{presenter_.presentation().playback.playing};
    const engine::CommandResult result{presenter_.toggle_playback()};

    show_status(result == engine::CommandResult::accepted || pausing
            ? QString{}
            : QString{"no playback device"});
}

void MainWindow::stop_playback()
{
    if (!loaded())
        return;

    presenter_.stop();
}

void MainWindow::show_equalizer()
{
    if (equalizer_panel_ == nullptr)
        equalizer_panel_ = new EqualizerPanel{equalizer_, this};

    equalizer_panel_->show();
    equalizer_panel_->raise();
}

void MainWindow::set_volume(int percent)
{
    volume_.set_position(percent);

    show_volume();
}

void MainWindow::show_volume()
{
    volume_value_->setText(QString::number(volume_.position()) + "%");
}

void MainWindow::set_boosted(bool boosted)
{
    volume_.set_boosted(boosted);

    // The slider is asked to hold what the control now allows, and says what it settled on.
    volume_slider_->setRange(0, volume_.max_position());
    volume_slider_->setValue(volume_.position());

    show_volume();
}

void MainWindow::seek_to(double fraction)
{
    presenter_.seek_to(fraction);
}

void MainWindow::show_status(const QString& message)
{
    status_label_->setText(message);
}

void MainWindow::refresh()
{
    const engine::PlaybackSnapshot playback{presenter_.presentation().playback};

    actions_.set_available(PlayerAction::play, playback.loaded);
    actions_.set_available(PlayerAction::stop, playback.loaded);
    actions_.set_available(PlayerAction::previous, playback.loaded);
    actions_.set_available(PlayerAction::next, playback.loaded);

    play_button_->setEnabled(actions_.action(PlayerAction::play)->isEnabled());
    stop_button_->setEnabled(actions_.action(PlayerAction::stop)->isEnabled());
    previous_button_->setEnabled(actions_.action(PlayerAction::previous)->isEnabled());
    next_button_->setEnabled(actions_.action(PlayerAction::next)->isEnabled());
    position_bar_->setEnabled(playback.loaded && playback.seekable &&
        playback.total_time.has_value());

    now_playing_->show_playlist(presenter_.playlist());

    if (!playback.loaded) {
        show_play_button(false);
        track_label_->setText("no track");
        elapsed_label_->clear();
        total_label_->clear();

        return;
    }

    const std::optional<units::Time> total{playback.total_time};

    // While the edge is being dragged it is a place being chosen, and that is the time worth
    // showing: a listener aiming for somewhere needs to see where they are aiming.
    const units::Time shown{position_bar_->dragging() && total ? *total * position_bar_->fraction()
                                                               : playback.time_played};

    show_play_button(playback.playing);

    elapsed_label_->setText(as_clock(shown));
    total_label_->setText(total ? as_clock(*total) : QString{"--:--"});

    if (!total || *total == units::Time{})
        return;

    // A drag in progress is left alone by the bar itself.
    position_bar_->set_fraction(shown.get<units::Sec>() / total->get<units::Sec>());
}

} // namespace wiola::gui
