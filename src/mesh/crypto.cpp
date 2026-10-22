#include "crypto.h"
#include <string.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/ccm.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include "protocol.h"

namespace crypto {

uint32_t random32() { return esp_random(); }

static int rng(void *, unsigned char *out, size_t n)
{
    esp_fill_random(out, n);
    return 0;
}

void aes_ctr(const uint8_t *key, size_t key_len, uint32_t from, uint32_t id, uint8_t *buf, size_t len)
{
    if (key_len == 0) return;
    uint8_t nonce[16], stream[16];
    size_t off = 0;
    mt::make_nonce(nonce, from, id);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, key_len * 8);
    mbedtls_aes_crypt_ctr(&ctx, len, &off, nonce, stream, buf, buf);
    mbedtls_aes_free(&ctx);
}

// X25519 scalar multiplication via mbedtls Montgomery curve support.
static bool x25519(const uint8_t scalar[32], const uint8_t *point /* null = base */, uint8_t out[32])
{
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point P, R;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&P);
    mbedtls_ecp_point_init(&R);
    bool ok = false;
    uint8_t k[32];
    memcpy(k, scalar, 32);
    // RFC 7748 clamping
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519)) break;
        if (mbedtls_mpi_read_binary_le(&d, k, 32)) break;
        if (point) {
            uint8_t u[32];
            memcpy(u, point, 32);
            u[31] &= 127;
            if (mbedtls_mpi_read_binary_le(&P.X, u, 32)) break;
            if (mbedtls_mpi_lset(&P.Z, 1)) break;
        } else if (mbedtls_ecp_copy(&P, &grp.G)) {
            break;
        }
        if (mbedtls_ecp_mul(&grp, &R, &d, &P, rng, nullptr)) break;
        if (mbedtls_mpi_write_binary_le(&R.X, out, 32)) break;
        ok = true;
    } while (0);
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_point_free(&P);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

bool x25519_public(const uint8_t priv[32], uint8_t pub[32]) { return x25519(priv, nullptr, pub); }
bool x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]) { return x25519(priv, peer_pub, out); }

bool x25519_generate(uint8_t priv[32], uint8_t pub[32])
{
    esp_fill_random(priv, 32);
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    return x25519_public(priv, pub);
}

static bool shared_key(const uint8_t our_priv[32], const uint8_t peer_pub[32], uint8_t key[32])
{
    uint8_t secret[32];
    if (!x25519_shared(our_priv, peer_pub, secret)) return false;
    mbedtls_sha256(secret, 32, key, 0);
    return true;
}

bool pki_encrypt(const uint8_t our_priv[32], const uint8_t peer_pub[32], uint32_t from, uint32_t id,
                 const uint8_t *in, size_t len, uint8_t *out)
{
    uint8_t key[32], nonce[16];
    if (!shared_key(our_priv, peer_pub, key)) return false;
    uint32_t extra = random32();
    if (!extra) extra = 1;
    mt::make_nonce(nonce, from, id, extra);
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_ccm_encrypt_and_tag(&ctx, len, nonce, 13, nullptr, 0, in, out, out + len, 8) == 0;
    mbedtls_ccm_free(&ctx);
    mt::put_le32(out + len + 8, extra);
    return ok;
}

bool pki_decrypt(const uint8_t our_priv[32], const uint8_t peer_pub[32], uint32_t from, uint32_t id,
                 const uint8_t *in, size_t len, uint8_t *out)
{
    if (len <= mt::PKC_OVERHEAD) return false;
    size_t clen = len - mt::PKC_OVERHEAD;
    uint8_t key[32], nonce[16];
    if (!shared_key(our_priv, peer_pub, key)) return false;
    uint32_t extra = mt::get_le32(in + clen + 8);
    mt::make_nonce(nonce, from, id, extra);
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    bool ok = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_ccm_auth_decrypt(&ctx, clen, nonce, 13, nullptr, 0, in, out, in + clen, 8) == 0;
    mbedtls_ccm_free(&ctx);
    return ok;
}

} // namespace crypto
