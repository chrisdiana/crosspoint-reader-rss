#include "ElkAppActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "ElkBindings.h"

void ElkAppActivity::onEnter() {
  Activity::onEnter();

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

  // Cap C stack usage so deeply-recursive scripts can't overflow the ESP32-C3 task stack.
  js_setmaxcss(elkJs, 512);

  elkCtx.elk = elkJs;
  elkCtx.renderer = &renderer;
  elkCtx.input = &mappedInput;
  elkCtx.finishFn = [this] { finish(); };
  elkCtx.needsUpdate = false;

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
  callJsFn("onEnter()");

  requestUpdate();
}

void ElkAppActivity::onExit() {
  callJsFn("onExit()");
  ElkBindings::clear();
  elkJs = nullptr;
  elkHeap.reset();
  Activity::onExit();
}

void ElkAppActivity::loop() {
  if (hasError) return;
  elkCtx.needsUpdate = false;
  callJsFn("loop()");
  if (elkCtx.needsUpdate) requestUpdate();
}

void ElkAppActivity::render(RenderLock&&) {
  // JS draws directly to the framebuffer via bindings in loop(); just flush here.
  renderer.displayBuffer();
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
