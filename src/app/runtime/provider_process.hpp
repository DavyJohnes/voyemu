#pragma once
#include "provider_registry.hpp"
#include "source_registry.hpp"
namespace voyemu::runtime {
// Trusted workers must not escape the owned process group. This is not an OS sandbox.
ProcessResult run_provider(const ProcessSpec &specification, int timeout_ms);
SourceSample provider_sample(const Json &observation);
} // namespace voyemu::runtime
