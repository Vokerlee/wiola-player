/**
 * @file
 * @brief Typed standard metadata requests and explicit field operations.
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
#include <cstddef>
#include <library/records.hpp>
#include <utility>

namespace wiola::library {

/// Per standard text field, measured in encoded bytes; empty text is a valid explicit value.
inline constexpr std::size_t max_metadata_text_bytes{1024};

enum class FieldEditOperation {
    unchanged,
    set,
    clear,
    inherit,
};

/// A request, distinct from a stored override. Inherit removes only the application override.
template<typename T>
class FieldEdit final {
public:
    using Value = T;
    FieldEdit() = default;

    FieldEdit(T value)
        : operation_{FieldEditOperation::set}
        , value_{std::move(value)}
    {
    }

    FieldEdit& operator=(T value)
    {
        *this = FieldEdit{std::move(value)};
        return *this;
    }

    [[nodiscard]] static FieldEdit clear() { return FieldEdit{FieldEditOperation::clear}; }

    [[nodiscard]] static FieldEdit inherit() { return FieldEdit{FieldEditOperation::inherit}; }

    [[nodiscard]] FieldEditOperation operation() const noexcept { return operation_; }

    [[nodiscard]] const T* value() const noexcept
    {
        return operation_ == FieldEditOperation::set ? &value_ : nullptr;
    }

    bool operator==(const FieldEdit&) const = default;

private:
    explicit FieldEdit(FieldEditOperation operation)
        : operation_{operation}
    {
    }

    FieldEditOperation operation_{FieldEditOperation::unchanged};
    T value_{};
};

struct StandardMetadataPatch {
    FieldEdit<std::string> title;
    FieldEdit<std::string> artist;
    FieldEdit<std::string> album;
    FieldEdit<std::string> genre;
    FieldEdit<std::string> comment;
    FieldEdit<std::string> composer;
    FieldEdit<std::uint32_t> year;
    FieldEdit<std::uint32_t> track_number;
    FieldEdit<std::uint32_t> disc_number;
    bool operator==(const StandardMetadataPatch&) const = default;
};
enum class MetadataPatchResult {
    unchanged,
    changed,
    invalid,
};
[[nodiscard]] bool metadata_patch_empty(const StandardMetadataPatch& patch);
[[nodiscard]] bool valid_metadata_patch(const StandardMetadataPatch& patch);
[[nodiscard]] MetadataPatchResult apply_metadata_patch(StandardFieldOverrides& overrides,
    const StandardMetadataPatch& patch);
/// Inherit never deletes an embedded tag. Unsupported numeric deletion is explicitly rejected.
[[nodiscard]] media::Result<media::MetadataOverrides>
file_metadata_patch(const StandardMetadataPatch& patch);

} // namespace wiola::library
