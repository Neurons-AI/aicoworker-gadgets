// Builds site/dist from site/index.template.html + the app's boards.json.
// Usage: node site/build.mjs   (then: npx wrangler@4 pages deploy site/dist --project-name aicw-gadgets)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const boardsFile = process.env.BOARDS || path.join(process.env.HOME, 'aicoworker/aicwgw/workspace-main/skills/aicw-gadgets-dev/assets/boards.json');
const raw = JSON.parse(fs.readFileSync(boardsFile, 'utf8'));
const boards = Array.isArray(raw) ? raw : raw.boards;
if (!Array.isArray(boards) || !boards.length) throw new Error('no boards in ' + boardsFile);

// What each board looks like to a buyer. Only boards with a released firmware ("ready" = tested
// on real hardware, "build" = firmware ships, not yet tested by us) are listed as installable.
const info = {
  'm5stack-sticks3': { vi: 'Bỏ túi, màn hình màu, nút bấm phía trước. Bản chúng tôi test hằng ngày.', en: 'Pocket-size, colour screen, front button. The one we test every day.', buy: 'https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit' },
  'aipi': { vi: 'Bạn nhỏ để bàn: bấm nút để nói, trả lời thành tiếng, có màn hình màu.', en: 'A tiny desk buddy: press to talk, answers out loud, colour screen.', buy: 'https://aipi.com/products/aipi-lite' },
  'waveshare-s3-175c': { vi: 'Màn AMOLED tròn 1.75", có pin, loa và mic.', en: 'Round 1.75" AMOLED touch screen with battery, speaker and mic.', buy: 'https://www.waveshare.com/esp32-s3-touch-amoled-1.75c.htm' },
  'waveshare-s3-175': { vi: 'Màn AMOLED tròn 1.75" cảm ứng.', en: 'Round 1.75" AMOLED touch screen.', buy: 'https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm' },
  'waveshare-s3-185b': { vi: 'Màn LCD tròn 1.85" cảm ứng, loa và mic.', en: 'Round 1.85" LCD touch screen, speaker and mic.', buy: 'https://www.waveshare.com/esp32-s3-touch-lcd-1.85b.htm' },
  'waveshare-s3-185c': { vi: 'Màn LCD tròn 1.85" cảm ứng.', en: 'Round 1.85" LCD touch screen.', buy: 'https://www.waveshare.com/esp32-s3-touch-lcd-1.85c.htm' },
  'waveshare-s3-216': { vi: 'Màn AMOLED 2.16" cảm ứng.', en: '2.16" AMOLED touch screen.', buy: 'https://www.waveshare.com/' },
  'espressif-box-3': { vi: 'Bộ ESP32-S3-BOX-3 của Espressif: màn hình, loa, mic.', en: "Espressif's ESP32-S3-BOX-3: screen, speaker, mics.", buy: 'https://www.espressif.com/en/news/ESP32-S3-BOX-3' },
  'm5stack-cores3': { vi: 'Khối vuông M5Stack, màn cảm ứng, camera.', en: 'The M5Stack cube with touch screen and camera.', buy: 'https://shop.m5stack.com/products/m5stack-cores3-esp32s3-lotdevelopment-kit' },
  'm5stack-core2': { vi: 'M5Stack Core2: màn cảm ứng, pin, loa.', en: 'M5Stack Core2: touch screen, battery, speaker.', buy: 'https://shop.m5stack.com/products/m5stack-core2-esp32-iot-development-kit' },
  'm5stack-atoms3': { vi: 'AtomS3 nhỏ xíu + đế Atomic Voice (thử nghiệm).', en: 'Tiny AtomS3 on the Atomic Voice base (experimental).', buy: 'https://shop.m5stack.com/' },
  'm5stack-stickc-plus2': { vi: 'StickC Plus2 bỏ túi, màn nhỏ, pin.', en: 'Pocket StickC Plus2 with small screen and battery.', buy: 'https://shop.m5stack.com/products/m5stickc-plus2-esp32-mini-iot-development-kit' },
  'm5stack-stopwatch': { vi: 'M5Stack StopWatch hình đồng hồ.', en: 'M5Stack StopWatch, a watch-shaped gadget.', buy: 'https://shop.m5stack.com/' },
  'seeed-xiao-s3-147': { vi: 'XIAO ESP32-S3 kèm màn cảm ứng 1.47".', en: 'XIAO ESP32-S3 with a 1.47" touch display.', buy: 'https://www.seeedstudio.com/' },
  'sensecap-watcher': { vi: 'SenseCAP Watcher: màn tròn, camera, mic, loa.', en: 'SenseCAP Watcher: round screen, camera, mic, speaker.', buy: 'https://www.seeedstudio.com/watcher' },
  'reterminal-sticky': { vi: 'reTerminal Sticky, màn e-paper 3.97" (thử nghiệm).', en: 'reTerminal Sticky, 3.97" e-paper (experimental).', buy: 'https://www.seeedstudio.com/' },
};
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
const order = { ready: 0, build: 1, port: 2 };
const list = boards.filter((b) => b.aicw === 'ready' || b.aicw === 'build').sort((a, b) => order[a.aicw] - order[b.aicw] || a.name.localeCompare(b.name));
const soon = boards.filter((b) => b.aicw === 'port');
const cards = list.map((b) => {
  const i = info[b.id] || {};
  const ready = b.aicw === 'ready';
  return `      <article class="dev${ready ? ' ready' : ''}">
        <div class="dev-top"><span class="chip">${esc(b.chip || 'esp32')}</span>${ready ? '<span class="badge ok" data-i18n="badge_tested"></span>' : '<span class="badge" data-i18n="badge_fw"></span>'}</div>
        <h3>${esc(b.name)}</h3>
        <p><span lang="vi">${esc(i.vi || 'Firmware AICW Gadget đã có sẵn.')}</span><span lang="en">${esc(i.en || 'AICW Gadget firmware is available.')}</span></p>
        <div class="dev-links">${i.buy ? `<a href="${esc(i.buy)}" target="_blank" rel="noopener" data-i18n="buy"></a>` : ''}<a href="https://github.com/Neurons-AI/aicoworker-gadgets/tree/main/esp32/devices" target="_blank" rel="noopener" data-i18n="source"></a></div>
      </article>`;
}).join('\n');
const soonList = soon.map((b) => `<li>${esc(b.name)}</li>`).join('');

const tpl = fs.readFileSync(path.join(here, 'index.template.html'), 'utf8');
const html = tpl.replace('<!--DEVICES-->', cards).replace('<!--SOON-->', soonList).replace(/<!--COUNT-->/g, String(list.length));
const dist = path.join(here, 'dist');
fs.rmSync(dist, { recursive: true, force: true });
fs.cpSync(path.join(here, 'public'), dist, { recursive: true });
fs.writeFileSync(path.join(dist, 'index.html'), html);
fs.writeFileSync(path.join(dist, '_headers'), '/*\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: strict-origin-when-cross-origin\n/img/*\n  Cache-Control: public, max-age=604800\n');
console.log(`built ${list.length} devices (+${soon.length} coming) -> ${dist}`);
