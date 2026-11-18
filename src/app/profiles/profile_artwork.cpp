#include "profile_artwork.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "profile_originals.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <set>
namespace voyemu {
namespace {
constexpr Check need{"PROFILE_ARTWORK"};

void ordinary(const std::filesystem::path &path) {
    need(!os::linked_ancestor(path), "linked path rejected");
}
} // namespace

Json export_profile_artwork(const std::filesystem::path &root, std::string_view profile,
                            const std::filesystem::path &destination) {
    need(root.is_absolute() && std::filesystem::canonical(root) == root &&
                 valid_profile_name(profile),
         "canonical root and profile required");
    ordinary(destination);
    need(destination.is_absolute() && destination.lexically_normal() == destination &&
                 destination.lexically_relative(root).generic_string().starts_with("machines/") &&
                 !std::filesystem::exists(destination),
         "fresh machine artwork directory required");
    const auto definition =
            read_private_json(root / "profiles" / std::string(profile) / "profile.json", 1 << 20);
    const std::filesystem::path assets = definition.at("wheel").at("assets").get<std::string>();
    need(!assets.is_absolute() && assets.generic_string().starts_with("profiles/common/wheels/") &&
                 assets.generic_string().find(".. ") == std::string::npos,
         "common wheel resources required");

    for (const auto &part : assets) {
        need(part != ".." && part != ".", "confined wheel path required");
    }

    const auto descriptor = root / assets / "control-artwork.json";
    ordinary(descriptor);
    const auto spec = read_private_json(descriptor, 128 * 1024);
    need(spec.at("schema") == 1 && spec.at("assets").size() <= 16,
         "bounded artwork manifest required");

    if (spec.contains("ownerProfiles")) {
        const auto &owners = spec.at("ownerProfiles");
        need(owners.is_array() && !owners.empty() && owners.size() <= 16,
             "bounded owner profiles required");
        std::set<std::string> unique;
        bool selected = false;

        for (const auto &owner : owners) {
            need(owner.is_string(), "owner profile name required");
            const auto name = owner.get<std::string>();
            need(valid_profile_name(name) && unique.insert(name).second,
                 "unique safe owner profile required");
            selected |= name == runtime_profile_reference(definition, profile);
        }

        if (!selected) {
            return Json::object();
        }
    }

    Json files = Json::object();
    std::map<std::string, std::string> payloads;

    for (const auto &item : spec.at("assets")) {
        const auto name = item.at("file").get<std::string>();
        need(std::filesystem::path(name).filename() == name && name.ends_with(".png") &&
                     !payloads.contains(name),
             "unique PNG basename required");
        const auto &input = spec.at("inputs").at(item.at("input").get<std::string>());
        const std::filesystem::path relative = input.at("path").get<std::string>();
        need(!relative.is_absolute() && relative.generic_string().starts_with("originals/"),
             "owner APK path required");

        for (const auto &part : relative) {
            need(part != ".." && part != ".", "confined owner path required");
        }

        const auto apk = profile_originals_directory(root, profile) /
                         relative.lexically_relative("originals");
        ordinary(apk);
        payloads[name] = read_pinned_zip_entry(apk, input.at("sha256").get<std::string>(),
                                               item.at("member").get<std::string>(),
                                               item.at("png_sha256").get<std::string>(), 16 << 20);

        if (item.contains("dimensions")) {
            const auto &dimensions = item.at("dimensions");
            const auto &png = payloads.at(name);
            need(dimensions.is_array() && dimensions.size() == 2 && png.size() >= 24 &&
                         png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0 &&
                         png.compare(12, 4, "IHDR") == 0,
                 "declared PNG dimensions require an IHDR header");
            const auto size = [&](std::size_t offset) {
                std::uint32_t value = 0;

                for (unsigned i = 0; i < 4; ++i) {
                    value = (value << 8) | static_cast<unsigned char>(png[offset + i]);
                }

                return value;
            };
            const auto width = dimensions[0].get<std::uint32_t>(),
                       height = dimensions[1].get<std::uint32_t>();
            need(width > 0 && width <= 8192 && height > 0 && height <= 8192 && size(16) == width &&
                         size(20) == height,
                 "declared PNG dimensions differ");

            if (item.contains("crop_pixels")) {
                const auto &crop = item.at("crop_pixels");
                need(crop.is_array() && crop.size() == 4, "PNG crop rectangle required");
                const auto x = crop[0].get<std::uint64_t>(), y = crop[1].get<std::uint64_t>(),
                           w = crop[2].get<std::uint64_t>(), h = crop[3].get<std::uint64_t>();
                need(x < width && y < height && w > 0 && h > 0 && w <= width - x && h <= height - y,
                     "PNG crop leaves original dimensions");
            }
        }
    }

    std::ifstream input(descriptor, std::ios::binary);
    payloads["source-provenance.json"] = std::string(std::istreambuf_iterator<char>(input), {});
    need(bool(input) || input.eof(), "cannot read artwork provenance");
    ensure_private_directory(destination);
    try {
        for (const auto &[name, bytes] : payloads) {
            const auto file = destination / name;
            std::ofstream out(file, std::ios::binary);
            out.write(bytes.data(), bytes.size());
            out.close();
            need(bool(out) && sha256_file(file) == sha256_bytes(bytes),
                 "artwork publication differs");
            files[name] = {{"file", "ui/controls/" + name},
                           {"bytes", bytes.size()},
                           {"sha256", sha256_bytes(bytes)}};
        }
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(destination, error);
        throw;
    }

    return files;
}
} // namespace voyemu
