// @name Hello World
// @icon File
//
// Example CrossPoint Plus script.
// Drop into /.crosspoint/apps/ on the SD card — no reflash needed.
//
// Available globals:
//   renderer  — draw to the e-ink framebuffer
//   input     — read button state
//   http      — blocking HTTP GET
//   storage   — SD card read/write
//   app       — lifecycle control
//
// Button constants:  BTN_BACK  BTN_OK  BTN_LEFT  BTN_RIGHT  BTN_UP  BTN_DOWN
// Font constants:    FONT_SMALL  FONT_UI  FONT_BODY  FONT_HEADING

var tick = 0;

function onEnter() {
  app.log("hello.js loaded");
}

function loop() {
  if (input.released(BTN_BACK)) {
    app.finish();
    return;
  }

  tick = tick + 1;

  renderer.clear();
  renderer.text(FONT_HEADING, 20, 60, "Hello from JS!");
  renderer.text(FONT_UI, 20, 110, "frame: " + tick);
  renderer.text(FONT_SMALL, 20, 150, "Press BACK to exit");
  renderer.flush();
}

function onExit() {
  app.log("hello.js exiting");
}
