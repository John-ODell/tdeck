#pragma once
#include <stdint.h>
#include <stddef.h>

namespace crypto {

// AES-CTR in place (Meshtastic channel encryption). key_len 16 or 32; 0 = no-op.
void aes_ctr(const uint8_t *key, size_t key_len, uint32_t from, uint32_t id, uint8_t *buf, size_t len);

// Curve25519 keypair (raw 32-byte little-endian, as Meshtastic stores them)
bool x25519_generate(uint8_t priv[32], uint8_t pub[32]);
bool x25519_public(const uint8_t priv[32], uint8_t pub[32]);
bool x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]);

// PKI DM: out = ciphertext || tag(8) || extra_nonce(4). Returns false on failure.
bool pki_encrypt(const uint8_t our_priv[32], const uint8_t peer_pub[32], uint32_t from, uint32_t id,
                 const uint8_t *in, size_t len, uint8_t *out);
// in = ciphertext || tag || extra_nonce (len includes the 12 overhead bytes); out gets len-12 bytes.
bool pki_decrypt(const uint8_t our_priv[32], const uint8_t peer_pub[32], uint32_t from, uint32_t id,
                 const uint8_t *in, size_t len, uint8_t *out);

uint32_t random32();

} // namespace crypto
