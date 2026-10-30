// AirPlay pair-setup and pair-verify receiver state machine. Protocol fields
// and labels follow the local DiPlay implementation and remain UNVERIFIED
// against a live iPhone capture.
#include "airplay/pairing.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace airplay {
namespace ac = fxchain::airplay;

namespace {
using Items = ac::tlv::Map;
constexpr std::uint8_t kState = ac::tlv::State;
constexpr std::uint8_t kPublicKey = ac::tlv::PublicKey;
constexpr std::uint8_t kSalt = ac::tlv::Salt;
constexpr std::uint8_t kProof = ac::tlv::Proof;
constexpr std::uint8_t kEncrypted = ac::tlv::EncryptedData;
constexpr std::uint8_t kIdentifier = ac::tlv::Identifier;
constexpr std::uint8_t kSignature = ac::tlv::Signature;
constexpr std::array<std::uint8_t, 8> kPersistenceMagic{
    'I', 'A', 'P', '2', 'P', 'A', 'I', 'R'};
constexpr std::uint16_t kPersistenceVersion = 1;

ac::Bytes byte(std::uint8_t value) { return {value}; }

bool stateOf(const Items& items, std::uint8_t& state) {
    const auto value = ac::tlv::get(items, kState);
    if (!value || value->size() != 1) return false;
    state = (*value)[0];
    return true;
}

ac::Bytes nonceLabel(const char* label) {
    ac::Bytes nonce(8, 0);
    for (std::size_t i = 0; i < nonce.size() && label[i] != '\0'; ++i)
        nonce[i] = static_cast<std::uint8_t>(label[i]);
    return nonce;
}

ac::Bytes concat(const ac::Bytes& a, const ac::Bytes& b, const ac::Bytes& c) {
    ac::Bytes out;
    out.reserve(a.size() + b.size() + c.size());
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    out.insert(out.end(), c.begin(), c.end());
    return out;
}

std::string uuidFromRandom() {
    ac::Bytes id = ac::randomBytes(16);
    id[6] = static_cast<std::uint8_t>((id[6] & 0x0f) | 0x40);
    id[8] = static_cast<std::uint8_t>((id[8] & 0x3f) | 0x80);
    char text[37];
    std::snprintf(text, sizeof text,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
                  id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
    return text;
}

void append16(ac::Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append32(ac::Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

bool take16(const ac::Bytes& bytes, std::size_t& offset, std::uint16_t& value) {
    if (offset + 2 > bytes.size()) return false;
    value = static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
    offset += 2;
    return true;
}

bool take32(const ac::Bytes& bytes, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > bytes.size()) return false;
    value = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
            bytes[offset + 3];
    offset += 4;
    return true;
}

bool takeBytes(const ac::Bytes& bytes, std::size_t& offset, std::size_t count,
               ac::Bytes& value) {
    if (offset > bytes.size() || count > bytes.size() - offset) return false;
    value.assign(bytes.begin() + offset, bytes.begin() + offset + count);
    offset += count;
    return true;
}

std::string defaultPersistencePath() {
    if (const char* path = std::getenv("CARPLAY_PAIRING_FILE"); path && *path)
        return path;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return (std::filesystem::path(home) / "Library" / "Application Support" /
            "iap2-harness" / "airplay-pairings.bin").string();
}

} // namespace

PairingProtocol::PairingProtocol(std::string persistence_path)
    : persistence_path_(persistence_path.empty() ? defaultPersistencePath()
                                                  : std::move(persistence_path)) {
    if (!loadPersistentState()) {
        accessory_id_ = uuidFromRandom();
        identity_seed_ = ac::randomBytes(32);
        identity_public_ = ac::ed25519PublicFromSeed(identity_seed_);
        if (!persistence_path_.empty() && !savePersistentState())
            std::fprintf(stderr, "airplay: could not persist identity/pairings at %s\n",
                         persistence_path_.c_str());
    }
}

bool PairingProtocol::loadPersistentState() {
    if (persistence_path_.empty()) return false;
    std::ifstream file(persistence_path_, std::ios::binary);
    if (!file) return false;
    ac::Bytes bytes((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    if (file.bad() || bytes.size() < 8 + 2 + 2 + 32 + 32 + 4 ||
        !std::equal(kPersistenceMagic.begin(), kPersistenceMagic.end(), bytes.begin()))
        return false;
    std::size_t offset = kPersistenceMagic.size();
    std::uint16_t version = 0, id_size = 0;
    if (!take16(bytes, offset, version) || version != kPersistenceVersion ||
        !take16(bytes, offset, id_size) || id_size == 0 || id_size > 128)
        return false;
    ac::Bytes seed, public_key, id_bytes;
    if (!takeBytes(bytes, offset, 32, seed) || !takeBytes(bytes, offset, 32, public_key) ||
        !takeBytes(bytes, offset, id_size, id_bytes))
        return false;
    if (ac::ed25519PublicFromSeed(seed) != public_key) return false;
    std::uint32_t count = 0;
    if (!take32(bytes, offset, count) || count > 256) return false;
    std::map<std::string, ac::Bytes> loaded;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t controller_id_size = 0;
        ac::Bytes controller_id, controller_key;
        if (!take16(bytes, offset, controller_id_size) || controller_id_size == 0 ||
            controller_id_size > 512 || !takeBytes(bytes, offset, controller_id_size,
                                                    controller_id) ||
            !takeBytes(bytes, offset, 32, controller_key))
            return false;
        loaded.emplace(std::string(controller_id.begin(), controller_id.end()),
                       std::move(controller_key));
    }
    if (offset != bytes.size()) return false;
    identity_seed_ = std::move(seed);
    identity_public_ = std::move(public_key);
    accessory_id_.assign(id_bytes.begin(), id_bytes.end());
    controller_keys_ = std::move(loaded);
    return true;
}

bool PairingProtocol::savePersistentState() const {
    if (persistence_path_.empty() || identity_seed_.size() != 32 ||
        identity_public_.size() != 32 || accessory_id_.empty() ||
        accessory_id_.size() > 128 || controller_keys_.size() > 256)
        return false;

    const std::filesystem::path path(persistence_path_);
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) parent = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
    if (persistence_path_ == defaultPersistencePath()) ::chmod(parent.c_str(), 0700);

    ac::Bytes bytes(kPersistenceMagic.begin(), kPersistenceMagic.end());
    append16(bytes, kPersistenceVersion);
    append16(bytes, static_cast<std::uint16_t>(accessory_id_.size()));
    bytes.insert(bytes.end(), identity_seed_.begin(), identity_seed_.end());
    bytes.insert(bytes.end(), identity_public_.begin(), identity_public_.end());
    bytes.insert(bytes.end(), accessory_id_.begin(), accessory_id_.end());
    append32(bytes, static_cast<std::uint32_t>(controller_keys_.size()));
    for (const auto& [identifier, public_key] : controller_keys_) {
        if (identifier.empty() || identifier.size() > 512 || public_key.size() != 32)
            return false;
        append16(bytes, static_cast<std::uint16_t>(identifier.size()));
        bytes.insert(bytes.end(), identifier.begin(), identifier.end());
        bytes.insert(bytes.end(), public_key.begin(), public_key.end());
    }

    const std::string temporary = persistence_path_ + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        written += static_cast<std::size_t>(n);
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (!ok || ::rename(temporary.c_str(), persistence_path_.c_str()) != 0) {
        ::unlink(temporary.c_str());
        return false;
    }
    ::chmod(persistence_path_.c_str(), 0600);
    const int dir_fd = ::open(parent.c_str(), O_RDONLY);
    if (dir_fd >= 0) { (void)::fsync(dir_fd); (void)::close(dir_fd); }
    return true;
}

std::string PairingProtocol::publicKeyHex() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(identity_public_.size() * 2);
    for (const auto byte : identity_public_) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::string PairingProtocol::deviceIdentifier() const {
    std::array<unsigned char, 32> digest{};
    if (mbedtls_sha256(identity_public_.data(), identity_public_.size(),
                       digest.data(), 0) != 0) return {};
    digest[0] = static_cast<unsigned char>((digest[0] & 0xfc) | 0x02);
    char text[18];
    std::snprintf(text, sizeof text, "%02X:%02X:%02X:%02X:%02X:%02X",
                  digest[0], digest[1], digest[2], digest[3], digest[4], digest[5]);
    return text;
}

ac::Bytes PairingProtocol::errorResponse(std::uint8_t state) const {
    return ac::tlv::encode({{kState, byte(state)}, {ac::tlv::Error, byte(2)}});
}

ac::Bytes PairingProtocol::handlePairSetup(const ac::Bytes& body) {
    const Items input = ac::tlv::decode(body);
    std::uint8_t state = 0;
    if (!stateOf(input, state)) return errorResponse(0);
    try {
        if (state == 1) {
            const auto server = srp_.start("3939");
            srp_started_ = true;
            srp_session_key_.clear();
            pair_setup_complete_ = false;
            return ac::tlv::encode({{kState, byte(2)}, {kPublicKey, server.publicB},
                                    {kSalt, server.salt}});
        }
        if (state == 3) {
            const auto public_a = ac::tlv::get(input, kPublicKey);
            const auto proof = ac::tlv::get(input, kProof);
            if (!srp_started_ || !public_a || !proof) return errorResponse(4);
            const auto verified = srp_.verify(*public_a, *proof);
            if (!verified) return errorResponse(4);
            srp_session_key_ = verified->sessionKey;
            return ac::tlv::encode({{kState, byte(4)}, {kProof, verified->serverProof}});
        }
        if (state == 5) {
            const auto encrypted = ac::tlv::get(input, kEncrypted);
            if (srp_session_key_.empty() || !encrypted) return errorResponse(6);
            const ac::Bytes key = ac::hkdfSha512("Pair-Setup-Encrypt-Salt",
                                                  "Pair-Setup-Encrypt-Info",
                                                  srp_session_key_);
            const auto opened = ac::chacha20Poly1305Decrypt(
                key, nonceLabel("PS-Msg05"), *encrypted);
            if (!opened) return errorResponse(6);
            const Items controller = ac::tlv::decode(*opened);
            const auto id_bytes = ac::tlv::get(controller, kIdentifier);
            const auto controller_public = ac::tlv::get(controller, kPublicKey);
            const auto signature = ac::tlv::get(controller, kSignature);
            if (!id_bytes || !controller_public || !signature || controller_public->size() != 32)
                return errorResponse(6);

            const std::string sign_salt = "Pair-Setup-Controller-Sign-Salt";
            const std::string sign_info = "Pair-Setup-Controller-Sign-Info";
            const ac::Bytes controller_x = ac::hkdfSha512(sign_salt, sign_info, srp_session_key_);
            const ac::Bytes controller_msg = concat(controller_x, *id_bytes, *controller_public);
            if (!ac::ed25519Verify(*controller_public, controller_msg, *signature))
                return errorResponse(6);

            const std::string controller_id(id_bytes->begin(), id_bytes->end());
            controller_keys_[controller_id] = *controller_public;
            if (!savePersistentState())
                std::fprintf(stderr, "airplay: failed to persist controller pairing\n");
            const ac::Bytes accessory_x = ac::hkdfSha512(
                "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info",
                srp_session_key_);
            const ac::Bytes accessory_msg = concat(
                accessory_x, ac::Bytes(accessory_id_.begin(), accessory_id_.end()), identity_public_);
            const ac::Bytes accessory_signature = ac::ed25519Sign(identity_seed_, accessory_msg);
            const ac::Bytes response_inner = ac::tlv::encode({
                {kIdentifier, ac::Bytes(accessory_id_.begin(), accessory_id_.end())},
                {kPublicKey, identity_public_}, {kSignature, accessory_signature}});
            const ac::Bytes sealed = ac::chacha20Poly1305Encrypt(
                key, nonceLabel("PS-Msg06"), response_inner);
            pair_setup_complete_ = true;
            return ac::tlv::encode({{kState, byte(6)}, {kEncrypted, sealed}});
        }
    } catch (const std::exception&) {
        return errorResponse(state);
    }
    return errorResponse(state);
}

ac::Bytes PairingProtocol::handlePairVerify(const ac::Bytes& body) {
    const Items input = ac::tlv::decode(body);
    std::uint8_t state = 0;
    if (!stateOf(input, state)) return errorResponse(0);
    try {
        if (state == 1) {
            const auto controller_public = ac::tlv::get(input, kPublicKey);
            if (!controller_public || controller_public->size() != 32) return errorResponse(2);
            verify_ephemeral_ = ac::x25519Generate();
            controller_ephemeral_ = *controller_public;
            verify_shared_ = ac::x25519SharedSecret(verify_ephemeral_.priv, controller_ephemeral_);
            verify_encryption_key_ = ac::hkdfSha512(
                "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info", verify_shared_);

            const ac::Bytes id(accessory_id_.begin(), accessory_id_.end());
            const ac::Bytes signed_data = concat(verify_ephemeral_.pub, id, controller_ephemeral_);
            const ac::Bytes signature = ac::ed25519Sign(identity_seed_, signed_data);
            const ac::Bytes inner = ac::tlv::encode({{kIdentifier, id}, {kSignature, signature}});
            const ac::Bytes sealed = ac::chacha20Poly1305Encrypt(
                verify_encryption_key_, nonceLabel("PV-Msg02"), inner);
            pair_verified_ = false;
            return ac::tlv::encode({{kState, byte(2)}, {kPublicKey, verify_ephemeral_.pub},
                                    {kEncrypted, sealed}});
        }
        if (state == 3) {
            const auto encrypted = ac::tlv::get(input, kEncrypted);
            if (verify_shared_.empty() || !encrypted) return errorResponse(4);
            const auto opened = ac::chacha20Poly1305Decrypt(
                verify_encryption_key_, nonceLabel("PV-Msg03"), *encrypted);
            if (!opened) return errorResponse(4);
            const Items controller = ac::tlv::decode(*opened);
            const auto id_bytes = ac::tlv::get(controller, kIdentifier);
            const auto signature = ac::tlv::get(controller, kSignature);
            if (!id_bytes || !signature) return errorResponse(4);
            const std::string controller_id(id_bytes->begin(), id_bytes->end());
            const auto saved_key = controller_keys_.find(controller_id);
            if (saved_key == controller_keys_.end()) return errorResponse(4);
            const ac::Bytes signed_data = concat(controller_ephemeral_, *id_bytes,
                                                   verify_ephemeral_.pub);
            if (!ac::ed25519Verify(saved_key->second, signed_data, *signature))
                return errorResponse(4);

            control_write_key_ = ac::hkdfSha512(
                "Control-Salt", "Control-Write-Encryption-Key", verify_shared_);
            control_read_key_ = ac::hkdfSha512(
                "Control-Salt", "Control-Read-Encryption-Key", verify_shared_);
            pair_verified_ = true;
            return ac::tlv::encode({{kState, byte(4)}});
        }
    } catch (const std::exception&) {
        return errorResponse(state);
    }
    return errorResponse(state);
}

} // namespace airplay
