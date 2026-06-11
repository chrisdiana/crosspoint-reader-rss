// @name Counter
// @icon Text

let count = 0;
let dirty = true;

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 42, 80, "Counter");
  renderer.text(FONT_BODY, 42, 145, count);
  renderer.text(FONT_SMALL, 42, 220, "OK +1, LEFT reset, BACK exit");
  renderer.flush();
};

let onEnter = function() {
  count = storage.readNumber("/.crosspoint/apps/counter.txt", 0);
  dirty = true;
};

let loop = function() {
  if (input.pressed(BTN_BACK)) {
    storage.write("/.crosspoint/apps/counter.txt", count);
    app.finish();
    return;
  }
  if (input.pressed(BTN_OK)) {
    count = count + 1;
    app.log(count);
    dirty = true;
  }
  if (input.pressed(BTN_LEFT)) {
    count = 0;
    dirty = true;
  }
  if (dirty) {
    dirty = false;
    draw();
  }
};
