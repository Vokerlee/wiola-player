#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <chrono>
#include <gui/tracks/track_browser.hpp>
#include <iostream>
#include <random>
#include <stdexcept>

using Clock = std::chrono::steady_clock;

static double milliseconds(Clock::time_point before)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - before).count();
}

static void settle(wiola::gui::TrackTableView& view)
{
    if (!view.projection_pending())
        return;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&view, &wiola::gui::TrackTableView::view_state_changed, &loop, [&] {
        if (!view.projection_pending())
            loop.quit();
    });
    deadline.start(30000);
    loop.exec();
    if (view.projection_pending())
        throw std::runtime_error{"projection deadline exceeded"};
}

int main(int argc, char** argv)
{
#ifndef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "minimal");
#endif
    QApplication app{argc, argv};
    for (const int size : {1000, 10000, 100000}) {
        std::mt19937 random{731};
        std::vector<wiola::gui::TrackRow> rows;
        rows.reserve(size);
        for (int i = 0; i < size; ++i) {
            wiola::gui::TrackRow row;
            row.id = wiola::engine::QueueEntryId{static_cast<std::uint64_t>(i + 1)};
            row.media = {std::to_string(i)};
            row.location_key = "/music/" + std::to_string(i) + ".flac";
            row.metadata.title = "Track " + std::to_string(random() % size);
            row.metadata.artist = "Artist " + std::to_string(random() % 100);
            rows.push_back(std::move(row));
        }
        for (int trial = 0; trial < 3; ++trial) {
            wiola::gui::TrackTableView view;
            auto* model = static_cast<wiola::gui::TrackTableModel*>(view.model());
            auto before = Clock::now();
            view.show_rows(rows);
            const auto initial = milliseconds(before);
            wiola::gui::TrackViewState state;
            state.query.title = "Track";
            state.sort_keys = {{.field = wiola::gui::TrackSortField::artist},
                {.field = wiola::gui::TrackSortField::title}};
            before = Clock::now();
            view.set_view_state(state);
            const auto first_submit = milliseconds(before);
            settle(view);
            const auto first_complete = milliseconds(before);
            state.query.title = "Track 1";
            before = Clock::now();
            view.set_view_state(state);
            const auto cached_submit = milliseconds(before);
            settle(view);
            const auto cached_complete = milliseconds(before);
            auto changed = rows;
            for (std::size_t i = 0; i < 64 && i < changed.size(); ++i) {
                changed[i].metadata.title += " revised";
                ++changed[i].metadata_revision;
            }
            std::vector<wiola::gui::TrackRow> patch(changed.begin(),
                changed.begin() + std::min<std::size_t>(64, changed.size()));
            before = Clock::now();
            view.update_rows(patch);
            const auto batch_submit = milliseconds(before);
            settle(view);
            const auto batch_complete = milliseconds(before);
            const auto statistics = model->projection_statistics();
            std::cout << "{\"rows\":" << size << ",\"trial\":" << trial
                      << ",\"initial_ms\":" << initial << ",\"first_submit_ms\":" << first_submit
                      << ",\"first_complete_ms\":" << first_complete
                      << ",\"cached_submit_ms\":" << cached_submit
                      << ",\"cached_complete_ms\":" << cached_complete
                      << ",\"batch_submit_ms\":" << batch_submit
                      << ",\"batch_complete_ms\":" << batch_complete
                      << ",\"source_rebuilds\":" << statistics.source_rebuilds
                      << ",\"snapshots\":" << statistics.snapshots << "}" << std::endl;
        }
    }
}
