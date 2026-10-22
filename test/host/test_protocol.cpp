// Host-side checks of the pure Meshtastic helpers against values in
// research_notes/TDeck Plus Meshtastic firmware/meshtastic_protocol.md.
// Run: test/host/run.sh
#include <cstdio>
#include <cmath>
#include "mesh/protocol.h"
#include "mesh/pb.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

int main()
{
    CHECK(mt::djb2("LongFast") == 130429955u);
    CHECK(mt::num_slots(250) == 104);
    CHECK(std::fabs(mt::slot_freq_mhz("LongFast", 250) - 906.875f) < 0.0005f);

    uint8_t key[32];
    const uint8_t idx1 = 1;
    CHECK(mt::expand_psk(&idx1, 1, key) == 16);
    CHECK(memcmp(key, mt::DEFAULT_PSK, 16) == 0);
    CHECK(mt::channel_hash("LongFast", key, 16) == 0x08);
    const uint8_t idx2 = 2;
    mt::expand_psk(&idx2, 1, key);
    CHECK(key[15] == 0x02);

    uint8_t nonce[16];
    char buf[64];
    mt::make_nonce(nonce, 0x12345678, 0x0000ABCD);
    hex(nonce, 16, buf);
    CHECK(strcmp(buf, "cdab0000000000007856341200000000") == 0);

    uint8_t h[16];
    mt::Header hd{0xFFFFFFFF, 0x12345678, 0xABCD, 0, 0x08, 0, 0x78};
    hd.set_hops(3, 3);
    CHECK(hd.flags == 0x63);
    mt::header_write(hd, h);
    mt::Header back = mt::header_read(h);
    CHECK(back.from == 0x12345678 && back.hop_limit() == 3 && back.hop_start() == 3 && back.relay_node == 0x78);

    uint8_t pb[32];
    PbWriter w(pb, sizeof(pb));
    w.varint(1, mt::PORT_TEXT);
    w.bytes(2, "hi", 2);
    hex(pb, w.len(), buf);
    CHECK(strcmp(buf, "08011202" "6869") == 0);

    PbReader r(pb, w.len());
    uint32_t port = 0; size_t n = 0; const uint8_t *payload = nullptr;
    while (r.next()) {
        if (r.field == 1) port = (uint32_t)r.varint();
        else if (r.field == 2) payload = r.bytes(&n);
        else r.skip();
    }
    CHECK(r.ok() && port == 1 && n == 2 && memcmp(payload, "hi", 2) == 0);

    // Routing{error_reason = NONE} must still carry the field (oneof presence)
    PbWriter rw(pb, sizeof(pb));
    rw.varint(3, 0);
    CHECK(rw.len() == 2 && pb[0] == 0x18 && pb[1] == 0x00);

    // sint32 round trip
    PbWriter sw(pb, sizeof(pb));
    sw.sint32(9, -1234);
    PbReader sr(pb, sw.len());
    CHECK(sr.next() && sr.sint32() == -1234);

    // Print the nonce for the AES check in run.sh
    mt::make_nonce(nonce, 0x12345678, 0x0000ABCD);
    hex(nonce, 16, buf);
    printf("NONCE %s\n", buf);
    hex(mt::DEFAULT_PSK, 16, buf);
    printf("KEY %s\n", buf);
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
