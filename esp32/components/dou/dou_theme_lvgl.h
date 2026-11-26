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
#include "dou_theme.h"
#include "lvgl.h"
#if CONFIG_DOU_UI_THEME
void dou_theme_lvgl_init(void);
bool dou_theme_lvgl_tick(void);
uint32_t dou_theme_background(void);
void dou_theme_set_style_bg_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_text_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_arc_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_border_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_outline_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_line_color(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
void dou_theme_set_style_image_recolor(lv_obj_t *obj,lv_color_t color,lv_style_selector_t selector);
#else
static inline void dou_theme_lvgl_init(void) {}
static inline bool dou_theme_lvgl_tick(void) {return false;}
static inline uint32_t dou_theme_background(void) {return 0;}
#define dou_theme_set_style_bg_color lv_obj_set_style_bg_color
#define dou_theme_set_style_text_color lv_obj_set_style_text_color
#define dou_theme_set_style_arc_color lv_obj_set_style_arc_color
#define dou_theme_set_style_border_color lv_obj_set_style_border_color
#define dou_theme_set_style_outline_color lv_obj_set_style_outline_color
#define dou_theme_set_style_line_color lv_obj_set_style_line_color
#define dou_theme_set_style_image_recolor lv_obj_set_style_image_recolor
#endif
