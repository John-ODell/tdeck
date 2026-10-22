# Meshtastic Over-the-Air (LoRa) Protocol: Implementation-Ready Spec (firmware 2.5 to 2.8)

Method note: the meshtastic/firmware repo was shallow-cloned at develop commit `0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b` (2026-10-22, version.properties = 2.8.2), along with meshtastic/protobufs at `569a2b3ae7b6bda1a7ab177463a55d8ede3bef53` (2026-10-21). The same files were also pulled at the latest release tag of each line: `v2.5.23.bf958ed`, `v2.6.13.0561f2c`, `v2.7.26.54e0d8d` and `v2.8.1.8e6a88d`. Line numbers refer to the develop commit unless a tag is named. Short link prefixes used below:
- FW = https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/
- FW27 = https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/
- PB = https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/

## Q1. US region and LongFast PHY parameters, and the frequency-slot formula

### Takeaway
US LongFast uses BW 250 kHz, SF11, CR 4/5, a 16-symbol preamble, LoRa sync word 0x2B, explicit header and CRC on. The US band is 902.0 to 928.0 MHz with a 30 dBm limit, giving 104 slots of 250 kHz. djb2("LongFast") = 130429955, and 130429955 % 104 = 19 (0-based), which is UI slot 20. The center frequency is 902.0 + 0.125 + 19 × 0.25 = **906.875 MHz**, matching the official docs.

### Cited Findings
- LongFast parameters come from `modemPresetToParams()`, where LONG_FAST is the `default:` branch: `bwKHz = wideLora ? 812.5f : 250.0f; cr = 5; sf = 11;`. Here `cr = 5` means 4/5. For reference: ShortTurbo 500/SF7/CR5, ShortFast 250/7/5, ShortSlow 250/8/5, MediumFast 250/9/5, MediumSlow 250/10/5, LongModerate 125/11/8, LongSlow 125/12/8, LongTurbo 500/11/8 — [MeshRadio.h L216-300](FW/MeshRadio.h) (https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshRadio.h)
- Docs confirm "Long Range / Fast": 250 kHz, SF 11, CR 4/5. They also say a factory-reset radio uses LongFast with "Frequency Slot of 0, which translates to Slot 20", centered at 906.875 MHz — [meshtastic.org radio settings](https://meshtastic.org/docs/overview/radio-settings/)
- Sync word, verbatim: `const uint8_t syncWord = 0x2b;`. The source comment says pre-1.2 firmware used 0x12 (very old firmware used 0x14), and that 0x34 is reserved for LoRaWAN — [RadioLibInterface.h L115-124](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioLibInterface.h). The SX126x driver passes it straight to RadioLib: `lora.begin(getFreq(), bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO)` — [SX126xInterface.cpp L136](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/SX126xInterface.cpp)
- Preamble: `static constexpr uint16_t preambleLengthDefault = 16;` and `wideLoraPreambleLengthDefault = 12` (2.4 GHz only) — [RadioInterface.h L112-116](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.h). Tags v2.5.23, v2.6.13 and v2.7.26 also have `uint16_t preambleLength = 16;` — [FW27 RadioInterface.h L98](https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/RadioInterface.h)
- Packet config: `.implicitHeader = false, .crcEnabled = true, .ldrOptimize = (1 << sf) / bw >= 16`. For SF11/250 kHz the symbol time is 2048/250 = 8.19 ms, so LDRO is OFF — [RadioLibInterface.h L106-112](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioLibInterface.h)
- US region definition (develop): `RDEF(US, 902.0f, 928.0f, 100, 30, false, false, PROFILE_STD, PRESET(LONG_FAST), 0)`, which is freqStart, freqEnd, duty cycle 100%, powerLimit 30 dBm, no freq switching, not wideLora, standard profile (spacing 0, padding 0), default preset LongFast, overrideSlot 0 (0 = use the channel-name hash) — [RadioInterface.cpp L120](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp), [PROFILE_STD L89](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp), [RegionInfo struct MeshRadio.h L56-70](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshRadio.h). In 2.5 to 2.7 the macro layout differs but the values match: `RDEF(US, 902.0f, 928.0f, 100, 0, 30, true, false, false)` (freqStart, freqEnd, dutyCycle, spacing, powerLimit, audioPermitted, freqSwitching, wideLora) — [FW27 RadioInterface.cpp L48](https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/RadioInterface.cpp)
- TX power: if `tx_power == 0` or it exceeds `powerLimit` (and the user is not licensed), power = `powerLimit`. If no limit is set, the default is 17 dBm — [RadioInterface.cpp applyModemConfig ~L1335-1345](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp). The SX1262 tops out at +22 dBm, so in practice the chip limit applies (the module may add PA gain).
- djb2 hash, verbatim: `uint32_t hash = 5381; while ((c = *str++) != 0) hash = ((hash << 5) + hash) + (unsigned char)c;` — [RadioInterface.cpp L990-999](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp)
- Slot formula in 2.5 to 2.7, verbatim:
  `uint32_t numChannels = floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000)));`
  `uint32_t channel_num = (loraConfig.channel_num ? loraConfig.channel_num - 1 : hash(channelName)) % numChannels;`
  `float freq = myRegion->freqStart + (bw / 2000) + (channel_num * (bw / 1000));` — [FW27 RadioInterface.cpp ~L847-862](https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/RadioInterface.cpp). v2.5.23 and v2.6.13 have the identical freq line at L558.
- Slot formula in 2.8 / develop, verbatim:
  `float freqSlotWidth = spacing + (padding * 2) + (bw / 1000);`
  `uint32_t numFreqSlots = round((freqEnd - freqStart + spacing) / freqSlotWidth);`
  `freq = freqStart + (bw / 2000) + padding + (channel_num * freqSlotWidth);`
  The result is `saveFreq(freq + loraConfig.frequency_offset)`. For US (spacing 0, padding 0) this gives the same answer as 2.5 to 2.7 — [RadioInterface.cpp L1354-1400](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp)
- The name that gets hashed is `channels.getName(primaryIndex)`. If the primary channel name is empty "" (the default), `getName()` returns the preset display name, e.g. "LongFast" (or "Custom" when `use_preset` is false). The legacy name "Default" is rewritten to "" — [Channels.cpp L424-440, L74-76](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.cpp). The display string is literally `"LongFast"` — [DisplayFormatters.cpp L36-37](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/DisplayFormatters.cpp)
- A non-zero `override_frequency` bypasses all of this — [RadioInterface.cpp L1370-1373](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp)

### Inferences
- Computed with Python: djb2("LongFast") = 130429955. numChannels = 26 / 0.25 = 104. 130429955 % 104 = 19, so UI slot 20 at 906.875 MHz, which matches the docs.
- RadioLib call for the SX1262: `radio.begin(906.875, 250.0, 11, 5, 0x2B, <dBm>, 16, <tcxoV>)`. RadioLib converts sync word 0x2B into the SX126x 2-byte register value (0x24B4) itself, so pass 0x2B and do not set the register by hand.
- Nodes decode only packets whose preset, frequency and sync word all match. A changed primary channel name moves the frequency unless `channel_num` is set explicitly.

### Gaps
- The T-Deck Plus TCXO voltage, DIO2 RF-switch use and current limit are board-variant details and were not checked here (see variants/t-deck).

## Q2. The 16-byte PacketHeader

### Takeaway
Every on-air packet is a 16-byte header in little-endian native struct order, followed by the encrypted payload (at most 255 − 16 = 239 bytes). The header holds to(4), from(4), id(4), flags(1), channel-hash(1), next_hop(1) and relay_node(1).

### Cited Findings
- Struct, verbatim: `typedef struct { NodeNum to, from; PacketId id; uint8_t flags; uint8_t channel; uint8_t next_hop; uint8_t relay_node; } PacketHeader;`. The comment says it "has to exactly match the wire layout". `NodeNum` and `PacketId` are `uint32_t` — [RadioInterface.h L36-56](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.h), [MeshTypes.h L9-10](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshTypes.h)
- Constants: `MAX_LORA_PAYLOAD_LEN 255`, `MESHTASTIC_HEADER_LENGTH 16`, `MESHTASTIC_PKC_OVERHEAD 12`, `MESHTASTIC_AEAD_OVERHEAD 12`, `PACKET_FLAGS_HOP_LIMIT_MASK 0x07`, `PACKET_FLAGS_WANT_ACK_MASK 0x08`, `PACKET_FLAGS_VIA_MQTT_MASK 0x10`, `PACKET_FLAGS_HOP_START_MASK 0xE0`, `PACKET_FLAGS_HOP_START_SHIFT 5`. Also `MAX_RADIO_PAYLOAD_LEN = MAX_LORA_PAYLOAD_LEN - sizeof(PacketHeader)` — [RadioInterface.h L20-30, L74](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.h)
- Encoding: `flags = hop_limit | (want_ack ? 0x08 : 0) | (via_mqtt ? 0x10 : 0); flags |= (hop_start << 5) & 0xE0;`. A hop_limit above `HOP_MAX` (7) is clamped to `HOP_RELIABLE` (3). `from` must be non-zero (asserted) — [RadioInterface.cpp L1530-1560](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp)
- Decoding: hop_limit = flags & 7; hop_start = (flags & 0xE0) >> 5; want_ack = flags & 0x08; via_mqtt = flags & 0x10. "If hop_start is not set, next_hop and relay_node are invalid (firmware <2.3)" — when hop_start == 0 they are treated as 0. Packets with `from == 0` are dropped — [RadioLibInterface.cpp L931-957](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioLibInterface.cpp)
- `NODENUM_BROADCAST UINT32_MAX`, `HOP_MAX 7`, `HOP_RELIABLE 3`, `NO_NEXT_HOP_PREFERENCE 0`, `NO_RELAY_NODE 0` — [MeshTypes.h L12-46](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshTypes.h)
- `relay_node` and `next_hop` hold the last byte of a NodeNum (`getLastByteOfNodeNum`). The originator sets relay_node to itself on send, and relayers overwrite it with their own byte — [FloodingRouter.cpp L22](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/FloodingRouter.cpp), [NextHopRouter.cpp L67-71](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NextHopRouter.cpp), [Router.cpp L526](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- On origination, `p->hop_start = p->hop_limit` — [Router.cpp L552](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)

### Inferences
- The ESP32 is little-endian, so the C struct maps 1:1. Use `__attribute__((packed))` or memcpy to be safe; the fields happen to be naturally aligned (4+4+4+1+1+1+1 = 16).
- Self-computed example header for a broadcast from 0x12345678, id 0x0000ABCD, hop_limit 3, hop_start 3, LongFast channel hash 0x08, next_hop 0, relay 0x78: `ff ff ff ff 78 56 34 12 cd ab 00 00 63 08 00 78`.
- hops_away = hop_start − hop_limit (used by `getHopsAway`).

### Gaps
- None for layout.

## Q3. Channel encryption (AES-CTR), default PSK, nonce and counter

### Takeaway
Channel traffic is AES-128-CTR, or AES-256-CTR for 32-byte keys, over the serialized `Data` protobuf. There is no MAC and no padding: ciphertext length equals plaintext length. The 16-byte IV is packetId as uint64 LE (bytes 0-7), then fromNode as uint32 LE (bytes 8-11), then 4 zero bytes (12-15). The CTR counter is the last 4 bytes (big-endian increment, starting at 0). A 1-byte PSK index N maps to the default key with its last byte increased by N−1. Index 0 means no encryption.

### Cited Findings
- Default key, verbatim: `static const uint8_t defaultpsk[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};` — [Channels.h L163-164](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.h). A new default channel gets `psk.bytes[0] = 1; psk.size = 1;` — [Channels.cpp L170-172](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.cpp)
- Short-PSK expansion in `getKey()`, verbatim:
  - Length 1: `if (pskIndex == 0) k.length = 0;` (encryption off), else `memcpy(k.bytes, defaultpsk, 16); *last = *last + pskIndex - 1;`
  - Length 0: a SECONDARY channel borrows the primary's key; otherwise encryption is off.
  - Length 2-15: zero-padded to 16. Length 17-31: zero-padded to 32.
  — [Channels.cpp L248-296](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.cpp)
- `encryptPacket` calls `initNonce(fromNode, packetId); encryptAESCtr(key, nonce, numBytes, bytes);`. Decrypt is the same operation ("For CTR, the implementation is the same") — [CryptoEngine.cpp L626-643](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.cpp)
- `encryptAESCtr` uses `CTR<AES128>` if the key length is 16, otherwise `CTR<AES256>`, then `ctr->setIV(_nonce, 16); ctr->setCounterSize(4); ctr->encrypt(...)` — [CryptoEngine.cpp L645-672](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.cpp). `setCounterSize(4)` is present in v2.5.23 (L243), v2.6.13 (L252) and v2.7.26 (L256).
- `initNonce`, verbatim: `memset(nonce, 0, 16); memcpy(nonce, &packetId, sizeof(uint64_t)); memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t)); if (extraNonce) memcpy(nonce + sizeof(uint32_t), &extraNonce, sizeof(uint32_t));`. Note that extraNonce, used only by PKI, overwrites bytes 4-7, not 12-15 — [CryptoEngine.cpp L675-686](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.cpp)
- The packet id is a 32-bit `fixed32`, passed as uint64 with the upper 4 bytes = 0 — [mesh.proto MeshPacket id = 6](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)
- Send path: encode `Data` → reject if `numbytes + 16 > 255` (TOO_LARGE) → `channels.setActiveByIndex(chIndex)` returns the hash → `p->channel = hash` → CTR-encrypt with `getFrom(p)` and `p->id` — [Router.cpp perhapsEncode L1196-1345](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- Receive path: for each channel whose hash equals header.channel, decrypt, then `pb_decode` into `Data`. Treat as failure if the decode fails or `portnum == UNKNOWN_APP (0)` ("bad psk?"), and try the next channel — [Router.cpp perhapsDecode L994-1040](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- New in 2.8: an opt-in per-channel `use_aead` (ChannelSettings field 8) switches to AES-CCM. It uses a 12-byte tag, AAD = from(4 LE) || to(4 LE), and the nonce from `initNonce(from,id)`. It XORs 0xAE into the channel hash and never falls back to CTR — [CryptoEngine.cpp L568-612](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.cpp), [CryptoEngine.h L152-155](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.h), [channel.proto L98](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/channel.proto). The default LongFast channel does not use it.

### Inferences
- Because bytes 12-15 start at zero and payloads are at most 15 blocks, the 4-byte counter never wraps. Standard 128-bit big-endian AES-CTR (mbedtls `mbedtls_aes_crypt_ctr`, ESP-IDF hardware AES) therefore gives identical output.
- Self-computed test vector (OpenSSL aes-128-ctr, default key):
  - from = 0x12345678, id = 0x0000ABCD
  - IV = `cdab0000000000007856341200000000`
  - plaintext Data{portnum=1, payload="hi"} = `08 01 12 02 68 69`
  - ciphertext = `4d 75 77 d5 e0 02`
  This vector was not taken from the firmware. Validate it against a real capture.

### Gaps
- No official published known-answer vector was found in the time available.

## Q4. Channel hash byte

### Takeaway
hash = XOR of every byte of the channel name, XORed with the XOR of every byte of the expanded key. Default LongFast with key AQ== gives **0x08**.

### Cited Findings
- `xorHash` is a running XOR over the bytes. `generateHash`: `h = xorHash(name, strlen(name)); h ^= xorHash(k.bytes, k.length);` (k is the expanded key from `getKey`). In 2.8+ it also does `if (use_aead) h ^= 0xAE;` — [Channels.cpp L27-58](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.cpp)
- The name is `getName()`, so an empty name hashes as "LongFast" — [Channels.cpp L424-440](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Channels.cpp)
- PKI packets carry channel byte 0 (`p->channel = 0`) — [Router.cpp ~L1316](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)

### Inferences
- Computed: xor("LongFast") ^ xor(defaultpsk) = 0x08.
- An encryption-off channel hashes only the name, because the key length is 0.
- The hash is only a hint: collisions are expected, which is why the receiver tries the decrypt and checks the protobuf.

## Q5. Protobuf messages and enums

### Takeaway
The plaintext under encryption is a `meshtastic.Data` protobuf. Inside it, payload carries the app message (raw UTF-8 for text; User / Position / Telemetry / Routing protobufs for the other ports).

### Cited Findings
- `message Data` ([mesh.proto L1270+](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)):
  - `PortNum portnum = 1;`
  - `bytes payload = 2;`
  - `bool want_response = 3;`
  - `fixed32 dest = 4;`
  - `fixed32 source = 5;`
  - `fixed32 request_id = 6;`
  - `fixed32 reply_id = 7;`
  - `fixed32 emoji = 8;`
  - `optional uint32 bitfield = 9;`
  - `bytes xeddsa_signature = 10;` (new in 2.8)
- Bitfield: `BITFIELD_OK_TO_MQTT_SHIFT 0`, `BITFIELD_WANT_RESPONSE_SHIFT 1`. The originator always sets `has_bitfield`, OR-ing in `config_ok_to_mqtt << 0` and `want_response << 1`. Receivers OR bit 1 back into want_response — [Router.h L330-333](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.h), [Router.cpp L1203-1206, ~L1052](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- `DATA_PAYLOAD_LEN = 233;` — [mesh.proto L2107](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)
- PortNum values: `UNKNOWN_APP = 0`, `TEXT_MESSAGE_APP = 1`, `REMOTE_HARDWARE_APP = 2`, `POSITION_APP = 3`, `NODEINFO_APP = 4`, `ROUTING_APP = 5`, `ADMIN_APP = 6`, `TEXT_MESSAGE_COMPRESSED_APP = 7`, `WAYPOINT_APP = 8`, `KEY_VERIFICATION_APP = 12`, `STORE_FORWARD_APP = 65`, `RANGE_TEST_APP = 66`, `TELEMETRY_APP = 67`, `TRACEROUTE_APP = 70`, `NEIGHBORINFO_APP = 71`, `MAP_REPORT_APP = 73`, `PRIVATE_APP = 256`, `MAX = 511` — [portnums.proto](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/portnums.proto)
- `message User` ([mesh.proto L1027+](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)):
  - `string id = 1;`
  - `string long_name = 2;`
  - `string short_name = 3;`
  - `bytes macaddr = 4 [deprecated = true];`
  - `HardwareModel hw_model = 5;`
  - `bool is_licensed = 6;`
  - `Config.DeviceConfig.Role role = 7;`
  - `bytes public_key = 8;`
  - `optional bool is_unmessagable = 9;`
- `message Position` ([mesh.proto L23-212](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)):
  - `optional sfixed32 latitude_i = 1;` (degrees × 1e7)
  - `optional sfixed32 longitude_i = 2;`
  - `optional int32 altitude = 3;`
  - `fixed32 time = 4;`
  - `LocSource location_source = 5;`
  - `AltSource altitude_source = 6;`
  - `fixed32 timestamp = 7;`
  - `int32 timestamp_millis_adjust = 8;`
  - `optional sint32 altitude_hae = 9;`
  - `optional sint32 altitude_geoidal_separation = 10;`
  - `PDOP = 11`, `HDOP = 12`, `VDOP = 13`, `gps_accuracy = 14`
  - `optional uint32 ground_speed = 15;`
  - `optional uint32 ground_track = 16;`
  - `fix_quality = 17`, `fix_type = 18`
  - `uint32 sats_in_view = 19;`
  - `sensor_id = 20`, `next_update = 21`, `seq_number = 22`
  - `uint32 precision_bits = 23;`
- `message Routing` ([mesh.proto L1117+](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)):
  - `oneof variant { RouteDiscovery route_request = 1; RouteDiscovery route_reply = 2; Error error_reason = 3; }`
  - `bytes ack_proof = 4;` (2.8)
  - Error values: NONE 0, NO_ROUTE 1, GOT_NAK 2, TIMEOUT 3, NO_INTERFACE 4, MAX_RETRANSMIT 5, NO_CHANNEL 6, TOO_LARGE 7, NO_RESPONSE 8, DUTY_CYCLE_LIMIT 9, BAD_REQUEST 32, NOT_AUTHORIZED 33, PKI_FAILED 34, PKI_UNKNOWN_PUBKEY 35, ADMIN_BAD_SESSION_KEY 36, ADMIN_PUBLIC_KEY_UNAUTHORIZED 37, RATE_LIMIT_EXCEEDED 38, PKI_SEND_FAIL_PUBLIC_KEY 39
- `message MeshPacket` (API and MQTT only, not sent over the air as a protobuf): from 1, to 2, channel 3, decoded 4 / encrypted 5, id 6, rx_time 7, rx_snr 8, hop_limit 9, want_ack 10, priority 11, rx_rssi 12, via_mqtt 14, hop_start 15, public_key 16, pki_encrypted 17, next_hop 18, relay_node 19 — [mesh.proto L1674-2052](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)
- HardwareModel: `T_DECK = 50;` ("LilyGo T-Deck with ESP32-S3 CPU, Keyboard and IPS display"). There is no separate T-Deck Plus value; `T_DECK_PRO = 102` is a different device — [mesh.proto L478-480, L751](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)
- Roles: CLIENT 0, CLIENT_MUTE 1, ROUTER 2, ROUTER_CLIENT 3 (deprecated), TRACKER 5, TAK_TRACKER 10, ROUTER_LATE 11, CLIENT_BASE 12 — [config.proto L27-159](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/config.proto)

### Inferences
- The `fixed32` fields (dest, source, request_id, reply_id, emoji) use wire type 5 (4 bytes LE); latitude_i and longitude_i are `sfixed32`. Use nanopb with the official .proto/.options files, or hand-encode carefully.
- TELEMETRY_APP payloads are `meshtastic.Telemetry` from telemetry.proto, which was not examined field by field here.

### Gaps
- Telemetry message field layout was not extracted (telemetry.proto).

## Q6. Addresses, node number, packet id

### Takeaway
Broadcast is 0xFFFFFFFF. The node number is the last 4 bytes of the MAC, big-endian composed. Numbers 0-3 are reserved and collisions trigger a random re-pick. User.id is "!%08x". Packet ids keep a 10-bit rolling counter in the low bits and 22 random bits above it.

### Cited Findings
- `nodeNum = (ourMacAddr[2] << 24) | (ourMacAddr[3] << 16) | (ourMacAddr[4] << 8) | ourMacAddr[5];`. Re-pick `random(NUM_RESERVED, LONG_MAX)` if the number is in use by another key, equals broadcast, or is below `NUM_RESERVED` (4) — [NodeDB.cpp L2236-2270](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NodeDB.cpp)
- Defaults: `long_name = "Meshtastic %04x"` (nodenum & 0xffff), `short_name = "%04x"`, `id = "!%08x"` — [NodeDB.cpp L2211-2222](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NodeDB.cpp)
- `generatePacketId()`: seed `rollingPacketId = random(0x7fffffff)` once, then `rollingPacketId++; rollingPacketId &= ID_COUNTER_MASK; id = rollingPacketId | random(0x7fffffff) << 10;`, with `ID_COUNTER_MASK (UINT32_MAX >> 22)` = 0x3FF — [Router.cpp L337-357](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp), [MeshTypes.h L20](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshTypes.h)
- Relayers skip packets with id == 0 — [NextHopRouter.cpp L34](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NextHopRouter.cpp)

### Inferences
- Never reuse a (from, id) pair: it is both the dedupe key and the CTR nonce. Reuse breaks confidentiality and causes peers to drop the packet as a duplicate.

## Q7. Flooding, relay, ACK and PKI rules

### Takeaway
Managed flooding: a node that hears a packet not addressed to itself, with hop_limit > 0, waits an SNR-weighted random delay. It cancels its relay if it hears someone else relay first (except ROUTER / ROUTER_LATE); otherwise it retransmits with hop_limit − 1 and relay_node = its own last byte. CLIENT does rebroadcast; CLIENT_MUTE, or rebroadcast_mode NONE, does not. The default hop limit is 3. A want_ack unicast is answered with a ROUTING_APP packet: Routing{error_reason = NONE}, request_id = the original id. Broadcasters accept the "implicit ACK" of overhearing their own packet relayed. DMs between PKI-capable nodes use X25519 + SHA-256 + AES-256-CCM. Since 2.7, a channel-encrypted (legacy) TEXT DM addressed to a node is rejected.

### Cited Findings
- `isRebroadcaster()`: `role != CLIENT_MUTE && rebroadcast_mode != NONE` — [FloodingRouter.cpp L154-157](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/FloodingRouter.cpp)
- No relay if any of these hold: no interface, packet is to us, packet is from us, id == 0, hop_limit == 0, not a rebroadcaster, licensed, or `next_hop` set and not our byte. On relay: `hop_limit--`, `relay_node = our last byte` — [NextHopRouter.cpp L30-71, L243-301](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NextHopRouter.cpp)
- Duplicates are detected via `wasSeenRecently(p)` (PacketHistory keyed by from+id). On a dupe, `perhapsCancelDupe` cancels our queued relay unless the role is ROUTER or ROUTER_LATE. A dupe with a higher hop_limit "upgrades" the queued copy — [FloodingRouter.cpp L28-125](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/FloodingRouter.cpp), [NextHopRouter.cpp L133-180](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NextHopRouter.cpp)
- Contention window: `CWmin = 3`, `CWmax = 8`. `slotTimeMsec = max(2.25, NUM_SYM_CAD(4)+0.5) * (2^sf / bw) + 0.2+0.4+7` ms. For LongFast this is 4.5 × 8.192 + 7.6 ≈ 44 ms.
  - Original sends: `random(0, 2^CWsize) * slotTimeMsec`, with `CWsize = map(channelUtil, 0, 100, CWmin, CWmax)`.
  - Rebroadcasts: `CWsize = map(snr, -20, 10, CWmin, CWmax)`.
    - ROUTER: `random(0, 2*CWsize) * slot`.
    - Others: `(2*CWmax*slot) + random(0, 2^CWsize) * slot`.
  - The result: low SNR (distant) nodes relay sooner.
  — [RadioInterface.h L107-120](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.h), [RadioInterface.cpp L829-888, L1435-1455](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/RadioInterface.cpp)
- ACK construction: `Routing{error_reason=err}`, portnum ROUTING_APP, priority ACK, `to = original from`, `decoded.request_id = original id`, same channel. Hop limit comes from `getHopLimitForResponse`: hopsUsed + 2 if that is below the configured limit, 0 if the request had hop_start 0, otherwise the configured limit — [MeshModule.cpp L55-94](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/MeshModule.cpp), [RoutingModule.cpp L64-81](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/modules/RoutingModule.cpp)
- ReliableRouter sends the ACK (or NAK with PKI_UNKNOWN_PUBKEY / NO_CHANNEL) for want_ack packets addressed to us. It generates an implicit ACK when it overhears its own packet being rebroadcast. Retries: `NUM_RELIABLE_RETX = 3` — [ReliableRouter.cpp L30-158](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/ReliableRouter.cpp), [NextHopRouter.h L113](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/NextHopRouter.h)
- PKI encrypt is used when all of these hold: the packet is from us, not licensed (ham mode), private key is 32 bytes, destination is not broadcast, portnum is not TRACEROUTE / NODEINFO / ROUTING / POSITION, and the destination key is known. Without a known key the send fails with `PKI_SEND_FAIL_PUBLIC_KEY` ("refuse legacy DM") — [Router.cpp L1137-1160, L1290-1320](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- PKI crypto:
  - `shared_key = SHA256(X25519(our_priv, peer_pub))`.
  - `initNonce(fromNode, packetId, extraNonce)` with a random 32-bit extraNonce.
  - `aes_ccm_ae(shared_key, 32, nonce, 8, ...)`: AES-256-CCM, 8-byte tag, L = 2, so the nonce is the first 13 bytes of the 16-byte buffer.
  - On-air payload = ciphertext || tag(8) || extraNonce(4 LE). That is `MESHTASTIC_PKC_OVERHEAD = 12`.
  - Header channel byte = 0.
  — [CryptoEngine.cpp L295-350, L405-459](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/CryptoEngine.cpp), [aes-ccm.cpp L142-162](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/aes-ccm.cpp)
- A PKI decrypt is attempted only if `channel == 0`, the packet is to us, not broadcast, and size > 12 — [Router.cpp L905-907](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- Legacy DM rejection: `else if (!owner.is_licensed && isToUs(p) && decodedtmp.portnum == TEXT_MESSAGE_APP) { LOG_WARN("Rejecting legacy DM"); return DECODE_FAILURE; }`. Present in v2.7.26 ([FW27 Router.cpp L498-501](https://github.com/meshtastic/firmware/blob/v2.7.26.54e0d8d/src/mesh/Router.cpp)), v2.8.1 and develop. Not present in v2.5.23 or v2.6.13.
- 2.8 additions:
  - XEdDSA signatures (`Data.xeddsa_signature`, 64 bytes) on broadcasts, NodeInfo and Position, unless `packet_signature_policy = COMPATIBLE`.
  - `Routing.ack_proof` (HMAC with the pairwise key).
  - Opt-in AEAD channels.
  — [Router.cpp L1207-1232](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp), [config.proto L1672 since_firmware 2.8.0](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/config.proto)

### Inferences
- Minimum interop for a CLIENT:
  1. Send and receive CTR channel packets.
  2. Dedupe on (from, id).
  3. Relay with the delay rules above.
  4. Broadcast NODEINFO_APP User with a 32-byte public_key (needed so others can DM you).
  5. Implement PKI DMs, since 2.7+ peers reject channel-encrypted text DMs to themselves. Broadcast text on the channel still works with CTR only.
- Unsigned packets from your client should still be accepted by 2.8 nodes under the default policy. This is not verified: check `checkXeddsaReceivePolicy` before relying on it.

### Gaps
- The exact XEdDSA receive-policy defaults were not read (`checkXeddsaReceivePolicy`).
- The NodeInfo exchange cadence and want_response behaviour were not read.

## Q8. Max payload size and interop pitfalls

### Takeaway
LoRa frame ≤ 255 bytes, so header (16) + encrypted Data ≤ 239. The encoded Data protobuf must satisfy `numbytes + 16 ≤ 255`, and `+12` more for PKI or AEAD. `DATA_PAYLOAD_LEN = 233` is the protobuf constant for the inner `payload` field. The "237" figure in the brief is not what the source says.

### Cited Findings
- `if (numbytes + MESHTASTIC_HEADER_LENGTH > MAX_LORA_PAYLOAD_LEN) return TOO_LARGE;` and PKI adds `MESHTASTIC_PKC_OVERHEAD` — [Router.cpp ~L1264-1295](https://github.com/meshtastic/firmware/blob/0d3da2dcda5d83fe8ecfb08ce23c9ef77464d39b/src/mesh/Router.cpp)
- `DATA_PAYLOAD_LEN = 233` — [mesh.proto L2107](https://github.com/meshtastic/protobufs/blob/569a2b3ae7b6bda1a7ab177463a55d8ede3bef53/meshtastic/mesh.proto)

### Inferences
Interop pitfalls:
- Wrong slot formula or hashed name: an empty name must hash as "LongFast".
- Wrong sync word: pass 0x2B to RadioLib.
- Preamble set to 8 instead of 16.
- CR given as 4/5 in a different encoding: RadioLib expects 5.
- Header CRC disabled.
- Big-endian header fields.
- Nonce built with the id in the wrong byte order.
- Treating Data.portnum 0 as valid.
- Forgetting `hop_start` (packets with hop_start 0 are treated as legacy).
- Sending legacy text DMs to 2.7+ nodes.
- Not checking the payload limits above.

### Gaps
- Air-time and duty-cycle (channel-utilization) throttling of telemetry/position broadcasts was not covered.
