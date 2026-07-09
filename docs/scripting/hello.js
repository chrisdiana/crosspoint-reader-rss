// @name Hello World
// @icon File

let onEnter = function() {
  app.log("hello loaded draw-once");
  renderer.clear();
  renderer.text(FONT_HEADING, 50, 100, "Hello World!");
  renderer.text(FONT_SMALL, 50, 160, "Press BACK to exit");
  renderer.flush();
};

let loop = function() {
  if (input.pressed(BTN_BACK)) {
    app.finish();
  }
};

let onExit = function() {
  app.log("hello exit");
};
