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

#include "dou_resend.h"

dou_chat_fail_t dou_resend_failure(int status, bool body_sent)
{
    /* 408 and 429 ask to try later; the rest of 4xx won't change. */
    if (status >= 400 && status < 500 && status != 408 && status != 429) {
        return DOU_CHAT_FAIL_REFUSED;
    }
    return body_sent ? DOU_CHAT_FAIL_UNKNOWN : DOU_CHAT_FAIL_RETRY;
}

dou_chat_fail_t dou_resend_outcome(dou_chat_fail_t reported, bool fed)
{
    if (reported != DOU_CHAT_FAIL_NONE) {
        return reported;
    }
    return fed ? DOU_CHAT_FAIL_UNKNOWN : DOU_CHAT_FAIL_RETRY;
}

int64_t dou_resend_delay_us(int tries)
{
    static const int8_t secs[] = { 5, 15, 30, 60 };
    int i = tries < 1 ? 0 : tries > (int)sizeof(secs) ? (int)sizeof(secs) - 1 : tries - 1;
    return secs[i] * 1000000LL;
}

int dou_resend_make_room(const size_t *sizes, int count, int max_count, size_t max_bytes, size_t bytes)
{
    if (max_count < 1 || bytes > max_bytes) {
        return -1;
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        total += sizes[i];
    }
    int drop = 0;
    while (drop < count && (count - drop >= max_count || total + bytes > max_bytes)) {
        total -= sizes[drop++];
    }
    return drop;
}
