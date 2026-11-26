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
#include "sdkconfig.h"
#include "dou_theme_policy.h"
#if CONFIG_DOU_UI_THEME
void dou_theme_start(void);
dou_theme_mode_t dou_theme_mode(void);
void dou_theme_request_mode(dou_theme_mode_t mode);
/* Content providers publish a stable background hint from any task. No
 * provider is required; Automatic falls back to the original dark scheme. */
void dou_theme_set_hint(bool valid,uint32_t rgb);
uint32_t dou_theme_epoch(void);
uint32_t dou_theme_hint(void);
int dou_theme_save_error(void);
#else
static inline void dou_theme_start(void) {}
static inline dou_theme_mode_t dou_theme_mode(void) {return DOU_THEME_DARK;}
static inline void dou_theme_request_mode(dou_theme_mode_t mode) {(void)mode;}
static inline void dou_theme_set_hint(bool valid,uint32_t rgb) {(void)valid;(void)rgb;}
#endif
