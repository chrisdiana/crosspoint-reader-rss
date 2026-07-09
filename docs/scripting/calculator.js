// @name Calculator
// @icon Text

let row = 1;
let col = 0;
let value = 0;
let pending = 0;
let op = 0;
let entering = false;
let hasPending = false;
let dirty = true;
let fullDraw = true;
let oldRow = 1;
let oldCol = 0;
let displayDirty = true;

let keyAt = function(r, c) {
  if (r === 0 && c === 0) return "C";
  if (r === 0 && c === 1) return "Del";
  if (r === 0 && c === 3) return "/";
  if (r === 1 && c === 0) return "7";
  if (r === 1 && c === 1) return "8";
  if (r === 1 && c === 2) return "9";
  if (r === 1 && c === 3) return "*";
  if (r === 2 && c === 0) return "4";
  if (r === 2 && c === 1) return "5";
  if (r === 2 && c === 2) return "6";
  if (r === 2 && c === 3) return "-";
  if (r === 3 && c === 0) return "1";
  if (r === 3 && c === 1) return "2";
  if (r === 3 && c === 2) return "3";
  if (r === 3 && c === 3) return "+";
  if (r === 4 && c === 0) return "0";
  if (r === 4 && c === 3) return "=";
  return "";
};

let isEmpty = function(r, c) {
  return keyAt(r, c) === "";
};

let rememberKey = function() {
  oldRow = row;
  oldCol = col;
};

let skipEmptyRight = function() {
  if (isEmpty(row, col)) col = col + 1;
  if (col > 3) col = 0;
  if (isEmpty(row, col)) col = col + 1;
  if (col > 3) col = 0;
};

let skipEmptyLeft = function() {
  if (isEmpty(row, col)) col = col - 1;
  if (col < 0) col = 3;
  if (isEmpty(row, col)) col = col - 1;
  if (col < 0) col = 3;
};

let moveLeft = function() {
  rememberKey();
  col = col - 1;
  if (col < 0) col = 3;
  skipEmptyLeft();
  dirty = true;
};

let moveRight = function() {
  rememberKey();
  col = col + 1;
  if (col > 3) col = 0;
  skipEmptyRight();
  dirty = true;
};

let moveUp = function() {
  rememberKey();
  row = row - 1;
  if (row < 0) row = 4;
  if (isEmpty(row, col)) row = row - 1;
  if (row < 0) row = 4;
  dirty = true;
};

let moveDown = function() {
  rememberKey();
  row = row + 1;
  if (row > 4) row = 0;
  if (isEmpty(row, col)) row = row + 1;
  if (row > 4) row = 0;
  dirty = true;
};

let applyOp = function() {
  if (!hasPending) {
    pending = value;
    hasPending = true;
    return;
  }
  if (op === 1) pending = pending + value;
  if (op === 2) pending = pending - value;
  if (op === 3) pending = pending * value;
  if (op === 4 && value !== 0) pending = pending / value;
  value = pending;
};

let press = function() {
  let k = keyAt(row, col);
  if (k === "C") {
    value = 0;
    pending = 0;
    op = 0;
    entering = false;
    hasPending = false;
    displayDirty = true;
    return;
  }
  if (k === "Del") {
    value = (value - (value % 10)) / 10;
    displayDirty = true;
    return;
  }
  if (k === "=") {
    applyOp();
    op = 0;
    entering = false;
    hasPending = false;
    displayDirty = true;
    return;
  }
  if (k === "+" || k === "-" || k === "*" || k === "/") {
    applyOp();
    if (k === "+") op = 1;
    if (k === "-") op = 2;
    if (k === "*") op = 3;
    if (k === "/") op = 4;
    value = 0;
    entering = false;
    displayDirty = true;
    return;
  }
  if (!entering) {
    value = 0;
    entering = true;
  }
  if (k === "0") value = value * 10;
  if (k === "1") value = value * 10 + 1;
  if (k === "2") value = value * 10 + 2;
  if (k === "3") value = value * 10 + 3;
  if (k === "4") value = value * 10 + 4;
  if (k === "5") value = value * 10 + 5;
  if (k === "6") value = value * 10 + 6;
  if (k === "7") value = value * 10 + 7;
  if (k === "8") value = value * 10 + 8;
  if (k === "9") value = value * 10 + 9;
  displayDirty = true;
};

let drawKey = function(r, c) {
  let k = keyAt(r, c);
  if (k === "") return;
  let x = 28 + c * 118;
  let y = 220 + r * 82;
  let selected = r === row && c === col;
  renderer.fillRect(x, y, 104, 60, false);
  if (selected) renderer.fillRect(x, y, 104, 60, true);
  renderer.rect(x, y, 104, 60);
  renderer.text(FONT_UI, x + 38, y + 20, k, !selected);
};

let drawDisplay = function() {
  renderer.fillRect(24, 84, renderer.width() - 48, 96, false);
  renderer.rect(24, 84, renderer.width() - 48, 96);
  renderer.text(FONT_BODY, 44, 110, value);
  if (hasPending) renderer.text(FONT_SMALL, 44, 150, pending);
};

let drawRow0 = function() {
  drawKey(0, 0);
  drawKey(0, 1);
  drawKey(0, 2);
  drawKey(0, 3);
};

let drawRow1 = function() {
  drawKey(1, 0);
  drawKey(1, 1);
  drawKey(1, 2);
  drawKey(1, 3);
};

let drawRow2 = function() {
  drawKey(2, 0);
  drawKey(2, 1);
  drawKey(2, 2);
  drawKey(2, 3);
};

let drawRow3 = function() {
  drawKey(3, 0);
  drawKey(3, 1);
  drawKey(3, 2);
  drawKey(3, 3);
};

let drawRow4 = function() {
  drawKey(4, 0);
  drawKey(4, 1);
  drawKey(4, 2);
  drawKey(4, 3);
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Calculator");
  drawDisplay();
  drawRow0();
  drawRow1();
  drawRow2();
  drawRow3();
  drawRow4();
  renderer.text(FONT_SMALL, 24, renderer.height() - 42, "Arrows move, OK press, BACK exits");
  renderer.flush();
};

let onEnter = function() {
  fullDraw = true;
  displayDirty = true;
  dirty = true;
};

let drawChanges = function() {
  if (fullDraw) {
    fullDraw = false;
    displayDirty = false;
    draw();
    return;
  }
  if (displayDirty) {
    displayDirty = false;
    drawDisplay();
  }
  drawKey(oldRow, oldCol);
  drawKey(row, col);
  renderer.flush();
};

let loop = function() {
  if (input.pressed(BTN_BACK)) {
    app.finish();
    return;
  }
  if (input.pressed(BTN_LEFT)) {
    moveLeft();
    drawChanges();
    return;
  }
  if (input.pressed(BTN_RIGHT)) {
    moveRight();
    drawChanges();
    return;
  }
  if (input.pressed(BTN_UP)) {
    moveUp();
    drawChanges();
    return;
  }
  if (input.pressed(BTN_DOWN)) {
    moveDown();
    drawChanges();
    return;
  }
  if (input.pressed(BTN_OK)) {
    rememberKey();
    press();
    dirty = true;
    drawChanges();
    return;
  }
  if (dirty) {
    dirty = false;
    drawChanges();
  }
};
