"""Virtual M5StickS3: speaks PROTOCOL.md to the bridge using the PC mic + speakers.

  python fake_stick.py                 interactive: Enter = start talking, Enter = stop
  python fake_stick.py --say "câu"     test: synthesize the sentence with edge-tts and send it as mic audio
  python fake_stick.py --text "câu"    typed turn (no STT)
  --url ws://host:8765/stick   --out reply.wav (save the reply speech)   --mute (do not play)
"""
import argparse, asyncio, json, sys, time, wave

import numpy as np
import websockets

RATE = 16000

async def synth(text):
    import requests
    try:   # local VieNeu first (male voice for "anh"), Edge as fallback
        r = await asyncio.to_thread(requests.post, "http://127.0.0.1:8766/tts", json={"text": text, "voice": "Bình An"}, timeout=120)
        if r.ok and r.content:
            return r.content
    except Exception:
        pass
    import edge_tts, imageio_ffmpeg
    mp3 = b""
    async for c in edge_tts.Communicate(text, "vi-VN-NamMinhNeural").stream():
        if c["type"] == "audio":
            mp3 += c["data"]
    p = await asyncio.create_subprocess_exec(imageio_ffmpeg.get_ffmpeg_exe(), "-loglevel", "error", "-i", "pipe:0",
                                             "-f", "s16le", "-ac", "1", "-ar", str(RATE), "pipe:1",
                                             stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE)
    out, _ = await p.communicate(mp3)
    return out

def record_until_enter():
    import sounddevice as sd
    chunks = []
    with sd.InputStream(samplerate=RATE, channels=1, dtype="int16", callback=lambda d, *_: chunks.append(d.copy())):
        input("  🎙  đang nghe... Enter để gửi ")
    return np.concatenate(chunks).tobytes() if chunks else b""

async def turn(ws, tid, pcm=None, text=None, out=None, mute=False):
    t0 = time.time()
    if text is not None:
        await ws.send(json.dumps({"type": "text_turn", "id": tid, "text": text}))
    else:
        await ws.send(json.dumps({"type": "turn_begin", "id": tid, "rate": RATE}))
        for i in range(0, len(pcm), 3200):          # 100 ms frames, like the stick
            await ws.send(pcm[i:i + 3200])
        await ws.send(json.dumps({"type": "turn_end", "id": tid}))
    speech = bytearray()
    result = {"heard": None, "reply": None, "ok": False, "error": None}
    while True:
        msg = await asyncio.wait_for(ws.recv(), timeout=200)
        if isinstance(msg, bytes):
            speech += msg
            continue
        m = json.loads(msg)
        if m.get("id") not in (None, tid):
            continue
        t = m["type"]
        dt = time.time() - t0
        if t == "heard":
            result["heard"] = m["text"]; print(f"  [{dt:5.1f}s] 👂 {m['text']}")
        elif t == "reply":
            result["reply"] = m["text"]; print(f"  [{dt:5.1f}s] 💬 {m['text']}")
        elif t == "audio_start":
            print(f"  [{dt:5.1f}s] 🔊 audio {m.get('frames', 0) / RATE:.1f}s")
        elif t == "error":
            result["error"] = m["text"]; print(f"  [{dt:5.1f}s] ❌ {m['text']}"); break
        elif t == "done":
            result["ok"] = True; print(f"  [{dt:5.1f}s] ✅ done, {len(speech) / 2 / RATE:.1f}s speech"); break
    if out and speech:
        with wave.open(out, "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(bytes(speech))
    if speech and not mute:
        import sounddevice as sd
        sd.play(np.frombuffer(bytes(speech), dtype=np.int16), RATE); sd.wait()
    result["speech_s"] = len(speech) / 2 / RATE
    return result

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="ws://127.0.0.1:8765/stick")
    ap.add_argument("--say"); ap.add_argument("--text"); ap.add_argument("--out"); ap.add_argument("--mute", action="store_true")
    ap.add_argument("--pcm", help="raw PCM16 16 kHz mono file to send as the voice turn")
    ap.add_argument("--relay", help="hostId: go through wss://agent.aicoworker.net instead of the LAN")
    a = ap.parse_args()
    if a.relay:
        import aicw_relay
        ws = await aicw_relay.connect_guest(a.relay)
        print("relay E2EE session up")
        await body(ws, a)
        return
    async with websockets.connect(a.url, max_size=None) as ws:
        await body(ws, a)

async def body(ws, a):
    if True:
        await ws.send(json.dumps({"type": "hello", "device": "fake-stick", "mac": "000000000000", "fw": "sim"}))
        print("bridge:", json.loads(await ws.recv()))
        if a.say or a.text or getattr(a, "pcm", None):
            pcm = open(a.pcm, "rb").read() if getattr(a, "pcm", None) else (await synth(a.say) if a.say else None)
            r = await turn(ws, 1, pcm=pcm, text=a.text, out=a.out, mute=a.mute)
            print("RESULT", json.dumps(r, ensure_ascii=False))
            sys.exit(0 if r["ok"] else 1)
        tid = 0
        while True:
            input("Enter để nói (Ctrl+C thoát) ")
            tid += 1
            pcm = await asyncio.to_thread(record_until_enter)
            await turn(ws, tid, pcm=pcm, out=a.out, mute=a.mute)

if __name__ == "__main__":
    asyncio.run(main())
