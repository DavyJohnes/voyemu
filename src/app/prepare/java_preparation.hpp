#pragma once
#include "model.hpp"
#include <map>
#include <optional>
namespace voyemu {
enum class JavaPreparationOperation {
    plan,
    build_artifacts
};
struct JavaPreparationRuntime {
    std::filesystem::path jdk, helper, receipt;
    std::string java_sha256, helper_sha256, receipt_sha256;
    // Expected key selected by the installer, independently of the ownership receipt.
    std::string helper_cache_key;
};
struct JavaPreparationAndroidInput {
    std::filesystem::path receipt;
    std::string receipt_sha256;
};
// The handoff of an installed native component: its receipt beside the output.
struct JavaPreparationNativeComponent {
    std::filesystem::path receipt;
    std::string receipt_sha256;
};
struct JavaPreparationBuildInputs {
    std::filesystem::path patch_data, sdk, key, certificate, work;
    // A11-only C++ file producer handoff; Java may consume, never mutate these outputs.
    std::filesystem::path native_files_receipt;
    std::string native_files_receipt_sha256;
    std::map<std::string, JavaPreparationNativeComponent> native_components;
    // Optional public Android platform JAR overlay; installed native SDK stays immutable.
    std::filesystem::path platform;
    std::string platform_sha256;
    // Optional CI-built guest Java (android-java.zip); without it the helper compiles on its JDK.
    std::filesystem::path guest_java;
    std::string guest_java_sha256;
    std::string patch_data_sha256, tool_manifest_sha256, key_sha256, certificate_sha256;
    std::map<std::string, std::string> tool_files;
    std::map<std::string, JavaPreparationAndroidInput> android_inputs;
};
struct JavaPreparationRequest {
    std::string profile, profile_sha256;
    JavaPreparationOperation operation = JavaPreparationOperation::plan;
    std::optional<JavaPreparationBuildInputs> build;
    std::string target;
    // In-memory invocation guard; never serialized into a request or result receipt.
    std::string source_snapshot;
};
// Finite artifact-graph budget, one for every profile; plans retain their independent shorter
// limit.
int java_preparation_timeout_ms(const JavaPreparationRequest &);
// Revalidates the immutable helper cut, receipt and every dependency before invocation.
std::string java_preparation_classpath(const std::filesystem::path &root,
                                       const JavaPreparationRuntime &);
// Validates typed request inputs and produces PreparationMain schema1; no process or guest.
Json java_preparation_request(const std::filesystem::path &root, const JavaPreparationRequest &);
// Validates the native file draft of a build request against the file nodes that profile
// preparation plans; returns its receipt binding, or null for a profile without one.
Json java_preparation_native_files(const std::filesystem::path &root,
                                   const JavaPreparationRequest &);
// Validates the helper response against the pinned profile without invoking Java.
void validate_java_preparation_response(const std::filesystem::path &root,
                                        const JavaPreparationRequest &, const Json &);
// Existing D8/dexlib2 Java helper only. Java runtime or JDK25+, verified classpath,
// project-private request/log/result, bounded process. Never downloads or launches guests.
Json prepare_java_artifacts(const std::filesystem::path &root, const JavaPreparationRuntime &,
                            const JavaPreparationRequest &,
                            const std::filesystem::path &destination, int timeout_ms = 300000);
} // namespace voyemu
