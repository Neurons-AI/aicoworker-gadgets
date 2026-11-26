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
 * Inside the apps component: what the runtime (dou_apps.c) gives the
 * modules that implement the API, one file per group of components/dou_apps/sdk/api/api.json:
 *
 *   dou_apps_sys.c     sys_*, dou_*
 *   dou_apps_ui.c      ui_* (objects, properties, styles, animation, images)
 *   dou_apps_canvas.c  cv_*
 *   dou_apps_chat.c    chat_*
 *   dou_apps_files.c   file_read, state_*
 *   dou_apps_dev.c     dev_*
 *
 * Installed apps (app.install, app.list, app.remove, app.run {id}) live in
 * dou_apps_store.c over dou_apps_catalog.c, and the launcher that opens
 * them, a list of LVGL objects, in dou_apps_launcher.c.
 *
 * Each import is gadget_<name>, declared in dou_apps_api.h (generated from
 * api.json); one a module doesn't define falls back to a weak stub that
 * answers GADGET_ERR_UNSUPPORTED. Imports run on the app's task, one at a
 * time, between the app's instructions: they mustn't block.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "dou_apps_catalog.h"
#include "capabilities_registry.h"
#include "wasm_export.h"

#define DOU_APPS_NATIVES
#include "dou_apps_api.h"

/* ---- Requests and their answers ---- */

/* Who asked, so the answer goes back there: Dou, the console's "@app" line,
 * or nobody (the launcher, the view). */
typedef enum { DOU_APPS_FROM_DOU, DOU_APPS_FROM_CONSOLE, DOU_APPS_FROM_LAUNCHER, DOU_APPS_FROM_VIEW } dou_apps_from_t;
typedef struct {
    dou_apps_from_t from;
    uint64_t session_generation;  /* DOU_APPS_FROM_DOU */
    char request_id[64];
} dou_apps_origin_t;

cJSON *dou_apps_api_command(cJSON *params);

/* Answers a request where it came from. Takes the result. Any task. */
void dou_apps_reply(const dou_apps_origin_t *o, cJSON *result);
cJSON *dou_apps_error(const char *code, const char *message);

/* An app's manifest (the gadget.manifest section), for installing it: its
 * name, title, icon and colour, each "" if absent. False without a manifest. */
bool dou_apps_manifest_info(const uint8_t *bytes, size_t len, char *name, size_t name_cap, char *title,
                             size_t title_cap, char *icon, size_t icon_cap, char *color, size_t color_cap);

/* ---- The running app ---- */

/* Its name: the manifest's, else its file's, else "console app". */
const char *dou_apps_name(void);
/* The manifest lists this permission ("chat.read", "chat.send", "files.home"). */
bool dou_apps_permitted(const char *perm);
/* The manifest names this device in requires or uses. */
bool dou_apps_device_allowed(const char *name);

/* An event for on_event, from any task. False if the queue is full and it was
 * dropped (the app is behind). */
bool dou_apps_post(int type, int a, int b);

/* A line in the app's log (app.logs) and the console. */
void dou_apps_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- Operations: calls that finish later with EV_DONE ---- */

/* App task: a new operation id, or GADGET_ERR_LIMIT. buf (in the app's
 * memory, from a WAMR-checked argument; may be NULL) is where its data lands
 * when it finishes, up to cap bytes. */
int32_t dou_apps_op_begin(wasm_exec_env_t env, void *buf, uint32_t cap);
/* Any task, once per operation: result is 0 or more, or a GADGET_ERR_. data
 * (heap_caps memory, or NULL) is the runtime's from here: it copies up to cap
 * bytes of it into the op's buffer on the app task, then frees it, then calls
 * on_event(EV_DONE, op, result). */
void dou_apps_op_finish(int32_t op, int32_t result, void *data, size_t len);

/* ---- Memory: the app's budget (limits.budget_bytes) ---- */

/* PSRAM counted against the budget; NULL past it. */
void *dou_apps_alloc(size_t bytes);
void *dou_apps_calloc(size_t n, size_t size);
void dou_apps_free(void *p);
/* Memory the app holds that the firmware allocates elsewhere (LVGL objects
 * and their styles): false, and nothing counted, past the budget. */
bool dou_apps_charge(size_t bytes);
void dou_apps_uncharge(size_t bytes);
size_t dou_apps_mem_used(void);
size_t dou_apps_mem_budget(void);

/* ---- LVGL ---- */

/* App task: takes LVGL's lock for the rest of the current callback (the
 * runtime gives it back when the callback returns). Call it before touching
 * any LVGL object. */
void dou_apps_ui_lock(void);

/* ---- Timers and frames (dou_apps.c) ---- */

int32_t dou_apps_timer_set(int32_t id, int32_t ms, bool repeat);
int32_t dou_apps_timer_stop(int32_t id);
int32_t dou_apps_frame_rate(int32_t fps);
void dou_apps_keep_awake(bool on);
void dou_apps_exit(void);

/* ---- Module hooks: the runtime calls them on the app task ---- */

/* The app starts (its view is open, no callback has run yet). */
void dou_apps_ui_start(void);
void dou_apps_chat_start(void);
void dou_apps_dev_start(void);
/* The app has stopped (no more callbacks; LVGL's lock not held; the view
 * still open). Free what the app held. */
void dou_apps_ui_stop(void);
void dou_apps_canvas_stop(void);
void dou_apps_chat_stop(void);
void dou_apps_files_stop(void);
void dou_apps_dev_stop(void);
void dou_apps_sys_stop(void);

/* What Dou is doing, as GADGET_DOU_*. */
int dou_apps_dou_mode(void);
/* Pixels to keep clear of the edges (INFO_SAFE_INSET). */
int dou_apps_safe_inset(void);
/* A device's description as JSON, as device.guide and dev_describe give it. */
cJSON *dou_apps_describe_device(const capability_t *dev);

/* ---- Installed apps (dou_apps_store.c, over dou_apps_catalog.c) ---- */

/* Mounts the "apps" partition and starts the store's task. Once, at boot. */
void dou_apps_store_start(void);
/* A copy of the installed apps, in the order they were installed. Any task. */
int dou_apps_store_list(dou_apps_entry_t *out, int max);
/* app.list's result. */
cJSON *dou_apps_store_list_result(void);
/* Work for the store's task, answered through dou_apps_reply (app.run's
 * through the app that runs). False if its queue is full. id may be NULL or
 * "" for an install: the manifest's name; name, icon and colour likewise
 * default to the manifest's title, icon and colour. path is a .wasm file or
 * an app's folder holding app.wasm. */
bool dou_apps_store_install(const dou_apps_origin_t *o, const char *id, const char *name, const char *icon,
                             const char *color, const char *path, const char *sha256);
/* The console's upload instead of a file on the VM; takes bytes if it returns true. */
bool dou_apps_store_install_bytes(const dou_apps_origin_t *o, const char *id, const char *name, const char *icon,
                                   const char *color, uint8_t *bytes, size_t len);
bool dou_apps_store_remove(const dou_apps_origin_t *o, const char *id);
bool dou_apps_store_open(const dou_apps_origin_t *o, const char *id);
/* The gadget was unpaired: every app goes, before anything else is installed. */
void dou_apps_store_forget_all(void);

/* The gadget's own files ("files" partition, dou_apps_flash.c): Dou's
 * files.put, files.list and files.remove, answered through dou_apps_reply.
 * name is a file's name in the partition, without the leading '/'; sha256,
 * if given, must be the file's. False if the queue is full. */
bool dou_apps_store_file_put(const dou_apps_origin_t *o, const char *path, const char *name, const char *sha256);
/* The console's upload; takes bytes if it returns true. */
bool dou_apps_store_file_put_bytes(const dou_apps_origin_t *o, const char *name, uint8_t *bytes, size_t len);
bool dou_apps_store_file_remove(const dou_apps_origin_t *o, const char *name);
bool dou_apps_store_file_list(const dou_apps_origin_t *o);
/* Apps' reads, on a reader task of the store's (flash reads need a stack the
 * caches being off can't reach, on boards without XIP from PSRAM): finishes
 * op with the bytes, as file_read's EV_DONE; false if its queue is full. */
bool dou_apps_store_file_read(const char *name, size_t offset, size_t max, int32_t op);
/* The whole file, up to max bytes, blocking the caller (an image loader's
 * thread): 0 with *out (the caller frees it), or a GADGET_ERR_. */
int32_t dou_apps_store_file_load(const char *name, size_t max, uint8_t **out, size_t *len);

/* From the store's task, to the runtime: run an installed app's module
 * (takes bytes), or say why it couldn't be read. */
void dou_apps_run_installed(const dou_apps_origin_t *o, const char *id, const char *name, uint8_t *bytes,
                             size_t len);
void dou_apps_open_failed(const dou_apps_origin_t *o, const char *code, const char *message);
/* Closes the running app if it's installed app id (NULL, for unpairing: any app).
 * Any task but the app task. */
void dou_apps_stop_installed(const char *id);
/* The installed app running now, or "". Any task. */
void dou_apps_running_id(char *out, size_t cap);

/* ---- The launcher (dou_apps_launcher.c), on the app task ---- */

/* Builds it under the view's root (just opened); false without memory. hide
 * deletes it, if the root hasn't already gone with it. */
bool dou_apps_launcher_show(void);
void dou_apps_launcher_hide(void);
void dou_apps_launcher_event(int type, int a, int b);
/* A few times a second: the list as the store has it, and notes running out. */
void dou_apps_launcher_frame(uint32_t now_ms);
/* Its Close row (or Esc): back to the face, as the power key does. In dou_apps.c. */
void dou_apps_launcher_close(void);
/* A line at the bottom for a few seconds: "Pomodoro stopped". */
void dou_apps_launcher_note(const char *text);

/* ---- Dou's VM, over Link's session (dou_apps_vm.c) ---- */

/* Whole files, for app.run and the console: block the calling task. On
 * success the caller owns *out. False with a message in err. */
bool dou_apps_vm_file_read(const char *path, size_t max_bytes, int timeout_ms, uint8_t **out, size_t *len,
                            char *err, size_t err_len);
bool dou_apps_vm_file_write(const char *path, const uint8_t *data, size_t len, int timeout_ms, char *err,
                             size_t err_len);
/* The same read handed to sink a piece at a time (up to a frame's worth), for
 * files too large to hold whole; sink returning false stops it. *len is what
 * sink took. */
bool dou_apps_vm_file_stream(const char *path, size_t max_bytes, int timeout_ms,
                              bool (*sink)(void *ctx, const uint8_t *data, size_t len), void *ctx, size_t *len,
                              char *err, size_t err_len);
/* Cancels whatever requests the app left open. */
void dou_apps_vm_close_all(void);
/* An app's path, as file_read and ui_image_load take it, as a path on the VM
 * relative to home: relative to the app's folder, or from "~/" with the
 * files.home permission. 0, or GADGET_ERR_ARG (absolute, "..", too long),
 * GADGET_ERR_DENIED, or GADGET_ERR_UNSUPPORTED (a console app has no folder). */
int32_t dou_apps_vm_path(const char *path, char *out, size_t cap);
/* A path from "/" (file_read, ui_image_load) as the gadget's own file's name
 * in the "files" partition: 0, or ERR_ARG for a bad name, ERR_DENIED without
 * the files.gadget permission. */
int32_t dou_apps_gadget_path(const char *path, char *name, size_t cap);
/* The gadget's folder on the VM: "workspace/gadgets/devices/<gadget>". */
const char *dou_apps_device_dir(void);
