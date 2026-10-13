#include "ghostwave/core.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace gw {
namespace {
constexpr char magic[] = "GHOSTWAVE_SN_V1\0";
constexpr char salt[] = "GhostWave_Studio_v1.0_Acoustic_Cloak_Vault_Salt_2026";
constexpr char secret[] = "GhostWave_Core_Internal_Master_Cipher_Key_v1.0_Protected";
void check(NTSTATUS status) { if (status < 0) throw std::runtime_error("Profile cryptography failed"); }
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    Algorithm(LPCWSTR name, ULONG flags = 0) { check(BCryptOpenAlgorithmProvider(&handle, name, nullptr, flags)); }
    ~Algorithm() { BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Key {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~Key() { if (handle) BCryptDestroyKey(handle); }
};
struct Hash {
    BCRYPT_HASH_HANDLE handle = nullptr;
    ~Hash() { if (handle) BCryptDestroyHash(handle); }
};
struct Derived {
    Bytes bytes = Bytes(32);
    Derived() {
        Algorithm algorithm(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        check(BCryptDeriveKeyPBKDF2(algorithm.handle, reinterpret_cast<PUCHAR>(const_cast<char*>(secret)), sizeof(secret) - 1,
                                  reinterpret_cast<PUCHAR>(const_cast<char*>(salt)), sizeof(salt) - 1, 100000, bytes.data(), 32, 0));
    }
    ~Derived() { SecureZeroMemory(bytes.data(), bytes.size()); }
};
Bytes mac(const Bytes& input, const Bytes& key) {
    Algorithm algorithm(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    Hash hash;
    check(BCryptCreateHash(algorithm.handle, &hash.handle, nullptr, 0, const_cast<PUCHAR>(key.data()), 16, 0));
    check(BCryptHashData(hash.handle, const_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()), 0));
    Bytes output(32);
    check(BCryptFinishHash(hash.handle, output.data(), 32, 0));
    return output;
}
Bytes cipher(Bytes input, Bytes iv, const Bytes& key, bool encrypt) {
    Algorithm algorithm(BCRYPT_AES_ALGORITHM);
    check(BCryptSetProperty(algorithm.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)), sizeof(BCRYPT_CHAIN_MODE_CBC), 0));
    Key aes;
    check(BCryptGenerateSymmetricKey(algorithm.handle, &aes.handle, nullptr, 0, const_cast<PUCHAR>(key.data()) + 16, 16, 0));
    ULONG count = 0;
    auto operation = encrypt ? BCryptEncrypt : BCryptDecrypt;
    Bytes original_iv = iv;
    check(operation(aes.handle, input.data(), static_cast<ULONG>(input.size()), nullptr, iv.data(), 16, nullptr, 0, &count, BCRYPT_BLOCK_PADDING));
    iv = original_iv;
    Bytes output(count);
    check(operation(aes.handle, input.data(), static_cast<ULONG>(input.size()), nullptr, iv.data(), 16, output.data(), count, &count, BCRYPT_BLOCK_PADDING));
    output.resize(count);
    SecureZeroMemory(input.data(), input.size());
    return output;
}
}
Profile::Profile() : data(read_json(app_dir() / "resources/profile-defaults.json")), path(resolve()) { data["app_version"] = version; }
fs::path Profile::resolve(fs::path explicit_path) {
    if (!explicit_path.empty()) {
        auto path = fs::absolute(explicit_path);
        if (path.extension() != ".sn") path += L".sn";
        return path;
    }
    auto local = fs::current_path() / "ghostwave.sn";
    return fs::exists(local) ? local : user_dir() / ".ghostwave.sn";
}
void Profile::merge(const Json& values) {
    if (!values.is_object()) throw std::runtime_error("Profile must contain an object");
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!data.contains(it.key())) continue;
        const auto& expected = data[it.key()];
        if (!(expected.is_number() && it.value().is_number()) && expected.type() != it.value().type()) throw std::runtime_error("Invalid profile field: " + it.key());
        if (it.key() == "custom_celebrity_blacklist") for (const auto& name : it.value()) if (!name.is_string()) throw std::runtime_error("Invalid blacklist entry");
        if (it.key() == "user_tickets") for (const auto& ticket : it.value()) if (!ticket.is_object()) throw std::runtime_error("Invalid ticket entry");
        data[it.key()] = it.value();
    }
}
Profile Profile::decode(const Bytes& input) {
    if (input.size() < 16 || !std::equal(input.begin(), input.begin() + 16, reinterpret_cast<const unsigned char*>(magic))) throw std::runtime_error("Invalid profile signature");
    Bytes token;
    try { token = unbase64(std::string(input.begin() + 16, input.end()), true); }
    catch (...) { throw std::runtime_error("Invalid profile encoding"); }
    if (token.size() < 73 || token[0] != 0x80 || (token.size() - 57) % 16) throw std::runtime_error("Invalid profile payload");
    Derived key;
    Bytes authenticated(token.begin(), token.end() - 32);
    auto expected = mac(authenticated, key.bytes);
    unsigned difference = 0;
    for (size_t i = 0; i < 32; ++i) difference |= expected[i] ^ token[token.size() - 32 + i];
    if (difference) throw std::runtime_error("Profile authentication failed");
    auto clear = cipher(Bytes(token.begin() + 25, token.end() - 32), Bytes(token.begin() + 9, token.begin() + 25), key.bytes, false);
    Profile profile;
    try { profile.merge(Json::parse(clear.begin(), clear.end())); }
    catch (...) { SecureZeroMemory(clear.data(), clear.size()); throw; }
    SecureZeroMemory(clear.data(), clear.size());
    return profile;
}
Bytes Profile::encode() const {
    Derived key;
    Bytes iv(16);
    check(BCryptGenRandom(nullptr, iv.data(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    auto text = data.dump(-1, ' ', false, Json::error_handler_t::strict);
    auto encrypted = cipher(Bytes(text.begin(), text.end()), iv, key.bytes, true);
    SecureZeroMemory(text.data(), text.size());
    Bytes token{0x80};
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (int shift = 56; shift >= 0; shift -= 8) token.push_back(static_cast<unsigned char>(timestamp >> shift));
    token.insert(token.end(), iv.begin(), iv.end());
    token.insert(token.end(), encrypted.begin(), encrypted.end());
    auto signature = mac(token, key.bytes);
    token.insert(token.end(), signature.begin(), signature.end());
    auto encoded = base64(token, true);
    Bytes result(reinterpret_cast<const unsigned char*>(magic), reinterpret_cast<const unsigned char*>(magic) + 16);
    result.insert(result.end(), encoded.begin(), encoded.end());
    return result;
}
Profile Profile::load(fs::path explicit_path) {
    const auto path = resolve(explicit_path);
    if (fs::exists(path)) {
        try { auto result = decode(read_bytes(path, 16 * 1024 * 1024)); result.path = path; return result; }
        catch (const std::exception&) {
            auto backup = path; backup += L".bak";
            if (fs::exists(backup)) {
                try { auto result = decode(read_bytes(backup, 16 * 1024 * 1024)); result.path = path; result.recovered = true; return result; }
                catch (...) {}
            }
            throw std::runtime_error("Profile is damaged and has been preserved: " + path.u8string());
        }
    }
    Profile result; result.path = path;
    auto old_cloud = user_dir() / ".suno_sanitizer_cloud_config.json";
    if (fs::exists(old_cloud)) {
        auto legacy = read_json(old_cloud);
        for (const auto& pair : {std::pair{"provider", "cloud_provider"}, {"api_token", "cloud_api_token"}, {"endpoint_url", "cloud_endpoint_url"}, {"timeout_seconds", "cloud_timeout_seconds"}}) {
            if (legacy.contains(pair.first)) result.merge(Json{{pair.second, legacy[pair.first]}});
        }
    }
    auto blacklist = app_dir() / "celebrity_blacklist.json";
    if (fs::exists(blacklist)) result.merge(Json{{"custom_celebrity_blacklist", read_json(blacklist)}});
    result.save();
    return result;
}
void Profile::save(fs::path destination) {
    auto target = destination.empty() ? path : resolve(destination);
    if (target.empty()) target = resolve();
    if (recovered && target == path) throw std::runtime_error("Export the recovered profile to a new file before saving");
    auto bytes = encode();
    if (fs::exists(target)) {
        auto previous = read_bytes(target, 16 * 1024 * 1024);
        decode(previous);
        auto backup = target; backup += L".bak";
        write_atomic(backup, previous);
    }
    write_atomic(target, bytes);
    path = target; recovered = false;
}
}
