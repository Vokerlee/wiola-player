/**
 * @file
 * @brief Registered widget factories and recursive layout construction.
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

#pragma once
#include "layout_document.hpp"
#include "prepared_layout.hpp"
#include <persistence/qt/atomic_file_store.hpp>

namespace wiola::gui {

enum class LayoutChoice {
    classic,
    library_focus,
    queue_focus,
    custom,
};
[[nodiscard]] QString layout_choice_name(LayoutChoice choice);
[[nodiscard]] std::optional<LayoutChoice> parse_layout_choice(const QString& key);
enum class LayoutSessionError {
    none,
    invalid,
    unsupported_version,
    read_failed,
    write_failed,
    unavailable_views,
    rollback_failed,
};

struct LayoutAcceptance {
    QWidget* root{};
    LayoutSessionError error{LayoutSessionError::none};
};

class LayoutSession final {
public:
    LayoutSession(std::filesystem::path file, LayoutDocument fallback, LayoutChoice choice,
        persistence::qt::AtomicFileStore::OutputFactory output = {});

    [[nodiscard]] const LayoutDocument& document() const { return accepted_; }

    [[nodiscard]] LayoutChoice choice() const { return choice_; }

    [[nodiscard]] LayoutSessionError restore_error() const { return restore_error_; }

    [[nodiscard]] bool recovered_backup() const { return recovered_; }

    /// Retain a usable in-memory fallback when saved view references are no longer available.
    void use_fallback(LayoutDocument document, LayoutChoice choice);
    [[nodiscard]] LayoutAcceptance accept(LayoutDocument candidate, LayoutChoice choice,
        std::unique_ptr<PreparedLayout> prepared, QWidget* parent);

private:
    LayoutDocument accepted_;
    LayoutChoice choice_;
    persistence::qt::AtomicFileStore store_;
    QByteArray last_good_;
    LayoutSessionError restore_error_{LayoutSessionError::none};
    bool recovered_{}, read_only_{};
};

} // namespace wiola::gui
