// @name Sudoku
// @icon Text

let cursor = 0;
let edit = false;
let pick = 0;
let c0 = 0;
let c1 = 0;
let c2 = 0;
let c3 = 0;
let c4 = 0;
let c5 = 0;
let c6 = 0;
let c7 = 0;
let dirty = true;

let solution = function(i) {
  if (i === 0) return 1; if (i === 1) return 2; if (i === 2) return 3; if (i === 3) return 4;
  if (i === 4) return 3; if (i === 5) return 4; if (i === 6) return 1; if (i === 7) return 2;
  if (i === 8) return 2; if (i === 9) return 1; if (i === 10) return 4; if (i === 11) return 3;
  return i === 12 ? 4 : i === 13 ? 3 : i === 14 ? 2 : 1;
};

let clue = function(i) {
  if (i === 0) return 1;
  if (i === 3) return 4;
  if (i === 5) return 4;
  if (i === 6) return 1;
  if (i === 9) return 1;
  if (i === 10) return 4;
  if (i === 12) return 4;
  if (i === 15) return 1;
  return 0;
};

let slotFor = function(i) {
  if (i === 1) return c0;
  if (i === 2) return c1;
  if (i === 4) return c2;
  if (i === 7) return c3;
  if (i === 8) return c4;
  if (i === 11) return c5;
  if (i === 13) return c6;
  if (i === 14) return c7;
  return clue(i);
};

let setSlot = function(i, v) {
  if (i === 1) c0 = v;
  if (i === 2) c1 = v;
  if (i === 4) c2 = v;
  if (i === 7) c3 = v;
  if (i === 8) c4 = v;
  if (i === 11) c5 = v;
  if (i === 13) c6 = v;
  if (i === 14) c7 = v;
};

let isClue = function(i) {
  return clue(i) > 0;
};

let solved = function() {
  let ok = true;
  for (let i = 0; i < 16; i = i + 1) {
    if (slotFor(i) !== solution(i)) ok = false;
  }
  return ok;
};

let drawCell = function(i) {
  let r = (i - (i % 4)) / 4;
  let c = i % 4;
  let x = 74 + c * 92;
  let y = 150 + r * 92;
  if (i === cursor) renderer.fillRect(x + 2, y + 2, 88, 88, true);
  renderer.rect(x, y, 92, 92);
  let v = slotFor(i);
  if (v > 0) renderer.text(FONT_HEADING, x + 34, y + 26, v, i !== cursor);
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Sudoku");
  renderer.text(FONT_SMALL, 24, 88, "4x4 port for the JS SDK");
  for (let i = 0; i < 16; i = i + 1) drawCell(i);
  if (edit) {
    renderer.text(FONT_BODY, 86, 548, "Pick:");
    renderer.text(FONT_HEADING, 176, 538, pick);
    renderer.text(FONT_SMALL, 24, renderer.height() - 42, "LEFT/RIGHT value, OK set, BACK cancel");
  } else {
    if (solved()) renderer.text(FONT_BODY, 138, 548, "Solved");
    renderer.text(FONT_SMALL, 24, renderer.height() - 42, "Arrows move, OK edit, BACK exits");
  }
  renderer.flush();
};

let onEnter = function() {
  dirty = true;
};

let loop = function() {
  if (edit) {
    if (input.pressed(BTN_BACK)) { edit = false; dirty = true; return; }
    if (input.pressed(BTN_LEFT)) { pick = pick - 1; if (pick < 0) pick = 4; dirty = true; }
    if (input.pressed(BTN_RIGHT)) { pick = pick + 1; if (pick > 4) pick = 0; dirty = true; }
    if (input.pressed(BTN_OK)) { setSlot(cursor, pick); edit = false; dirty = true; }
  } else {
    if (input.pressed(BTN_BACK)) { app.finish(); return; }
    if (input.pressed(BTN_LEFT)) { cursor = cursor - 1; if (cursor < 0) cursor = 15; dirty = true; }
    if (input.pressed(BTN_RIGHT)) { cursor = cursor + 1; if (cursor > 15) cursor = 0; dirty = true; }
    if (input.pressed(BTN_UP)) { cursor = cursor - 4; if (cursor < 0) cursor = cursor + 16; dirty = true; }
    if (input.pressed(BTN_DOWN)) { cursor = cursor + 4; if (cursor > 15) cursor = cursor - 16; dirty = true; }
    if (input.pressed(BTN_OK) && !isClue(cursor)) { pick = slotFor(cursor); edit = true; dirty = true; }
  }
  if (dirty) { dirty = false; draw(); }
};
