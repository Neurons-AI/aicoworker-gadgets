# Research notes: an AICoworker voice gadget on a $20 board

What we wanted: hold a button on a tiny board, talk, and hear your own
AICoworker agent answer, from anywhere, not only at home.

What we started from: the Muse Gadget SDK by Meta, a mature ESP32 firmware for
voice gadgets (display, mic, speaker, push-to-talk, avatar, Wi-Fi setup,
dozens of boards). We kept the device side and replaced the cloud side.

## 1. Architecture

```
 StickS3 ──hold to talk──▶ PCM16 16 kHz
    │  LAN:   ws://<pc>:8765/stick
    │  WAN:   wss://agent.aicoworker.net/g/<hostId>  (E2EE inside)
    ▼
 bridge/ (Python, on the PC that runs AICoworker)
    ├─ STT   faster-whisper large-v3-turbo (CUDA, falls back to CPU small)
    ├─ chat  AICoworker sharedwebchat  POST /webchat/<account>/message
    └─ TTS   VieNeu (local, bridge/vieneu-server.cjs) or Edge TTS
    ▼
 PCM16 back to the stick, played while the reply caption scrolls
```

The device protocol is small JSON plus binary audio frames, see
[PROTOCOL.md](PROTOCOL.md).

## 2. Reaching the PC from anywhere: the AICW relay

The board is often on another network (phone hotspot, office Wi-Fi). Instead
of port forwarding or a third-party tunnel we reuse the relay AICoworker
already runs for shared web chats.

- The bridge registers as a **relay host** (`wss://agent.aicoworker.net/h`)
  with its own Ed25519 identity (`bridge/relay-identity.json`, created on first
  run, never commit it). Its public key in base32 is the **hostId**.
- The stick connects as a **guest** to `/g/<hostId>`, answers the relay's
  challenge with its own Ed25519 key, then runs the same end-to-end handshake
  as the AICoworker web app: X25519 ephemeral keys, both sides sign the
  transcript, HKDF-SHA256 (`aicw-e2ee-v1`) gives two AES-256-GCM keys, nonces are
  direction byte + counter.
- The relay only forwards ciphertext. Inside the session every plaintext is
  `0x00 + JSON` or `0x01 + PCM`, the same frames as on the LAN.

Wire format, from AICoworker's `relay-protocol.ts` and `e2ee.ts`:

| frame | meaning |
|---|---|
| `0x01` hello + 32-byte challenge | relay → peer |
| `0x02` auth role, idPub, target, sig | peer → relay |
| `0x03` / `0x04` | auth ok / refused |
| `0x10` + inner | guest → host |
| `0x11` + guestPub + inner | relay → host |
| `0x12` + guestPub + inner | host → guest |
| `0x13` + inner | relay → guest |
| `0x21` / `0x22` | guest gone / host gone |
| `0x40` / `0x41` | ping / pong |

Relay auth signs `sha256("aicw-relay-auth-v1" 0 host 0 challenge role idPub target)`.

On the ESP32-S3: TLS with the IDF certificate bundle, X25519 / AES-GCM /
HMAC-SHA256 from mbedTLS 4 (PSA API), Ed25519 from TweetNaCl (public domain).
A round trip (speech in, Vietnamese speech out) took about 15-20 s, most of it
the agent thinking; relay latency is not noticeable.

**Security note.** Anyone who knows a bridge's hostId can open a session to it.
Keep the hostId private and set `AICW_ALLOWED_MACS` on the bridge so only your
own sticks are answered.

## 3. What we changed in the firmware

| Area | Change |
|---|---|
| Transport | `components/dou/aicw_chat_session.c` replaces the Muse cloud session; `aicw_link.inc` holds LAN and relay links |
| Settings | `aicw_host` lives in its own NVS namespace (`aicw`), because the stock settings only accept the vendor's hosts |
| Console | `>wifi=<ssid>\t<pass>`, `>aicw_host=<ip:port \| relay:<hostId>>`, `>aicw_status`, `>chat=<text>` |
| Avatar | `avatar/dou_pixel.c`: Dou (round dark body, glowing scalloped rim, spiral eyes), 14 expressions over the 7 device states |
| Brand | `tools/rebrand.py` renames Muse → Dou and Meta → AICW everywhere except licence headers and real upstream service URLs |

## 4. Lessons

1. **After flashing, the S3 may sit in download mode** (`boot:0x23 DOWNLOAD`)
   and look dead on the serial console. Reset it with
   `esptool.py --after watchdog-reset read-mac`, not an RTS pulse.
2. **The stock settings silently reject a non-vendor host.** Keep your own
   config key instead of fighting the allow-list.
3. **Test the whole path with a fake device first.** `bridge/fake_stick.py`
   speaks the device protocol (LAN or `--relay <hostId>`) with synthetic
   speech, so the bridge and relay were verified before touching C.
4. **mbedTLS 4 is PSA-only.** There is no `mbedtls/gcm.h`; use
   `psa_aead_*`, `psa_mac_*`, `psa_hash_*`.
5. **TweetNaCl's `crypto_sign_keypair` is commented out** (it needs
   `randombytes`); enable it and feed the seed through `randombytes`.

## 5. Dou expressions

See [DOU-FACES.md](DOU-FACES.md) for the mascot's design and the mood table,
and `esp32/tools/dou/dou_sheet.c` to render every state to an image on your
computer without hardware.
