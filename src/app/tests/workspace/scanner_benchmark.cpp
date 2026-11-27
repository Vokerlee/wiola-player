/**
 * @file
 * @brief Native measurements of recursive folder enumeration.
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

#include <app/workspace/workspace_scanner.hpp>
#include <fixtures/temporary_directory.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

int main()
{
    for (const std::size_t count : {1000, 10000}) {
        wiola::testing::TemporaryDirectory directory;
        for (std::size_t index = 0; index < count; ++index) {
            const auto folder = directory.path() / std::to_string(index % 16);
            std::filesystem::create_directories(folder);
            if (!(std::ofstream{folder / (std::to_string(index) + ".wav")} << "benchmark"))
                throw std::runtime_error{"could not create scan fixture"};
        }

        for (int trial = 0; trial < 3; ++trial) {
            std::mutex mutex;
            std::condition_variable changed;
            std::size_t entries = 0;
            bool done = false;
            auto status = wiola::app::WorkspaceScanStatus::running;
            wiola::app::WorkspaceScanner scanner;
            const auto before = std::chrono::steady_clock::now();
            scanner.start({.id = {"benchmark"}, .root = directory.path(), .recursive = true},
                {".wav"}, [&](wiola::app::WorkspaceScanBatch batch) {
                    std::lock_guard lock{mutex};
                    entries += batch.entries.size();
                    status = batch.status;
                    done = status != wiola::app::WorkspaceScanStatus::running;
                    changed.notify_all();
                });

            {
                std::unique_lock lock{mutex};
                if (!changed.wait_for(lock, std::chrono::seconds{30}, [&] { return done; }))
                    throw std::runtime_error{"scan did not complete"};
            }
            scanner.stop();
            const auto elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before)
                    .count();
            if (status != wiola::app::WorkspaceScanStatus::complete || entries != count)
                throw std::runtime_error{"incomplete benchmark scan"};

            std::cout << "{\"benchmark\":\"workspace_scan\",\"tracks\":" << count
                      << ",\"trial\":" << trial << ",\"elapsed_ms\":" << elapsed << "}\n";
        }
    }
}
