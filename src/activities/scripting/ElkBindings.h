#pragma once
#include <elk.h>
#include <functional>

#include "GfxRenderer.h"
#include "MappedInputManager.h"

// Active context for Elk C-function bindings. Only one ElkAppActivity can
// be running at a time, so a single static pointer is safe.
struct ElkContext {
  struct js* elk = nullptr;
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  std::function<void()> finishFn;
  bool needsUpdate = false;  // Set by renderer.flush(); checked in loop()
  bool framebufferDirty = false;
  bool updatePending = false;
};

class ElkBindings {
 public:
  // Install all JS objects/functions into the Elk global scope.
  // ctx must remain valid for the lifetime of the Elk instance.
  static void install(ElkContext* ctx);

  // Clear the active context (call in onExit).
  static void clear();

 private:
  static ElkContext* activeCtx;

  // renderer.*
  static jsval_t jsClear(struct js*, jsval_t*, int);
  static jsval_t jsText(struct js*, jsval_t*, int);
  static jsval_t jsFillRect(struct js*, jsval_t*, int);
  static jsval_t jsDrawRect(struct js*, jsval_t*, int);
  static jsval_t jsWidth(struct js*, jsval_t*, int);
  static jsval_t jsHeight(struct js*, jsval_t*, int);
  static jsval_t jsFlush(struct js*, jsval_t*, int);

  // input.*
  static jsval_t jsInputReleased(struct js*, jsval_t*, int);
  static jsval_t jsInputPressed(struct js*, jsval_t*, int);
  static jsval_t jsInputHeld(struct js*, jsval_t*, int);

  // http.*
  static jsval_t jsHttpGet(struct js*, jsval_t*, int);

  // storage.*
  static jsval_t jsStorageRead(struct js*, jsval_t*, int);
  static jsval_t jsStorageWrite(struct js*, jsval_t*, int);

  // app.*
  static jsval_t jsAppFinish(struct js*, jsval_t*, int);
  static jsval_t jsAppLog(struct js*, jsval_t*, int);
};
