#pragma once
#include <string>
namespace platform {
// CARPLAY_PAIRING_FILE overrides OS defaults. Empty means memory-only when
// no usable home/state directory is available.
std::string pairingStatePath();
}
