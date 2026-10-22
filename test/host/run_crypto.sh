#!/bin/sh
# Builds src/mesh/crypto.cpp against upstream mbedtls (the 2.28 line ESP-IDF 4.4 ships)
# and checks X25519 against OpenSSL. Usage: test/host/run_crypto.sh <path to mbedtls checkout>
set -e
MBED=${1:?path to mbedtls v2.28 checkout}
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
for f in "$MBED"/library/*.c; do cc -c -O1 -I"$MBED/include" "$f" -o "$out/$(basename "$f" .c).o"; done
c++ -std=c++17 -Isrc -Itest/host/shim -I"$MBED/include" test/host/test_crypto.cpp src/mesh/crypto.cpp "$out"/*.o -o "$out/test_crypto"
"$out/test_crypto" | tee "$out/log"
priv=$(grep '^A_PRIV' "$out/log" | cut -d' ' -f2)
pub=$(grep '^A_PUB' "$out/log" | cut -d' ' -f2)
bpub=$(grep '^B_PUB' "$out/log" | cut -d' ' -f2)
shared=$(grep '^SHARED' "$out/log" | cut -d' ' -f2)
# Wrap raw keys in DER for OpenSSL
printf '302e020100300506032b656e04220420%s' "$priv" | xxd -r -p > "$out/a.der"
printf '302a300506032b656e032100%s' "$bpub" | xxd -r -p > "$out/b.der"
ossl_pub=$(openssl pkey -inform DER -in "$out/a.der" -pubout -outform DER | tail -c 32 | xxd -p -c 64)
ossl_shared=$(openssl pkeyutl -derive -inkey "$out/a.der" -keyform DER -peerkey "$out/b.der" -peerform DER | xxd -p -c 64)
[ "$ossl_pub" = "$pub" ] && echo "X25519 public key matches OpenSSL" || { echo "X25519 pub MISMATCH $ossl_pub"; exit 1; }
[ "$ossl_shared" = "$shared" ] && echo "X25519 shared secret matches OpenSSL" || { echo "X25519 shared MISMATCH $ossl_shared"; exit 1; }
