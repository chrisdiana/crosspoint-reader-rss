#include "ElkAppActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "ElkBindings.h"

void ElkAppActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("ELK", "Opening: %s (heap=%u)", scriptPath.c_str(), (unsigned)ELK_HEAP_SIZE);

  // Scripts draw in the documented Portrait coordinate system. Force it here so
  // we don't inherit a Landscape/inverted orientation left by a reader session
  // (which rendered the script's text rotated/backwards).
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  elkHeap = makeUniqueNoThrow<uint8_t[]>(ELK_HEAP_SIZE);
  if (!elkHeap) {
    LOG_ERR("ELK", "OOM: %u byte heap", (unsigned)ELK_HEAP_SIZE);
    hasError = true;
    finish();
    return;
  }

  elkJs = js_create(elkHeap.get(), ELK_HEAP_SIZE);
  if (!elkJs) {
    LOG_ERR("ELK", "js_create failed");
    hasError = true;
    elkHeap.reset();
    finish();
    return;
  }

  // Cap C stack usage so deeply-recursive scripts can't overflow the main
  // loop task stack (8192 bytes), where onEnter()/loop() run. 2048 was too low:
  // even a simple script with a nested if-in-function needs ~2.5-3.5 KB of C
  // stack to parse, which surfaced as a misleading "parse error". 4096 covers
  // that with margin while still guarding against runaway recursion.
  js_setmaxcss(elkJs, 4096);

  elkCtx.elk = elkJs;
  elkCtx.renderer = &renderer;
  elkCtx.input = &mappedInput;
  elkCtx.finishFn = [this] { requestFinish(); };
  elkCtx.needsUpdate = false;
  elkCtx.framebufferDirty = false;
  elkCtx.updatePending = false;

  ElkBindings::install(&elkCtx);

  // Load and evaluate the script file.
  const String src = Storage.readFile(scriptPath.c_str());
  if (src.isEmpty()) {
    LOG_ERR("ELK", "Could not read: %s", scriptPath.c_str());
    hasError = true;
    finish();
    return;
  }

  jsval_t result = js_eval(elkJs, src.c_str(), src.length());
  if (js_type(result) == JS_ERR) {
    LOG_ERR("ELK", "Script error: %s", js_str(elkJs, result));
    hasError = true;
    finish();
    return;
  }

  LOG_DBG("ELK", "Loaded: %s", scriptPath.c_str());

  elkCtx.needsUpdate = false;
  callJsFn("onEnter()");
  if (elkCtx.needsUpdate) requestUpdate();
}

void ElkAppActivity::onExit() {
  LOG_INF("ELK", "onExit");
  callJsFn("onExit()");
  ElkBindings::clear();
  elkJs = nullptr;
  elkHeap.reset();
  Activity::onExit();
}

void ElkAppActivity::loop() {
  if (hasError || finishRequested) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    LOG_INF("ELK", "Native Back exit");
    requestFinish();
    return;
  }
  elkCtx.needsUpdate = false;
  callJsFn("loop()");
  if (elkCtx.needsUpdate) requestUpdate();
}

void ElkAppActivity::render(RenderLock&&) {
  // JS drawing bindings take RenderLock when mutating the framebuffer; render
  // receives the same lock from ActivityManager and flushes a stable buffer.
  renderer.displayBuffer();
  elkCtx.updatePending = false;
}

void ElkAppActivity::callJsFn(const char* fnCall) {
  if (!elkJs || hasError) return;
  jsval_t result = js_eval(elkJs, fnCall, strlen(fnCall));
  if (js_type(result) == JS_ERR) {
    const char* errMsg = js_str(elkJs, result);
    // Ignore "not defined" errors — the function is optional.
    if (errMsg && strstr(errMsg, "not defined") == nullptr) {
      LOG_ERR("ELK", "%s error: %s", fnCall, errMsg);
      hasError = true;
    }
  }
}

void ElkAppActivity::requestFinish() {
  if (finishRequested) return;
  LOG_INF("ELK", "Finish requested");
  finishRequested = true;
  finish();
}
