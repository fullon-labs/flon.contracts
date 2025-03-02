#include <eosio/crypto.hpp>
#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>
#include <array>
#include <vector>
#include <string_view>

using namespace eosio;
using namespace std;

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static constexpr char pszBase58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static constexpr int8_t mapBase58[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,
    8,  -1, -1, -1, -1, -1, -1, -1, 9,  10, 11, 12, 13, 14, 15, 16, -1, 17, 18,
    19, 20, 21, -1, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1,
    -1, -1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

bool DecodeBase58(const char* psz, vector<unsigned char>& vch) {
    // Skip leading spaces.
    while (*psz && isspace(*psz)) psz++;
    // Skip and count leading '1's.
    int zeroes = 0;
    while (*psz == '1') {
        zeroes++;
        psz++;
    }
    // Allocate enough space in big-endian base256 representation.
    int size = strlen(psz) * 733 / 1000 + 1;  // log(58) / log(256), rounded up.
    vector<unsigned char> b256(size);
    // Process the characters.
    static_assert(sizeof(mapBase58) == 256, "mapBase58.size() should be 256");
    int length = 0;
    while (*psz && !isspace(*psz)) {
        int carry = mapBase58[static_cast<uint8_t>(*psz)];
        if (carry == -1) return false;  // Invalid b58 character
        int i = 0;
        for (auto it = b256.rbegin(); (carry != 0 || i < length) && (it != b256.rend()); ++it, ++i) {
            carry += 58 * (*it);
            *it = carry % 256;
            carry /= 256;
        }
        if (carry != 0) return false;  // Non-zero carry indicates invalid input
        length = i;
        psz++;
    }
    // Skip trailing spaces.
    while (isspace(*psz)) psz++;
    if (*psz != 0) return false;
    // Skip leading zeroes in b256.
    auto it = b256.begin() + (size - length);
    while (it != b256.end() && *it == 0) it++;
    // Copy result into output vector.
    vch.assign(zeroes, 0x00);
    vch.insert(vch.end(), it, b256.end());
    return true;
}

bool decode_base58(string_view str, vector<unsigned char>& vch) {
    return DecodeBase58(str.data(), vch);
}

void str_to_pubkey(const string_view& pubkey, public_key& pub_key ) {
    constexpr string_view pubkey_prefix("FU");
    check(pubkey.size() >= pubkey_prefix.size() && pubkey.substr(0, pubkey_prefix.size()) == pubkey_prefix, 
                    "Invalid public key prefix");

    auto base58substr = pubkey.substr(pubkey_prefix.length());
    vector<unsigned char> vch;
    check( decode_base58(base58substr, vch), "Failed to decode base58 for pubkey");
    check( vch.size() == 37, "Invalid public key length");

    array<char, 33> pubkey_array;
    copy_n(vch.begin(), pubkey_array.size(), pubkey_array.begin());
    ecc_public_key ecc_key(pubkey_array);
    pub_key.emplace<0>(ecc_key);
    
}