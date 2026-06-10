#pragma once
#include <elk.h>

#include <memory>
#include <string>

#include "ElkBindings.h"
#include "activities/Activity.h"

// Runs a JavaScript app loaded from a .js file on the SD card.
// The script may define:
//   function onEnter() {}   — called once when the app opens
//   function loop()   {}   — called every frame for input and drawing
//   function onExit() {}   — called when the app closes
//
// The JS global scope provides: renderer, input, http, storage, app,
// and constants BTN_*, FONT_*.
class ElkAppActivity final : public Activity {
 public:
  static constexpr size_t ELK_HEAP_SIZE = 32 * 1024;

  ElkAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string scriptPath)
      : Activity("Script", renderer, mappedInput), scriptPath(std::move(scriptPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }

 private:
  std::string scriptPath;
  std::unique_ptr<uint8_t[]> elkHeap;
  struct js* elkJs = nullptr;
  ElkContext elkCtx;
  bool hasError = false;
  bool finishRequested = false;

  void callJsFn(const char* fnCall);
  void requestFinish();
};
