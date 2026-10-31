#include "platform/state_paths.hpp"
#include <cstdlib>
#include <filesystem>
namespace platform {
std::string pairingStatePath() {
    if (const char* path = std::getenv("CARPLAY_PAIRING_FILE"); path && *path) return path;
    const char* home = std::getenv("HOME");
#if defined(__APPLE__)
    if (!home || !*home) return {};
    return (std::filesystem::path(home) / "Library" / "Application Support" / "iap2-harness" / "airplay-pairings.bin").string();
#else
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state && std::filesystem::path(state).is_absolute())
        return (std::filesystem::path(state) / "iap2-harness" / "airplay-pairings.bin").string();
    if (!home || !*home) return {};
    return (std::filesystem::path(home) / ".local" / "state" / "iap2-harness" / "airplay-pairings.bin").string();
#endif
}
}
