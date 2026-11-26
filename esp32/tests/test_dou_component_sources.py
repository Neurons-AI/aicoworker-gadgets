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

"""Execute component source selection for graphical, headless and Link builds."""
from pathlib import Path
import subprocess
import tempfile
import unittest

DOU = Path(__file__).resolve().parents[1] / 'components/dou'


class DouComponentSources(unittest.TestCase):
    def sources(self, **config):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            output = directory / 'sources.txt'
            script = directory / 'CMakeLists.txt'
            # Only replace the ESP-IDF registration/target hooks. The entire
            # production component CMakeLists selects its sources unchanged.
            hooks = '''
function(idf_component_register)
    cmake_parse_arguments(component "WHOLE_ARCHIVE" ""
        "SRCS;INCLUDE_DIRS;EMBED_FILES;LDFRAGMENTS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    string(REPLACE ";" "\\n" lines "${component_SRCS}")
    file(WRITE "${SOURCES_OUT}" "${lines}")
endfunction()
function(idf_component_get_property name)
    set(${name} test_target PARENT_SCOPE)
endfunction()
function(target_compile_definitions)
endfunction()
function(set_source_files_properties)
endfunction()
'''
            settings = [f'set({key} {"ON" if value else "OFF"})' for key, value in config.items()]
            script.write_text(
                'cmake_minimum_required(VERSION 3.16)\nproject(dou_sources NONE)\n' + hooks + '\n'.join(settings)
                + f'\nset(COMPONENT_DIR "{DOU.as_posix()}")\n'
                + f'set(SOURCES_OUT "{output.as_posix()}")\n'
                + f'include("{(DOU / "CMakeLists.txt").as_posix()}")\n')
            result = subprocess.run(['cmake', '-S', str(directory), '-B', str(directory / 'build')],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return output.read_text().splitlines()

    def assert_option_support(self, sources):
        self.assertIn('dou_chat_session.cpp', sources)
        self.assertIn('dou_options.c', sources)
        self.assertIn('dou_options_json.c', sources)

    def test_each_headless_voice_profile_links_chat_option_support_without_ui(self):
        for board in ('VOICE_PE', 'RESPEAKER_LITE', 'RESPEAKER_XVF3800', 'WAVESHARE_154G'):
            for realtime in (False, True):
                with self.subTest(board=board, realtime=realtime):
                    sources = self.sources(CONFIG_HOMEHUB_VOICE=True,
                                           CONFIG_DOU_REALTIME_VOICE=realtime,
                                           **{f'CONFIG_HOMEHUB_VOICE_BOARD_{board}': True})
                    self.assert_option_support(sources)
                    self.assertNotIn('dou_ui.c', sources)
                    self.assertNotIn('dou_options_ui.c', sources)

    def test_touchscreen_chat_build_includes_picker(self):
        sources = self.sources(CONFIG_DOU_ENABLED=True, CONFIG_DOU_HATCH=True)
        self.assert_option_support(sources)
        self.assertIn('dou_options_ui.c', sources)

    def test_display_only_touch_boards_get_no_picker(self):
        # Answers go out through the voice task, which needs a speaker or mic.
        ui = (DOU / 'dou_ui.c').read_text()
        start = ui.index('dou_options_ui_start(')
        guard = ui[ui.rindex('if (', 0, start):start]
        self.assertIn('dou_board->touch', guard)
        self.assertIn('dou_board->audio_init || dou_board->mic_init', guard)

    def test_open_picker_is_not_the_face_for_touch_to_talk(self):
        # Boards with touch-and-hold talk poll dou_ui_face_active(); a held
        # choice in the open picker must not start a voice note.
        ui = (DOU / 'dou_ui.c').read_text()
        start = ui.index('bool dou_ui_face_active(void)')
        self.assertIn('!dou_options_ui_open()', ui[start:ui.index('}', start)])

    def test_freeink_chat_build_uses_shared_support_without_lvgl_picker(self):
        sources = self.sources(CONFIG_DOU_ENABLED=True, CONFIG_DOU_HATCH=True,
                               CONFIG_DOU_UI_FREEINK=True)
        self.assert_option_support(sources)
        self.assertNotIn('dou_options_ui.c', sources)

    def test_link_only_ui_does_not_link_the_session_json_adapter(self):
        sources = self.sources(CONFIG_DOU_ENABLED=True)
        self.assertIn('dou_chat_link.c', sources)
        self.assertNotIn('dou_chat_session.cpp', sources)
        self.assertNotIn('dou_options_json.c', sources)

    def test_plain_link_does_not_link_option_support(self):
        self.assertEqual(self.sources(), [])
