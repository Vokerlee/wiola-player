/**
 * @file
 * @brief Shared application and file conversion for typed metadata patches.
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

#include "metadata_patch.hpp"
#include <tuple>
#include <type_traits>

namespace wiola::library {
namespace {

// One typed field mapping for patch application and the explicit file-write conversion.
constexpr auto fields = std::tuple{
    std::tuple{&StandardMetadataPatch::title,        &StandardFieldOverrides::title,
               &media::MetadataOverrides::title       },
    std::tuple{&StandardMetadataPatch::artist,       &StandardFieldOverrides::artist,
               &media::MetadataOverrides::artist      },
    std::tuple{&StandardMetadataPatch::album,        &StandardFieldOverrides::album,
               &media::MetadataOverrides::album       },
    std::tuple{&StandardMetadataPatch::genre,        &StandardFieldOverrides::genre,
               &media::MetadataOverrides::genre       },
    std::tuple{&StandardMetadataPatch::comment,      &StandardFieldOverrides::comment,
               &media::MetadataOverrides::comment     },
    std::tuple{&StandardMetadataPatch::composer,     &StandardFieldOverrides::composer,
               &media::MetadataOverrides::composer    },
    std::tuple{&StandardMetadataPatch::year,         &StandardFieldOverrides::year,
               &media::MetadataOverrides::year        },
    std::tuple{&StandardMetadataPatch::track_number, &StandardFieldOverrides::track_number,
               &media::MetadataOverrides::track_number},
    std::tuple{&StandardMetadataPatch::disc_number,  &StandardFieldOverrides::disc_number,
               &media::MetadataOverrides::disc_number }
};

template<typename Visitor>
void visit(const StandardMetadataPatch& patch, Visitor&& visitor)
{
    std::apply(
        [&](const auto&... field) {
            (visitor(patch.*std::get<0>(field), std::get<1>(field), std::get<2>(field)), ...);
        },
        fields
    );
}

} // namespace

bool metadata_patch_empty(const StandardMetadataPatch& patch)
{
    bool empty = true;
    visit(patch, [&](const auto& edit, auto, auto) {
        empty = empty && edit.operation() == FieldEditOperation::unchanged;
    });
    return empty;
}

bool valid_metadata_patch(const StandardMetadataPatch& patch)
{
    bool valid = true;
    visit(patch, [&](const auto& edit, auto, auto) {
        if constexpr (std::is_same_v<typename std::decay_t<decltype(edit)>::Value, std::string>) {
            if (const auto* value = edit.value())
                valid = valid && value->size() <= max_metadata_text_bytes &&
                    value->find('\0') == std::string::npos;
        }
    });
    return valid;
}

MetadataPatchResult apply_metadata_patch(StandardFieldOverrides& overrides,
    const StandardMetadataPatch& patch)
{
    if (!valid_metadata_patch(patch))
        return MetadataPatchResult::invalid;
    auto updated = overrides;
    visit(patch, [&](const auto& edit, auto member, auto) {
        auto& target = updated.*member;
        using Stored = typename std::decay_t<decltype(target)>::value_type;
        switch (edit.operation()) {
        case FieldEditOperation::unchanged:
            break;
        case FieldEditOperation::set:
            target = *edit.value();
            break;
        case FieldEditOperation::clear:
            target = Stored{};
            break;
        case FieldEditOperation::inherit:
            target.reset();
            break;
        }
    });
    if (updated == overrides)
        return MetadataPatchResult::unchanged;
    overrides = std::move(updated);
    return MetadataPatchResult::changed;
}

media::Result<media::MetadataOverrides> file_metadata_patch(const StandardMetadataPatch& patch)
{
    if (!valid_metadata_patch(patch))
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::unsupported,
                         .detail = "invalid metadata patch text"}
        };
    media::MetadataOverrides result;
    bool unsupported_clear = false;
    visit(patch, [&](const auto& edit, auto, auto member) {
        if (edit.operation() == FieldEditOperation::set)
            result.*member = *edit.value();
        else if (edit.operation() == FieldEditOperation::clear) {
            if constexpr (std::is_same_v<typename std::decay_t<decltype(edit)>::Value, std::string>)
                result.*member = std::string{};
            else
                unsupported_clear = true;
        }
    });
    if (unsupported_clear)
        return media::unexpected{
            media::Error{.category = media::ErrorCategory::unsupported,
                         .detail = "the file-write contract cannot represent clearing a numeric tag"}
        };
    return result;
}

} // namespace wiola::library
