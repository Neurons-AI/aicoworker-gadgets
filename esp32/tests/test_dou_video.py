# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Video streaming on the host: the receive ring every transport shares
(components/dou_vstream) and the Wi-Fi control messages
(components/dou/dou_video_wire.c), in tests/dou_video_harness.c; and the
firmware's wiring under CONFIG_DOU_VIDEO."""

from __future__ import annotations

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

from host_cjson import cjson_source

ROOT = Path(__file__).resolve().parents[1]
DOU = ROOT / "components/dou"
VSTREAM = ROOT / "components/dou_vstream"
# CI supplies pinned upstream sources; local IDF builds already have cJSON.
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))


class DouVideoTest(unittest.TestCase):
    def test_ring_and_wire(self) -> None:
        if not (JSON / "cJSON.c").exists():
            self.skipTest("cJSON not found: run idf.py build once, or set CJSON_SOURCE_DIR")
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp, "video")
            build = subprocess.run(
                [*cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                 f"-I{VSTREAM / 'include'}", f"-I{DOU}", f"-I{JSON}", "-o", str(exe),
                 str(ROOT / "tests/dou_video_harness.c"), str(VSTREAM / "dou_vstream.c"),
                 str(DOU / "dou_video_wire.c"), *cjson_source(JSON), "-lm"],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS dou_video", run.stdout)

    def test_restart_while_the_consumer_pops(self) -> None:
        # The producer (a new start) can run while the consumer is between
        # checking a frame and consuming it. The ring source is built with its
        # barriers hooked, so the start lands exactly there.
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void race(void);
#define BARRIER() race()
#include "dou_vstream.c"

static dou_vstream_t s;
static int countdown;   /* barriers until the producer restarts the stream */

static void race(void)
{
    if (countdown > 0 && --countdown == 0) {
        dou_vstream_start(&s);
    }
}

static uint32_t frame(uint8_t *f, uint16_t number, uint32_t len)
{
    memset(f, 0x55, DOU_VIDEO_FRAME_HEADER_LEN + len);
    f[0] = DOU_VIDEO_FRAME_MAGIC;
    f[1] = 0;
    f[2] = (uint8_t)number;
    f[3] = (uint8_t)(number >> 8);
    f[4] = (uint8_t)len;
    f[5] = (uint8_t)(len >> 8);
    f[6] = (uint8_t)(len >> 16);
    f[7] = (uint8_t)(len >> 24);
    f[8] = f[9] = f[10] = 0;   /* an access unit's start code */
    f[11] = 1;
    return DOU_VIDEO_FRAME_HEADER_LEN + len;
}

int main(void)
{
    static uint8_t ring[4096], f[600], out[600];
    dou_vframe_t fr;
    dou_vstream_init(&s, ring, sizeof(ring), 1024);
    dou_vstream_start(&s);
    assert(dou_vstream_pop(&s, out, sizeof(out), &fr) == 0);   /* nothing yet */
    dou_vstream_write(&s, f, frame(f, 1, 500));
    /* Its second barrier is the one before the consume. */
    countdown = 2;
    assert(dou_vstream_pop(&s, out, sizeof(out), &fr) == 500);
    /* The new stream starts clean all the same. */
    dou_vstream_write(&s, f, frame(f, 7, 300));
    assert(dou_vstream_pop(&s, out, sizeof(out), &fr) == 300 && fr.number == 7);
    assert(dou_vstream_available(&s) == sizeof(ring));
    /* A start with the old stream unread: the new one's credits are the
     * whole ring at once, before the consumer has popped (it may be waiting
     * for those very credits to bring a frame). */
    dou_vstream_write(&s, f, frame(f, 8, 500));
    dou_vstream_write(&s, f, frame(f, 9, 500));
    assert(dou_vstream_available(&s) < sizeof(ring));
    dou_vstream_start(&s);
    assert(dou_vstream_available(&s) == sizeof(ring));
    dou_vstream_write(&s, f, frame(f, 1, 200));
    assert(dou_vstream_pop(&s, out, sizeof(out), &fr) == 200 && fr.number == 1);
    assert(dou_vstream_available(&s) == sizeof(ring));
    puts("PASS restart");
    return 0;
}
'''
        cc = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp, "restart.c"), Path(tmp, "restart")
            src.write_text(harness)
            build = subprocess.run(
                [*cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                 f"-I{VSTREAM / 'include'}", f"-I{VSTREAM}", "-o", str(exe), str(src)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS restart", run.stdout)

    def test_wired_under_the_option(self) -> None:
        cmake = (DOU / "CMakeLists.txt").read_text()
        self.assertIn("if(CONFIG_DOU_VIDEO)", cmake)
        self.assertIn('"dou_video_wifi.c" "dou_video_wire.c"', cmake)
        self.assertIn("dou_vstream tinyh264 libvpx_vp8", cmake)
        self.assertIn('list(APPEND srcs "dou_video.c")', cmake)
        self.assertIn('list(APPEND srcs "dou_vdec.c")', cmake)
        self.assertIn('list(APPEND srcs "dou_yuv.c")', cmake)
        self.assertIn("if(CONFIG_DOU_VIDEO OR CONFIG_DOU_AVATAR_ANIMATIONS)", cmake)
        manifest = (DOU / "idf_component.yml").read_text()
        # Keyed on a symbol every build defines: a rule on one a build lacks
        # (DOU_VIDEO_WIFI without video) stops the component manager.
        self.assertIn('if: "$CONFIG{DOU_VIDEO_MDNS} == \\"y\\""', manifest)
        kc = (DOU / "Kconfig").read_text()
        mdns = kc[kc.index("config DOU_VIDEO_MDNS"):]
        mdns = mdns[:mdns.index("\n\n")]
        self.assertNotIn("depends on", mdns)
        self.assertIn('default "n"', mdns)
        app = (DOU / "dou_app.c").read_text()
        self.assertIn("#if CONFIG_DOU_VIDEO_WIFI\n    dou_video_wifi_start();", app)
        # The touch boards' settings decide the Phone camera row as they're
        # built, so the transport has to be there before the UI.
        self.assertLess(app.index("dou_video_wifi_start();"), app.index("dou_ui_start()"))
        # Any button leaves the view; the console opens and closes it.
        inp = (DOU / "dou_input.c").read_text()
        self.assertIn("dou_video_close();\n            ev &= ~(presses | DOU_BTN_POWER_LONG);", inp)
        self.assertIn('!strcmp(line, "video=0")', inp)
        # The transports' clients hear the view open and close.
        video = (DOU / "dou_video.c").read_text()
        self.assertIn("s_transports[i]->request(true);", video)
        self.assertIn("s_transports[i]->request(false);", video)
        tiny = (ROOT / "components/tinyh264/CMakeLists.txt").read_text()
        self.assertIn("if(CONFIG_DOU_VIDEO OR CONFIG_DOU_AVATAR_ANIMATIONS)", tiny)
        vpx = (ROOT / "components/libvpx_vp8/CMakeLists.txt").read_text()
        self.assertIn("if(CONFIG_DOU_VIDEO_VP8)", vpx)
        # VP8's IRAM placement is a choice that follows the decoders' IRAM option.
        self.assertIn("default DOU_VP8_IRAM_COMPACT if DOU_VIDEO_DECODER_IRAM",
                      (ROOT / "components/libvpx_vp8/Kconfig").read_text())
        self.assertIn("elif DOU_VP8_IRAM_COMPACT = y:", (ROOT / "components/libvpx_vp8/linker.lf").read_text())
        # The ring resyncs by the stream's codec, whatever the transport.
        self.assertIn("t->ring->codec = vp->codec;", video)
        # A replaced stream waits for both pictures, as a stopped one does.
        swap = video[video.index("if (!dec || gen != s_gen) {"):]
        swap = swap[:swap.index("dou_vdec_close(dec);")]
        self.assertEqual(swap.count("xSemaphoreTake(s_slots, portMAX_DELAY);"), 2)

    def test_radio_held_for_a_stream(self) -> None:
        """A stream keeps Wi-Fi out of modem sleep and BLE quiet, and gives both back once."""
        wifi = (DOU / "dou_video_wifi.c").read_text()
        self.assertIn("dou_wifi_keep_awake(on);\n    dou_link_ble_quiet(on);", wifi)
        self.assertEqual(wifi.count("radio_for_video(true);"), 1)
        self.assertEqual(wifi.count("radio_for_video(false);"), 2)   # the client's stop, the gadget's
        self.assertIn("__atomic_exchange_n(&s_streaming, false", wifi)
        ble = (ROOT / "main/ble_server.c").read_text()
        start = ble[ble.index("static void start_advertising(void) {"):]
        self.assertIn("if (s_quiet) return;", start[:400])
        self.assertIn(".ble_quiet = ble_server_set_quiet,", (ROOT / "main/dou_glue.c").read_text())


if __name__ == "__main__":
    unittest.main()
