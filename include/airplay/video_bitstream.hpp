#pragma once
#include "airplay_crypto.h"
namespace airplay {
// AirPlay screen access units have explicit NAL lengths. Do not sniff start
// codes: a length of 256..511 begins with the same bytes as an Annex-B prefix.
fxchain::airplay::Bytes lengthPrefixedToAnnexB(const fxchain::airplay::Bytes&, unsigned length_bytes = 4);
fxchain::airplay::Bytes annexBToLengthPrefixed(const fxchain::airplay::Bytes&);
}
