// AirPlay HomeKit pairing state for pair-setup and pair-verify. The fixed
// setup code follows local DiPlay source; live iPhone compatibility remains
// UNVERIFIED until exercised against a real device.
#pragma once

#include "airplay_crypto.h"
#include <map>
#include <string>

namespace airplay {

class PairingProtocol {
public:
    // Loads or creates the stable AirPlay identity and controller pairing
    // keys. The default path is under ~/Library/Application Support on macOS;
    // CARPLAY_PAIRING_FILE can select another local state file.
    explicit PairingProtocol(std::string persistence_path = {});

    fxchain::airplay::Bytes handlePairSetup(const fxchain::airplay::Bytes& body);
    fxchain::airplay::Bytes handlePairVerify(const fxchain::airplay::Bytes& body);

    bool pairSetupComplete() const { return pair_setup_complete_; }
    bool pairVerified() const { return pair_verified_; }
    const fxchain::airplay::Bytes& controlReadKey() const { return control_read_key_; }
    const fxchain::airplay::Bytes& controlWriteKey() const { return control_write_key_; }
    const fxchain::airplay::Bytes& sharedSecret() const { return verify_shared_; }
    std::string publicKeyHex() const;
    std::string deviceIdentifier() const;

private:
    fxchain::airplay::Bytes errorResponse(std::uint8_t state) const;
    bool loadPersistentState();
    bool savePersistentState() const;
    std::string persistence_path_;
    std::string accessory_id_;
    fxchain::airplay::Bytes identity_seed_;
    fxchain::airplay::Bytes identity_public_;
    fxchain::airplay::SrpServer srp_;
    fxchain::airplay::Bytes srp_session_key_;
    std::map<std::string, fxchain::airplay::Bytes> controller_keys_;
    fxchain::airplay::X25519KeyPair verify_ephemeral_;
    fxchain::airplay::Bytes controller_ephemeral_;
    fxchain::airplay::Bytes verify_shared_;
    fxchain::airplay::Bytes verify_encryption_key_;
    fxchain::airplay::Bytes control_read_key_;
    fxchain::airplay::Bytes control_write_key_;
    bool srp_started_ = false;
    bool pair_setup_complete_ = false;
    bool pair_verified_ = false;
};

} // namespace airplay
