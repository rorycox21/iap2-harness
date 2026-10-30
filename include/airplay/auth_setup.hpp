#pragma once
#include "mfi/signer.hpp"
#include <optional>
#include <vector>
#include <cstdint>
namespace airplay {
// MFi v3 SAP exchange after encrypted RTSP pairing; returns no response on failure.
std::optional<std::vector<std::uint8_t>> mfiAuthSetup(
    const std::vector<std::uint8_t>& request, mfi::Signer& signer);
}
