#include "ElkBindings.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

ElkContext* ElkBindings::activeCtx = nullptr;

// Map a JS font index (0-3) to the firmware font ID.
static int mapFontId(int jsId) {
  static constexpr int kFontIds[] = {
      SMALL_FONT_ID,         // 0 — small / UI small
      UI_12_FONT_ID,         // 1 — UI medium
      NOTOSERIF_14_FONT_ID,  // 2 — body
      NOTOSERIF_18_FONT_ID,  // 3 — heading
  };
  if (jsId < 0 || jsId >= static_cast<int>(sizeof(kFontIds) / sizeof(kFontIds[0]))) return UI_12_FONT_ID;
  return kFontIds[jsId];
}

// Map a JS button index (0-5) to MappedInputManager::Button.
static MappedInputManager::Button mapButton(int jsId) {
  switch (jsId) {
    case 0: return MappedInputManager::Button::Back;
    case 1: return MappedInputManager::Button::Confirm;
    case 2: return MappedInputManager::Button::Left;
    case 3: return MappedInputManager::Button::Right;
    case 4: return MappedInputManager::Button::Up;
    case 5: return MappedInputManager::Button::Down;
    default: return MappedInputManager::Button::Back;
  }
}

void ElkBindings::install(ElkContext* ctx) {
  activeCtx = ctx;
  struct js* js = ctx->elk;
  jsval_t glob = js_glob(js);

  // Button constants: BTN_BACK=0, BTN_OK=1, BTN_LEFT=2, BTN_RIGHT=3, BTN_UP=4, BTN_DOWN=5
  js_set(js, glob, "BTN_BACK", js_mknum(0));
  js_set(js, glob, "BTN_OK", js_mknum(1));
  js_set(js, glob, "BTN_LEFT", js_mknum(2));
  js_set(js, glob, "BTN_RIGHT", js_mknum(3));
  js_set(js, glob, "BTN_UP", js_mknum(4));
  js_set(js, glob, "BTN_DOWN", js_mknum(5));

  // Font constants: FONT_SMALL=0, FONT_UI=1, FONT_BODY=2, FONT_HEADING=3
  js_set(js, glob, "FONT_SMALL", js_mknum(0));
  js_set(js, glob, "FONT_UI", js_mknum(1));
  js_set(js, glob, "FONT_BODY", js_mknum(2));
  js_set(js, glob, "FONT_HEADING", js_mknum(3));

  // renderer object
  jsval_t renderer = js_mkobj(js);
  js_set(js, renderer, "clear", js_mkfun(jsClear));
  js_set(js, renderer, "text", js_mkfun(jsText));
  js_set(js, renderer, "fillRect", js_mkfun(jsFillRect));
  js_set(js, renderer, "rect", js_mkfun(jsDrawRect));
  js_set(js, renderer, "width", js_mkfun(jsWidth));
  js_set(js, renderer, "height", js_mkfun(jsHeight));
  js_set(js, renderer, "flush", js_mkfun(jsFlush));
  js_set(js, glob, "renderer", renderer);

  // input object
  jsval_t input = js_mkobj(js);
  js_set(js, input, "released", js_mkfun(jsInputReleased));
  js_set(js, input, "pressed", js_mkfun(jsInputPressed));
  js_set(js, input, "held", js_mkfun(jsInputHeld));
  js_set(js, glob, "input", input);

  // http object
  jsval_t http = js_mkobj(js);
  js_set(js, http, "get", js_mkfun(jsHttpGet));
  js_set(js, glob, "http", http);

  // storage object
  jsval_t storage = js_mkobj(js);
  js_set(js, storage, "read", js_mkfun(jsStorageRead));
  js_set(js, storage, "write", js_mkfun(jsStorageWrite));
  js_set(js, glob, "storage", storage);

  // app object
  jsval_t app = js_mkobj(js);
  js_set(js, app, "finish", js_mkfun(jsAppFinish));
  js_set(js, app, "log", js_mkfun(jsAppLog));
  js_set(js, glob, "app", app);
}

void ElkBindings::clear() { activeCtx = nullptr; }

// --- renderer bindings ---

jsval_t ElkBindings::jsClear(struct js*, jsval_t*, int) {
  if (activeCtx && activeCtx->renderer) {
    RenderLock lock;
    activeCtx->renderer->clearScreen();
    activeCtx->framebufferDirty = true;
  }
  return js_mkundef();
}

jsval_t ElkBindings::jsText(struct js* js, jsval_t* args, int argc) {
  // text(fontId, x, y, text [, black=true])
  if (!activeCtx || !activeCtx->renderer) return js_mkundef();
  if (argc < 4) return js_mkerr(js, "text() needs 4 args");

  const int fontId = mapFontId(static_cast<int>(js_getnum(args[0])));
  const int x = static_cast<int>(js_getnum(args[1]));
  const int y = static_cast<int>(js_getnum(args[2]));
  const bool black = argc < 5 || js_truthy(js, args[4]);

  size_t len = 0;
  const char* str = js_getstr(js, args[3], &len);
  if (!str) return js_mkundef();

  // js_getstr returns a pointer into the Elk heap — it is null-terminated.
  RenderLock lock;
  activeCtx->renderer->drawText(fontId, x, y, str, black);
  activeCtx->framebufferDirty = true;
  return js_mkundef();
}

jsval_t ElkBindings::jsFillRect(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || !activeCtx->renderer) return js_mkundef();
  if (argc < 4) return js_mkerr(js, "fillRect() needs 4 args");
  const int x = static_cast<int>(js_getnum(args[0]));
  const int y = static_cast<int>(js_getnum(args[1]));
  const int w = static_cast<int>(js_getnum(args[2]));
  const int h = static_cast<int>(js_getnum(args[3]));
  const bool fill = argc < 5 || js_truthy(js, args[4]);
  RenderLock lock;
  activeCtx->renderer->fillRect(x, y, w, h, fill);
  activeCtx->framebufferDirty = true;
  return js_mkundef();
}

jsval_t ElkBindings::jsDrawRect(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || !activeCtx->renderer) return js_mkundef();
  if (argc < 4) return js_mkerr(js, "rect() needs 4 args");
  const int x = static_cast<int>(js_getnum(args[0]));
  const int y = static_cast<int>(js_getnum(args[1]));
  const int w = static_cast<int>(js_getnum(args[2]));
  const int h = static_cast<int>(js_getnum(args[3]));
  RenderLock lock;
  activeCtx->renderer->drawRect(x, y, w, h, true);
  activeCtx->framebufferDirty = true;
  return js_mkundef();
}

jsval_t ElkBindings::jsWidth(struct js*, jsval_t*, int) {
  if (!activeCtx || !activeCtx->renderer) return js_mknum(0);
  return js_mknum(activeCtx->renderer->getScreenWidth());
}

jsval_t ElkBindings::jsHeight(struct js*, jsval_t*, int) {
  if (!activeCtx || !activeCtx->renderer) return js_mknum(0);
  return js_mknum(activeCtx->renderer->getScreenHeight());
}

jsval_t ElkBindings::jsFlush(struct js*, jsval_t*, int) {
  if (activeCtx && activeCtx->framebufferDirty && !activeCtx->updatePending) {
    LOG_DBG("ELK", "renderer.flush accepted");
    activeCtx->needsUpdate = true;
    activeCtx->updatePending = true;
    activeCtx->framebufferDirty = false;
  } else if (activeCtx) {
    LOG_DBG("ELK", "renderer.flush ignored dirty=%d pending=%d", activeCtx->framebufferDirty,
            activeCtx->updatePending);
  }
  return js_mkundef();
}

// --- input bindings ---

jsval_t ElkBindings::jsInputReleased(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || !activeCtx->input || argc < 1) return js_mkfalse();
  const auto btn = mapButton(static_cast<int>(js_getnum(args[0])));
  return activeCtx->input->wasReleased(btn) ? js_mktrue() : js_mkfalse();
}

jsval_t ElkBindings::jsInputPressed(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || !activeCtx->input || argc < 1) return js_mkfalse();
  const auto btn = mapButton(static_cast<int>(js_getnum(args[0])));
  return activeCtx->input->wasPressed(btn) ? js_mktrue() : js_mkfalse();
}

jsval_t ElkBindings::jsInputHeld(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || !activeCtx->input || argc < 1) return js_mkfalse();
  const auto btn = mapButton(static_cast<int>(js_getnum(args[0])));
  return activeCtx->input->isPressed(btn) ? js_mktrue() : js_mkfalse();
}

// --- http bindings ---

jsval_t ElkBindings::jsHttpGet(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || argc < 1) return js_mknull();
  size_t len = 0;
  const char* url = js_getstr(js, args[0], &len);
  if (!url) return js_mknull();

  std::string body;
  if (!HttpDownloader::fetchUrl(std::string(url, len), body)) return js_mknull();

  return js_mkstr(js, body.data(), body.size());
}

// --- storage bindings ---

jsval_t ElkBindings::jsStorageRead(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || argc < 1) return js_mknull();
  size_t len = 0;
  const char* path = js_getstr(js, args[0], &len);
  if (!path) return js_mknull();

  std::string pathStr(path, len);
  const String content = Storage.readFile(pathStr.c_str());
  if (content.isEmpty()) return js_mknull();

  return js_mkstr(js, content.c_str(), content.length());
}

jsval_t ElkBindings::jsStorageWrite(struct js* js, jsval_t* args, int argc) {
  if (!activeCtx || argc < 2) return js_mkfalse();
  size_t pathLen = 0;
  size_t dataLen = 0;
  const char* path = js_getstr(js, args[0], &pathLen);
  const char* data = js_getstr(js, args[1], &dataLen);
  if (!path || !data) return js_mkfalse();

  std::string pathStr(path, pathLen);
  const bool ok = Storage.writeFile(pathStr.c_str(), String(data));
  return ok ? js_mktrue() : js_mkfalse();
}

// --- app bindings ---

jsval_t ElkBindings::jsAppFinish(struct js*, jsval_t*, int) {
  if (activeCtx && activeCtx->finishFn) activeCtx->finishFn();
  return js_mkundef();
}

jsval_t ElkBindings::jsAppLog(struct js* js, jsval_t* args, int argc) {
  if (argc < 1) return js_mkundef();
  const char* msg = js_str(js, args[0]);
  if (msg) LOG_INF("JS", "%s", msg);
  return js_mkundef();
}
