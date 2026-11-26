/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/*
 * When a voice note that didn't reach Dou is sent again
 * (CONFIG_DOU_RESEND_NOTES). Only the rules, with no ESP-IDF in them, so
 * tests/test_dou_resend.py runs them on the host: dou_voice.c keeps the
 * notes in PSRAM and sends them, and dou_chat_session.cpp says how each
 * try failed.
 *
 * POST /chat/stream has no idempotency key, so a note is sent again only
 * when Dou can't have it: its body never ended, or Dou answered with an
 * error that isn't a refusal. A note that went up whole and was never
 * acknowledged may be in the chat already, and isn't sent twice.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dou_chat.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How a note's request ended without an ack: status is its HTTP status (0 if
 * none came), body_sent whether the body's last chunk went out. Any 4xx but
 * 408 and 429 is a refusal, whenever it comes.
 */
dou_chat_fail_t dou_resend_failure(int status, bool body_sent);

/*
 * The voice task's view of a try that didn't land: what Dou reported, or
 * without a report (the voice task gave up waiting), whether it handed the
 * whole note over (fed), after which Dou may have it.
 */
dou_chat_fail_t dou_resend_outcome(dou_chat_fail_t reported, bool fed);

/* The wait after a note's tries-th failed try: 5 s, 15 s, 30 s, then 60 s. */
int64_t dou_resend_delay_us(int tries);

/*
 * How many of the oldest kept notes (sizes in bytes, oldest first) to drop so
 * that one more of `bytes` fits within max_count notes and max_bytes, or -1
 * if it doesn't fit even on its own.
 */
int dou_resend_make_room(const size_t *sizes, int count, int max_count, size_t max_bytes, size_t bytes);

#ifdef __cplusplus
}
#endif
