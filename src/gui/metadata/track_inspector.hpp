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

#pragma once

#include "custom_tag_editor.hpp"
#include "inspector_context.hpp"
#include "metadata_conflict_panel.hpp"
#include "standard_metadata_editor.hpp"
#include <gui/appearance/artwork_cache.hpp>

#include <QLabel>
#include <QWidget>

namespace wiola::gui {

class TrackInspector final : public QWidget {
public:
    explicit TrackInspector(QWidget* parent = nullptr);

    void show_context(InspectorContext context);

    [[nodiscard]] const InspectorContext& context() const { return context_; }

    [[nodiscard]] CustomTagEditor* custom_tags() const noexcept { return custom_tags_; }

    [[nodiscard]] StandardMetadataEditor* standard_metadata() const noexcept
    {
        return standard_metadata_;
    }

    [[nodiscard]] MetadataConflictPanel* conflicts() const noexcept { return conflicts_; }

private:
    InspectorContext context_;
    std::optional<InspectorContext> pending_context_;
    QLabel* context_label_{nullptr};
    ArtworkCache artwork_;
    QLabel* artwork_label_{nullptr};
    QLabel* title_{nullptr};
    QLabel* artist_{nullptr};
    QLabel* album_{nullptr};
    QLabel* location_{nullptr};
    QLabel* duration_{nullptr};
    QLabel* availability_{nullptr};
    CustomTagEditor* custom_tags_{nullptr};
    StandardMetadataEditor* standard_metadata_{nullptr};
    MetadataConflictPanel* conflicts_{nullptr};
};

} // namespace wiola::gui
