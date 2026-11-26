# AICW Stick protocol (firmware <-> aicw-bridge)

Transport: plain WebSocket (no TLS) over LAN. URL `ws://<host>:<port>/stick` (default port 8765).
Host:port stored in NVS on the stick, set over the serial console: `>aicw_host=192.168.1.20:8765` (answers `@aicw_host ok`).

All text frames are JSON. Binary frames are audio: PCM16 little-endian, mono, 16000 Hz.
Every turn has an integer `id` chosen by the stick (increasing). Messages for an id other than the current turn are dropped.

## Stick -> bridge
| frame | meaning |
|---|---|
| `{"type":"hello","device":"m5stack-sticks3","mac":"14c19fd59798","fw":"aicw-0.1"}` | sent on every connect |
| `{"type":"turn_begin","id":N,"rate":16000}` | button pressed |
| binary | mic audio of the current turn, in order |
| `{"type":"turn_end","id":N}` | button released, no more audio |
| `{"type":"cancel","id":N}` | turn abandoned (tap / barge-in) |
| `{"type":"text_turn","id":N,"text":"..."}` | typed turn from serial console |
| `{"type":"ping"}` | keepalive every 20 s |

## Bridge -> stick
| frame | meaning |
|---|---|
| `{"type":"ready","name":"Ani"}` | reply to hello |
| `{"type":"heard","id":N,"text":"...","final":true}` | transcript (maps to DOU_HATCH_EV_HEARD) |
| `{"type":"sent","id":N}` | bridge accepted the turn (DOU_HATCH_EV_SENT) |
| `{"type":"reply","id":N,"text":"<full reply text so far>"}` | reply text (DOU_HATCH_EV_REPLY, caption source) |
| `{"type":"audio_start","id":N,"rate":16000,"frames":F}` | reply speech follows; F = total frames if known, else 0 |
| binary | reply speech PCM16 16 kHz mono for the current turn |
| `{"type":"audio_end","id":N}` | no more speech for this turn |
| `{"type":"done","id":N}` | turn complete (DOU_HATCH_EV_DONE, empty text) |
| `{"type":"error","id":N,"text":"why"}` | turn failed (DOU_HATCH_EV_ERROR) |
| `{"type":"pong"}` | keepalive answer |

Order for a normal turn: sent -> heard(final) -> reply -> audio_start -> binary... -> audio_end -> done.
Bridge may send several `reply` frames (growing text) before audio.
Timeouts on the stick: no `heard` within 20 s of turn_end, or no `done` within 90 s -> error "timeout".
