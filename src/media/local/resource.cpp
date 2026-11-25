/**
 * @file
 * @brief Local file resource composition and native path conversion.
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

#include "resource.hpp"

#include <string>

namespace wiola::media {
namespace {

constexpr const char* source_namespace = "local-file";

std::string text(const std::u8string& value)
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace

ResourceReference local_resource(const std::filesystem::path& path)
{
    return {source_namespace, text(path.generic_u8string()), text(path.stem().u8string())};
}

std::vector<ResourceReference> local_resources(std::span<const std::filesystem::path> paths)
{
    std::vector<ResourceReference> resources;
    resources.reserve(paths.size());
    for (const auto& path : paths)
        resources.push_back(local_resource(path));
    return resources;
}

Result<std::filesystem::path> local_path(const ResourceReference& resource)
{
    if (resource.source() != source_namespace)
        return unexpected{
            Error{.category = ErrorCategory::unsupported,
                  .detail = "resource does not belong to the local file provider"}
        };
    if (resource.key().find('\0') != std::string::npos)
        return unexpected{
            Error{.category = ErrorCategory::unreadable,
                  .detail = "local file resource contains a null path character"}
        };

    std::u8string encoded;
    encoded.reserve(resource.key().size());
    for (const char value : resource.key())
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
    try {
        return std::filesystem::path{encoded};
    } catch (const std::filesystem::filesystem_error& failure) {
        return unexpected{
            Error{.category = ErrorCategory::unreadable, .detail = failure.what()}
        };
    }
}

} // namespace wiola::media
