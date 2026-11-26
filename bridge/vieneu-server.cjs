// VieNeu v3u TTS as a tiny local HTTP service for the stick bridge.
// Drives the bundled native worker (same one AICW uses) over worker_threads.
//   POST http://127.0.0.1:8766/tts  {"text":"...","voice":"Ngọc Huyền"}  -> audio/L16 PCM16 mono 16 kHz
// Run: NODE_PATH=<v3u>/node_modules V3U_ROOT=<v3u> V3U_PACK=<model pack> node vieneu-server.cjs
const { Worker } = require('node:worker_threads');
const http = require('node:http');
const { join } = require('node:path');

const ROOT = process.env.V3U_ROOT;
const PACK = process.env.V3U_PACK;
const RES = join(ROOT, 'res');
const PORT = +(process.env.V3U_PORT || 8766);
const DEFAULT_VOICE = process.env.V3U_VOICE || 'Ngọc Huyền';

const files = {
  dict: join(RES, 'sea_g2p.bin'),
  voices: join(RES, 'voices_v3u_turbo.json'),
  config: join(PACK, 'config.json'),
  tokenizer: join(PACK, 'tokenizer.json'),
  tokenizerConfig: join(PACK, 'tokenizer_config.json'),
  prefill: join(PACK, 'vieneu_prefill.onnx'),
  decodeStep: join(PACK, 'vieneu_decode_step.onnx'),
  acoustic: join(PACK, 'vieneu_acoustic_cached.onnx'),
  headsNpz: join(PACK, 'vieneu_v3_heads.npz'),
  codecDecode: join(PACK, 'moss_decode_full.onnx'),
  codecDecodeData: join(PACK, 'moss_audio_tokenizer_decode_shared.data'),
  backboneData: join(PACK, 'vieneu_backbone_shared.data'),
};

const w = new Worker(join(ROOT, 'tts.worker.js'));
const waiters = new Set();
w.on('message', (m) => { for (const f of [...waiters]) f(m); });
w.on('error', (e) => { console.error('worker error', e); process.exit(1); });
let ready = false;
let queue = Promise.resolve();
let seq = 0;

function to16k(chunks, sr) {
  const n = chunks.reduce((a, c) => a + c.length, 0);
  const all = new Float32Array(n); let o = 0; for (const c of chunks) { all.set(c, o); o += c.length; }
  const ratio = sr / 16000; const outN = Math.floor(n / ratio);
  const out = Buffer.alloc(outN * 2);
  for (let i = 0; i < outN; i++) {
    const a = Math.floor(i * ratio), b = Math.max(a + 1, Math.floor((i + 1) * ratio));
    let s = 0; for (let k = a; k < b && k < n; k++) s += all[k];
    const v = Math.max(-1, Math.min(1, s / (b - a)));
    out.writeInt16LE(Math.round(v * 32767), i * 2);
  }
  return out;
}

function speak(text, voice) {
  const id = `t${++seq}`;
  return new Promise((resolve, reject) => {
    const chunks = []; let sr = 48000;
    const f = (m) => {
      if (m.id !== id) return;
      if (m.type === 'chunk') { chunks.push(new Float32Array(m.pcm)); sr = m.sampleRate; }
      else if (m.type === 'done') { waiters.delete(f); resolve(to16k(chunks, sr)); }
      else if (m.type === 'error' || m.type === 'stopped') { waiters.delete(f); reject(new Error(m.message || m.type)); }
    };
    waiters.add(f);
    w.postMessage({ type: 'speak', id, text, voice, options: {} });
  });
}

const initDone = new Promise((resolve, reject) => {
  const f = (m) => {
    if (m.type === 'ready') { waiters.delete(f); ready = true; console.log('vieneu ready, voices:', (m.voices || []).length, 'eps:', m.eps); resolve(); }
    else if (m.type === 'error' && !m.id) { waiters.delete(f); reject(new Error(m.message)); }
  };
  waiters.add(f);
  w.postMessage({ type: 'init', files, tryCoreml: false, arch: 'v3u' });
});

http.createServer((req, res) => {
  if (req.method === 'GET') { res.end(JSON.stringify({ ready, voice: DEFAULT_VOICE })); return; }
  let body = '';
  req.on('data', (c) => { body += c; });
  req.on('end', () => {
    let p; try { p = JSON.parse(body); } catch { res.statusCode = 400; res.end('bad json'); return; }
    const t0 = Date.now();
    const job = queue.then(() => initDone).then(() => speak(String(p.text || ''), p.voice || DEFAULT_VOICE));
    queue = job.catch(() => {});
    job.then((pcm) => {
      console.log(`tts ${pcm.length / 32000}s in ${Date.now() - t0}ms: ${String(p.text).slice(0, 60)}`);
      res.setHeader('Content-Type', 'audio/L16;rate=16000'); res.end(pcm);
    }, (e) => { res.statusCode = 500; res.end(String(e.message || e)); });
  });
}).listen(PORT, '127.0.0.1', () => console.log(`vieneu tts on http://127.0.0.1:${PORT}/tts`));
