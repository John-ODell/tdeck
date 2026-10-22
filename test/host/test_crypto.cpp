// Host checks of src/mesh/crypto.cpp, built against upstream mbedtls 2.28.
// Prints keys so run_crypto.sh can cross-check X25519 with OpenSSL.
#include <cstdio>
#include <cstring>
#include "mesh/crypto.h"

static void hex(const char *tag, const uint8_t *p, size_t n)
{
    printf("%s ", tag);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main()
{
    uint8_t a_priv[32], a_pub[32], b_priv[32], b_pub[32], ab[32], ba[32];
    if (!crypto::x25519_generate(a_priv, a_pub) || !crypto::x25519_generate(b_priv, b_pub)) { puts("FAIL keygen"); return 1; }
    crypto::x25519_shared(a_priv, b_pub, ab);
    crypto::x25519_shared(b_priv, a_pub, ba);
    hex("A_PRIV", a_priv, 32);
    hex("A_PUB", a_pub, 32);
    hex("B_PUB", b_pub, 32);
    hex("SHARED", ab, 32);
    int fail = memcmp(ab, ba, 32) != 0;

    const char *msg = "\x08\x01\x12\x05hello";
    uint8_t ct[64], pt[64];
    size_t n = strlen(msg);
    fail |= !crypto::pki_encrypt(a_priv, b_pub, 0x11223344, 0xdeadbeef, (const uint8_t *)msg, n, ct);
    fail |= !crypto::pki_decrypt(b_priv, a_pub, 0x11223344, 0xdeadbeef, ct, n + 12, pt);
    fail |= memcmp(pt, msg, n) != 0;
    ct[0] ^= 1;
    fail |= crypto::pki_decrypt(b_priv, a_pub, 0x11223344, 0xdeadbeef, ct, n + 12, pt); // tamper must fail
    printf("%s\n", fail ? "FAILED" : "OK");
    return fail;
}
