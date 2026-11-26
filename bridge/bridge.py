"""AICW Stick bridge: WebSocket server for M5StickS3 (or the virtual stick).

stick --PCM16 16k--> bridge --STT (faster-whisper)--> your agent (AICW sharedwebchat account, uid aicw-stick)
      <--PCM16 16k-- bridge <--TTS (edge-tts)-------- reply text
Protocol: ../PROTOCOL.md
"""
import asyncio, json, os, re, subprocess, sys, time, logging, tempfile

import numpy as np
import requests
import websockets
import aicw_relay
import edge_tts
import imageio_ffmpeg

HOST = os.environ.get("AICW_BRIDGE_HOST", "0.0.0.0")
PORT = int(os.environ.get("AICW_BRIDGE_PORT", "8765"))
CHAT_BASE = os.environ.get("AICW_CHAT_BASE", "http://127.0.0.1:23336/webchat/ani")
CHAT_UID = os.environ.get("AICW_CHAT_UID", "aicw-stick")
AGENT_NAME = os.environ.get("AICW_AGENT_NAME", "Dou")
# Comma-separated Wi-Fi MACs of your sticks. Anyone who knows the relay hostId can
# open a session; set this so only your own devices reach the agent.
ALLOWED_MACS = {m.strip().lower() for m in os.environ.get("AICW_ALLOWED_MACS", "").split(",") if m.strip()}
VOICE = os.environ.get("AICW_TTS_VOICE", "vi-VN-HoaiMyNeural")
WHISPER_MODEL = os.environ.get("AICW_WHISPER", "large-v3-turbo")
RATE = 16000
PREFIX = ("[Tin nhắn thoại từ con M5StickS3 của anh. Câu trả lời sẽ được đọc to qua loa nhỏ: "
          "trả lời ngắn gọn 1-3 câu, văn nói tự nhiên, không markdown, không emoji, không liệt kê dài, không đưa link.]\n")

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s",
                    handlers=[logging.StreamHandler(sys.stdout),
                              logging.FileHandler(os.path.join(os.path.dirname(__file__), "bridge.log"), encoding="utf-8")])
log = logging.getLogger("bridge")
FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()

# ---- STT ----
_whisper = None
def _add_cuda_dlls():
    import site
    for sp in site.getsitepackages():
        for sub in ("nvidia/cublas/bin", "nvidia/cudnn/bin"):
            p = os.path.join(sp, sub)
            if os.path.isdir(p):
                os.add_dll_directory(p)
                os.environ["PATH"] = p + os.pathsep + os.environ["PATH"]

def whisper():
    global _whisper
    if _whisper is None:
        from faster_whisper import WhisperModel
        try:
            _add_cuda_dlls()
            _whisper = WhisperModel(WHISPER_MODEL, device="cuda", compute_type="float16")
            log.info("whisper %s on cuda", WHISPER_MODEL)
        except Exception as e:
            log.warning("cuda whisper failed (%s), using cpu small", e)
            _whisper = WhisperModel("small", device="cpu", compute_type="int8")
    return _whisper

def transcribe(pcm: bytes) -> str:
    audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
    if len(audio) < RATE * 0.3:
        return ""
    segs, _ = whisper().transcribe(audio, language="vi", beam_size=5, vad_filter=True)
    return " ".join(s.text.strip() for s in segs).strip()

# ---- Ani ----
_token = None
def ask_ani(text: str) -> str:
    global _token
    for attempt in range(2):
        if not _token:
            r = requests.post(f"{CHAT_BASE}/auth", json={"uid": CHAT_UID, "username": os.environ.get("AICW_CHAT_NAME", "AICW Stick")}, timeout=15)
            _token = r.json()["token"]
        r = requests.post(f"{CHAT_BASE}/message", params={"token": _token},
                          json={"message": PREFIX + text}, timeout=190)
        j = r.json()
        if j.get("ok"):
            return j.get("reply") or ""
        if attempt == 0 and r.status_code in (401, 403):
            _token = None
            continue
        raise RuntimeError(j.get("error") or f"http {r.status_code}")
    raise RuntimeError("auth failed")

def speakable(text: str) -> str:
    t = re.sub(r"```.*?```", " ", text, flags=re.S)
    t = re.sub(r"https?://\S+", " ", t)
    t = re.sub(r"[*_`#>|\[\]]", " ", t)
    t = re.sub(r"[\U0001F000-\U0001FAFF\u2600-\u27BF\uFE0F]", "", t)
    return re.sub(r"\s+", " ", t).strip()

# ---- TTS: VieNeu (vieneu-server.cjs), Edge only as fallback ----
VIENEU_URL = os.environ.get("AICW_VIENEU_URL", "http://127.0.0.1:8766/tts")
VIENEU_VOICE = os.environ.get("AICW_VIENEU_VOICE", "Ngọc Huyền")

async def tts_pcm(text: str) -> bytes:
    try:
        r = await asyncio.to_thread(requests.post, VIENEU_URL, json={"text": text, "voice": VIENEU_VOICE}, timeout=120)
        if r.ok and r.content:
            return r.content
        log.warning("vieneu failed: %s %s", r.status_code, r.text[:200])
    except Exception as e:
        log.warning("vieneu unreachable: %s", e)
    return await tts_edge(text)

async def tts_edge(text: str) -> bytes:
    mp3 = b""
    async for chunk in edge_tts.Communicate(text, VOICE, rate="+8%").stream():
        if chunk["type"] == "audio":
            mp3 += chunk["data"]
    p = await asyncio.create_subprocess_exec(FFMPEG, "-loglevel", "error", "-i", "pipe:0", "-f", "s16le",
                                             "-ac", "1", "-ar", str(RATE), "pipe:1",
                                             stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE)
    out, _ = await p.communicate(mp3)
    return out

# ---- one connection ----
async def handle(ws):
    peer = ws.remote_address
    log.info("connect %s", peer)
    cur = {"id": None, "pcm": bytearray(), "task": None}
    hello_ok = False

    async def send(obj):
        await ws.send(json.dumps(obj, ensure_ascii=False))

    async def run_turn(tid, text_in=None, pcm=None):
        t0 = time.time()
        try:
            await send({"type": "sent", "id": tid})
            if text_in is None:
                text_in = await asyncio.to_thread(transcribe, bytes(pcm))
                log.info("turn %s heard (%.1fs, %.1fs audio): %s", tid, time.time() - t0, len(pcm) / 2 / RATE, text_in)
                await send({"type": "heard", "id": tid, "text": text_in, "final": True})
                if not text_in:
                    await send({"type": "error", "id": tid, "text": "Không nghe rõ"})
                    return
            reply = await asyncio.to_thread(ask_ani, text_in)
            say = speakable(reply)
            log.info("turn %s reply (%.1fs): %s", tid, time.time() - t0, reply)
            await send({"type": "reply", "id": tid, "text": say or reply})
            if say:
                pcm_out = await tts_pcm(say)
                frames = len(pcm_out) // 2
                await send({"type": "audio_start", "id": tid, "rate": RATE, "frames": frames})
                CH = 4096
                for i in range(0, len(pcm_out), CH):
                    if cur["id"] != tid:
                        return
                    await ws.send(pcm_out[i:i + CH])
                    await asyncio.sleep(CH / 2 / RATE * 0.8)   # a bit faster than realtime
                await send({"type": "audio_end", "id": tid})
                log.info("turn %s audio %.1fs, total %.1fs", tid, frames / RATE, time.time() - t0)
            await send({"type": "done", "id": tid})
        except asyncio.CancelledError:
            raise
        except Exception as e:
            log.exception("turn %s failed", tid)
            try:
                await send({"type": "error", "id": tid, "text": str(e)[:80]})
            except Exception:
                pass

    def cancel_task():
        if cur["task"] and not cur["task"].done():
            cur["task"].cancel()

    try:
        async for msg in ws:
            if isinstance(msg, bytes):
                if cur["id"] is not None:
                    cur["pcm"] += msg
                continue
            m = json.loads(msg)
            t = m.get("type")
            if t == "hello":
                log.info("hello %s", m)
                if ALLOWED_MACS and str(m.get("mac", "")).lower() not in ALLOWED_MACS:
                    log.warning("refused device %s (not in AICW_ALLOWED_MACS)", m.get("mac"))
                    await send({"type": "error", "text": "device not allowed"})
                    return
                hello_ok = True
                await send({"type": "ready", "name": AGENT_NAME})
            elif not hello_ok:
                return
            elif t == "ping":
                await send({"type": "pong"})
            elif t == "turn_begin":
                cancel_task()
                cur["id"], cur["pcm"] = m["id"], bytearray()
            elif t == "turn_end" and m.get("id") == cur["id"]:
                cur["task"] = asyncio.create_task(run_turn(cur["id"], pcm=cur["pcm"]))
            elif t == "cancel":
                if m.get("id") == cur["id"]:
                    cancel_task()
                    cur["id"] = None
            elif t == "text_turn":
                cancel_task()
                cur["id"] = m["id"]
                cur["task"] = asyncio.create_task(run_turn(m["id"], text_in=m.get("text", "")))
    except (websockets.ConnectionClosed, aicw_relay.Closed):
        pass
    finally:
        cancel_task()
        log.info("disconnect %s", peer)

async def main():
    await asyncio.to_thread(whisper)   # load the model before the first turn
    idn = aicw_relay.Identity.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "relay-identity.json"))
    async with websockets.serve(handle, HOST, PORT, max_size=8 * 1024 * 1024, ping_interval=None):
        log.info("AICW bridge on ws://%s:%d/stick -> %s ; relay hostId %s", HOST, PORT, CHAT_BASE, idn.id)
        await aicw_relay.run_host(idn, handle)   # same stick protocol over the AICW relay

if __name__ == "__main__":
    asyncio.run(main())
