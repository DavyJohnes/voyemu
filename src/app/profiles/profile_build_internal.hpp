#pragma once
#include "android_engine.hpp"
#include "android_owned_package.hpp"
#include "api30_framework_boot.hpp"
#include "apk_base.hpp"
#include "base_image.hpp"
#include "can_model_candidate_binding.hpp"
#include "check.hpp"
#include "deps.hpp"
#include "download.hpp"
#include "error.hpp"
#include "host_payload.hpp"
#include "java_toolchain.hpp"
#include "logging.hpp"
#include "machine_preparation.hpp"
#include "original_navi_candidate.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "passion_generated_helper.hpp"
#include "passion_mcu_prepared.hpp"
#include "passion_power_prepared.hpp"
#include "passion_presentation.hpp"
#include "portable_resources.hpp"
#include "portable_runtime.hpp"
#include "process.hpp"
#include "profile_artwork.hpp"
#include "profile_build.hpp"
#include "profile_identity.hpp"
#include "profile_preparation.hpp"
#include "profile_runtime.hpp"
#include "progress.hpp"
#include "qinggan09_owned_packages.hpp"
#include "qnx_acceptance.hpp"
#include "qnx_host.hpp"
#include "qnx_prepare.hpp"
#include "qnx_runtime_layout.hpp"
#include "quickboot.hpp"
#include "vehicle_capture.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include <algorithm>
#include <array>
#include <cstdlib>
#ifdef __APPLE__
#include <stdio.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Helpers shared by the parts of the profile build; each part includes this header.
namespace voyemu::profile_build_detail {
inline constexpr Check need{"PROFILE_BUILD"};
std::function<void()> capture_authoring_inputs(const std::filesystem::path &root,
                                               std::string_view profile);
void inside(const std::filesystem::path &root, const std::filesystem::path &path);
void ordinary(const std::filesystem::path &p);
std::string hash(const std::filesystem::path &file, std::uint64_t bound = 8ULL << 30);
// The authoring profile.json, which build reads and pins directly from profiles/.
std::filesystem::path authoring_definition(const std::filesystem::path &root,
                                           std::string_view profile);
void root_profile(const std::filesystem::path &root, std::string_view profile);
// Validate the cold candidate's captured output profile, independently of
// authoring byte layout.
void validate_accepted_android_profile(const std::filesystem::path &android, const Json &candidate,
                                       std::string_view profile, const Json &definition);
std::string exe();
std::string backend_host();
std::filesystem::path build_target(const std::filesystem::path &root, std::string_view profile,
                                   const MachineIdentity &machine);
std::map<std::string, std::string> host_pins(const std::filesystem::path &sdk);
void publish(const std::filesystem::path &from, const std::filesystem::path &to);
// Copies the pinned guest images of group into stage; meter counts their bytes.
Json copy_images(const std::filesystem::path &root, const std::filesystem::path &source,
                 const Json &images, const std::filesystem::path &stage, const std::string &group,
                 const std::set<std::string> &roles, std::uint64_t reserve,
                 progress::Meter *meter = nullptr);
// The bytes of the pinned guest images that copy_images copies.
std::uint64_t image_bytes(const Json &images);
Json export_portable_boot_files(const std::filesystem::path &root, const Json &definition,
                                const std::filesystem::path &stage, std::uint64_t reserve);
std::unique_ptr<os::FileLock> artifact_reader(const std::filesystem::path &root,
                                              const std::filesystem::path &receipt,
                                              const std::string &pin);
std::unique_ptr<os::FileLock> candidate_artifact_reader(const std::filesystem::path &root,
                                                        const std::filesystem::path &candidate);
Json validate_accepted_qnx_closure(const std::filesystem::path &root, std::string_view profile,
                                   const std::filesystem::path &file, const std::string &expected,
                                   const std::filesystem::path &payload_source, bool persistent);
bool qinggan11_camera_delivery_required(const Json &definition, std::string_view profile);
bool qinggan11_navi_delivery_required(const Json &definition, std::string_view profile);
Json upgrade_pinned_json(const std::filesystem::path &root, const Json &descriptor,
                         std::uint64_t bound);
// QGMedia is installed in the immutable system image. This validates its source
// provenance only; it never exports or installs a second APK over user data.
Json source_system_media_artifact(const std::filesystem::path &root, std::string_view profile,
                                  const std::filesystem::path &android,
                                  const std::string &receipt_pin,
                                  const std::filesystem::path &artifact_receipt = {},
                                  const std::string &artifact_receipt_pin = {});
Json source_camera_artifact(const std::filesystem::path &root, std::string_view profile,
                            const std::filesystem::path &android, const std::string &receipt_pin,
                            const std::filesystem::path &artifact_receipt,
                            const std::string &artifact_receipt_pin);
Json source_navi_artifact(const std::filesystem::path &root, std::string_view profile,
                          const std::filesystem::path &android, const std::string &receipt_pin,
                          const std::filesystem::path &artifact_receipt,
                          const std::string &artifact_receipt_pin);
} // namespace voyemu::profile_build_detail
