"""AICW relay link (agent.aicoworker.net) for the AICW Stick.

Same wire format as AICoworker's sharedwebchat relay (relay-protocol.ts / e2ee.ts):
  host  : wss://<relay>/h        guest: wss://<relay>/g/<hostId>
  hello(0x01)+challenge -> auth(0x02) signed with Ed25519 -> authOK(0x03)
  guest -> host  : 0x10 + inner          host receives 0x11 + guestPub(32) + inner
  host  -> guest : 0x12 + guestPub + inner, guest receives 0x13 + inner
  inner = E2EE handshake (0x01/0x02/0x04) then 0x03 + nonce(12) + AES-GCM(ct)
The relay only sees ciphertext. Inside the session each plaintext is
  0x00 + JSON text   or   0x01 + binary (PCM)   -- the PROTOCOL.md frames.
"""
import asyncio, base64, hashlib, json, logging, os
import websockets
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives import hashes, serialization

log = logging.getLogger("relay")
RELAY = os.environ.get("AICW_RELAY", "wss://agent.aicoworker.net")
F_HELLO, F_AUTH, F_AUTHOK, F_AUTHERR = 1, 2, 3, 4
F_TOHOST, F_RECV, F_TOGUEST, F_FROMHOST = 0x10, 0x11, 0x12, 0x13
F_GUESTGONE, F_HOSTGONE, F_PING, F_PONG = 0x21, 0x22, 0x40, 0x41
ROLE_HOST, ROLE_GUEST = 1, 2
CI, CR, INFO, RA = b"aicw-e2ee-v1/init", b"aicw-e2ee-v1/resp", b"aicw-e2ee-v1", b"aicw-relay-auth-v1"
IH_INIT, IH_RESP, IH_TRANSPORT, IH_ERR = 1, 2, 3, 4

def b32e(b): return base64.b32encode(b).decode().rstrip("=").lower()
def b32d(s): s = s.upper(); return base64.b32decode(s + "=" * (-len(s) % 8))
def sha(b): return hashlib.sha256(b).digest()
def raw(pub): return pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)

class Identity:
    def __init__(self, seed: bytes):
        self.seed = seed
        self.priv = Ed25519PrivateKey.from_private_bytes(seed)
        self.pub = raw(self.priv.public_key())
        self.id = b32e(self.pub)
    def sign(self, m): return self.priv.sign(m)
    @staticmethod
    def load(path):
        if os.path.exists(path):
            return Identity(bytes.fromhex(json.load(open(path))["seed"]))
        seed = os.urandom(32)
        idn = Identity(seed)
        json.dump({"seed": seed.hex(), "id": idn.id}, open(path, "w"))
        return idn

def verify(pub, sig, m):
    try: Ed25519PublicKey.from_public_bytes(pub).verify(sig, m); return True
    except Exception: return False

def relay_auth(idn, host, challenge, role, target):
    return idn.sign(sha(RA + b"\0" + host.encode() + b"\0" + challenge + bytes([role]) + idn.pub + target))

class Session:
    def __init__(self, ss, guest_eph, host_eph, role):
        okm = HKDF(hashes.SHA256(), 64, guest_eph + host_eph, INFO).derive(ss)
        g2h, h2g = AESGCM(okm[:32]), AESGCM(okm[32:])
        self.tx, self.rx, self.dir = (g2h, h2g, 0) if role == "guest" else (h2g, g2h, 1)
        self.sc = self.rc = 0
    def _n(self, c, d): return bytes([d]) + c.to_bytes(11, "big")
    def enc(self, pt):
        n = self._n(self.sc, self.dir); self.sc += 1
        return bytes([IH_TRANSPORT]) + n + self.tx.encrypt(n, pt, None)
    def dec(self, fr):
        if len(fr) < 29 or fr[0] != IH_TRANSPORT: raise ValueError("bad frame")
        n = fr[1:13]
        if n != self._n(self.rc, self.dir ^ 1): raise ValueError("out of order")
        pt = self.rx.decrypt(n, fr[13:], None); self.rc += 1
        return pt

def pack(msg): return (b"\x01" + msg) if isinstance(msg, (bytes, bytearray)) else (b"\x00" + msg.encode())
def unpack(pt): return bytes(pt[1:]) if pt[0] == 1 else pt[1:].decode()

class Closed(Exception): pass

class Channel:
    """websocket-like: await send(str|bytes), await recv(), async for, remote_address."""
    def __init__(self, sendraw, name):
        self._send, self.q, self.remote_address, self.closed = sendraw, asyncio.Queue(), name, False
    async def send(self, msg):
        if self.closed: raise Closed()
        await self._send(pack(msg))
    async def recv(self):
        m = await self.q.get()
        if m is None: raise Closed()
        return m
    def __aiter__(self): return self
    async def __anext__(self):
        try: return await self.recv()
        except Closed: raise StopAsyncIteration
    def _close(self):
        if not self.closed: self.closed = True; self.q.put_nowait(None)

async def run_host(idn: Identity, on_guest):
    """Keep a relay host registration alive; on_guest(channel) is spawned per guest."""
    host = RELAY.split("://", 1)[1].split("/")[0]
    delay = 1
    while True:
        guests = {}
        try:
            async with websockets.connect(RELAY.rstrip("/") + "/h", max_size=None, ping_interval=None) as ws:
                lock = asyncio.Lock()
                async def wsend(b):
                    async with lock: await ws.send(b)
                async for data in ws:
                    t, body = data[0], data[1:]
                    if t == F_HELLO:
                        sig = relay_auth(idn, host, body[1:33], ROLE_HOST, bytes(32))
                        await wsend(bytes([F_AUTH, ROLE_HOST]) + idn.pub + bytes(32) + sig)
                    elif t == F_AUTHOK:
                        delay = 1
                        log.info("relay host up: hostId=%s", idn.id)
                    elif t == F_AUTHERR:
                        log.error("relay auth rejected: %s", body.decode(errors="replace")); break
                    elif t == F_PING:
                        await wsend(bytes([F_PONG]) + body)
                    elif t == F_GUESTGONE:
                        g = guests.pop(bytes(body[:32]), None)
                        if g: g[1]._close()
                    elif t == F_RECV and len(body) > 32:
                        gp, inner = bytes(body[:32]), body[32:]
                        if inner[0] == IH_INIT:
                            if len(inner) != 130 or inner[2:34] != gp: continue
                            geph = inner[34:66]
                            if not verify(gp, inner[66:130], sha(CI + idn.pub + gp + geph)): continue
                            eph = X25519PrivateKey.generate(); ephp = raw(eph.public_key())
                            sig = idn.sign(sha(CR + idn.pub + gp + geph + ephp))
                            sess = Session(eph.exchange(X25519PublicKey.from_public_bytes(geph)), geph, ephp, "host")
                            if gp in guests: guests[gp][1]._close()
                            async def send_to(pt, gp=gp, sess=sess):
                                await wsend(bytes([F_TOGUEST]) + gp + sess.enc(pt))
                            ch = Channel(send_to, "relay:" + b32e(gp)[:8])
                            guests[gp] = (sess, ch)
                            await wsend(bytes([F_TOGUEST]) + gp + bytes([IH_RESP, 1]) + ephp + sig)
                            log.info("relay guest %s up", b32e(gp)[:8])
                            asyncio.create_task(on_guest(ch))
                        elif gp in guests:
                            sess, ch = guests[gp]
                            try: ch.q.put_nowait(unpack(sess.dec(inner)))
                            except Exception as e: log.warning("guest %s decrypt: %s", b32e(gp)[:8], e)
        except Exception as e:
            log.warning("relay host link: %s", e)
        for _, ch in guests.values(): ch._close()
        await asyncio.sleep(delay); delay = min(delay * 2, 60)

async def connect_guest(host_id: str, idn: Identity = None):
    """Guest side (fake stick). Returns a Channel once the E2EE session is up."""
    idn = idn or Identity(os.urandom(32))
    hpub, host = b32d(host_id), RELAY.split("://", 1)[1].split("/")[0]
    ws = await websockets.connect(f"{RELAY.rstrip('/')}/g/{host_id}", max_size=None, ping_interval=None)
    eph = X25519PrivateKey.generate(); ephp = raw(eph.public_key())
    init = bytes([IH_INIT, 1]) + idn.pub + ephp + idn.sign(sha(CI + hpub + idn.pub + ephp))
    sess = None
    while sess is None:
        data = await ws.recv(); t, body = data[0], data[1:]
        if t == F_HELLO:
            await ws.send(bytes([F_AUTH, ROLE_GUEST]) + idn.pub + hpub + relay_auth(idn, host, body[1:33], ROLE_GUEST, hpub))
        elif t == F_AUTHOK:
            await ws.send(bytes([F_TOHOST]) + init)
        elif t in (F_AUTHERR, F_HOSTGONE):
            raise RuntimeError("relay: " + (body.decode(errors="replace") or "host offline"))
        elif t == F_PING:
            await ws.send(bytes([F_PONG]) + body)
        elif t == F_FROMHOST:
            if body[0] == IH_ERR: raise RuntimeError("host rejected: " + body[1:].decode(errors="replace"))
            hephp, hsig = body[2:34], body[34:98]
            if not verify(hpub, hsig, sha(CR + hpub + idn.pub + ephp + hephp)): raise RuntimeError("host signature invalid")
            sess = Session(eph.exchange(X25519PublicKey.from_public_bytes(hephp)), ephp, hephp, "guest")
    ch = Channel(lambda pt: ws.send(bytes([F_TOHOST]) + sess.enc(pt)), "relay")
    async def pump():
        try:
            async for data in ws:
                t, body = data[0], data[1:]
                if t == F_FROMHOST: ch.q.put_nowait(unpack(sess.dec(body)))
                elif t == F_PING: await ws.send(bytes([F_PONG]) + body)
                elif t == F_HOSTGONE: break
        finally:
            ch._close()
    ch._pump = asyncio.create_task(pump()); ch._ws = ws
    return ch
