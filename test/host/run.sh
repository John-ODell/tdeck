#!/bin/sh
# Host tests for the pure protocol code; also checks AES-CTR against OpenSSL.
set -e
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
c++ -std=c++17 -Wall -Isrc test/host/test_protocol.cpp -o "$out/test_protocol"
"$out/test_protocol" | tee "$out/log"
nonce=$(grep '^NONCE' "$out/log" | cut -d' ' -f2)
key=$(grep '^KEY' "$out/log" | cut -d' ' -f2)
ct=$(printf '\010\001\022\002hi' | openssl enc -aes-128-ctr -K "$key" -iv "$nonce" | xxd -p)
if [ "$ct" = "4d7577d5e002" ]; then echo "AES-CTR OK ($ct)"; else echo "AES-CTR FAIL ($ct)"; exit 1; fi
