// @name Chess
// @icon Book

let cursor = 60;
let selected = -1;
let white = true;
let wk = 60;
let wr = 56;
let bk = 4;
let dirty = true;
let fullDraw = true;
let redrawA = -1;
let redrawB = -1;

let abs = function(n) {
  return n < 0 ? 0 - n : n;
};

let pieceAt = function(i) {
  if (i === wk) return "K";
  if (i === wr) return "R";
  if (i === bk) return "k";
  return "";
};

let ownPiece = function(i) {
  if (white && (i === wk || i === wr)) return true;
  if (!white && i === bk) return true;
  return false;
};

let enemyPiece = function(i) {
  if (white && i === bk) return true;
  if (!white && (i === wk || i === wr)) return true;
  return false;
};

let clearPath = function(a, b) {
  let ar = (a - (a % 8)) / 8;
  let ac = a % 8;
  let br = (b - (b % 8)) / 8;
  let bc = b % 8;
  let dr = br - ar;
  let dc = bc - ac;
  let sr = dr === 0 ? 0 : dr > 0 ? 1 : -1;
  let sc = dc === 0 ? 0 : dc > 0 ? 1 : -1;
  let r = ar + sr;
  let c = ac + sc;
  for (let step = 0; step < 8; step = step + 1) {
    if (r === br && c === bc) return true;
    let p = r * 8 + c;
    if (pieceAt(p) !== "") return false;
    r = r + sr;
    c = c + sc;
  }
  return false;
};

let valid = function(a, b) {
  if (ownPiece(b)) return false;
  let ar = (a - (a % 8)) / 8;
  let ac = a % 8;
  let br = (b - (b % 8)) / 8;
  let bc = b % 8;
  let dr = abs(br - ar);
  let dc = abs(bc - ac);
  if (a === wk || a === bk) return dr < 2 && dc < 2;
  if (a === wr) return (dr === 0 || dc === 0) && clearPath(a, b);
  return false;
};

let movePiece = function(a, b) {
  if (a === wk) wk = b;
  if (a === wr) wr = b;
  if (a === bk) bk = b;
  selected = -1;
  white = !white;
  redrawA = a;
  redrawB = b;
};

let moveLeft = function() {
  let old = cursor;
  cursor = cursor - 1;
  if (cursor < 0) {
    cursor = 63;
  }
  redrawA = old;
  redrawB = cursor;
  dirty = true;
};

let moveRight = function() {
  let old = cursor;
  cursor = cursor + 1;
  if (cursor > 63) {
    cursor = 0;
  }
  redrawA = old;
  redrawB = cursor;
  dirty = true;
};

let moveUp = function() {
  let old = cursor;
  cursor = cursor - 8;
  if (cursor < 0) {
    cursor = cursor + 64;
  }
  redrawA = old;
  redrawB = cursor;
  dirty = true;
};

let moveDown = function() {
  let old = cursor;
  cursor = cursor + 8;
  if (cursor > 63) {
    cursor = cursor - 64;
  }
  redrawA = old;
  redrawB = cursor;
  dirty = true;
};

let drawSquare = function(i) {
  let r = (i - (i % 8)) / 8;
  let c = i % 8;
  let x = 44 + c * 55;
  let y = 140 + r * 55;
  if ((r + c) % 2 === 1) renderer.fillRect(x, y, 55, 55, true);
  if (i === cursor) renderer.rect(x + 3, y + 3, 49, 49);
  if (i === selected) renderer.rect(x + 8, y + 8, 39, 39);
  let p = pieceAt(i);
  if (p !== "") renderer.text(FONT_BODY, x + 18, y + 16, p, (r + c) % 2 === 0);
};

let drawRow0 = function() {
  drawSquare(0);
  drawSquare(1);
  drawSquare(2);
  drawSquare(3);
  drawSquare(4);
  drawSquare(5);
  drawSquare(6);
  drawSquare(7);
};

let drawRow1 = function() {
  drawSquare(8);
  drawSquare(9);
  drawSquare(10);
  drawSquare(11);
  drawSquare(12);
  drawSquare(13);
  drawSquare(14);
  drawSquare(15);
};

let drawRow2 = function() {
  drawSquare(16);
  drawSquare(17);
  drawSquare(18);
  drawSquare(19);
  drawSquare(20);
  drawSquare(21);
  drawSquare(22);
  drawSquare(23);
};

let drawRow3 = function() {
  drawSquare(24);
  drawSquare(25);
  drawSquare(26);
  drawSquare(27);
  drawSquare(28);
  drawSquare(29);
  drawSquare(30);
  drawSquare(31);
};

let drawRow4 = function() {
  drawSquare(32);
  drawSquare(33);
  drawSquare(34);
  drawSquare(35);
  drawSquare(36);
  drawSquare(37);
  drawSquare(38);
  drawSquare(39);
};

let drawRow5 = function() {
  drawSquare(40);
  drawSquare(41);
  drawSquare(42);
  drawSquare(43);
  drawSquare(44);
  drawSquare(45);
  drawSquare(46);
  drawSquare(47);
};

let drawRow6 = function() {
  drawSquare(48);
  drawSquare(49);
  drawSquare(50);
  drawSquare(51);
  drawSquare(52);
  drawSquare(53);
  drawSquare(54);
  drawSquare(55);
};

let drawRow7 = function() {
  drawSquare(56);
  drawSquare(57);
  drawSquare(58);
  drawSquare(59);
  drawSquare(60);
  drawSquare(61);
  drawSquare(62);
  drawSquare(63);
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Chess");
  renderer.text(FONT_SMALL, 24, 88, white ? "White to move" : "Black to move");
  drawRow0();
  drawRow1();
  drawRow2();
  drawRow3();
  drawRow4();
  drawRow5();
  drawRow6();
  drawRow7();
  renderer.text(FONT_SMALL, 24, renderer.height() - 42, "OK select/move, BACK exits");
  renderer.flush();
};

let onEnter = function() {
  fullDraw = true;
  dirty = true;
};

let drawIfDirty = function() {
  if (dirty) {
    dirty = false;
    if (fullDraw) {
      fullDraw = false;
      draw();
      return;
    }
    if (redrawA >= 0) drawSquare(redrawA);
    if (redrawB >= 0) drawSquare(redrawB);
    redrawA = -1;
    redrawB = -1;
    renderer.flush();
  }
};

let loop = function() {
  if (input.pressed(BTN_BACK)) {
    if (selected >= 0) {
      selected = -1;
      dirty = true;
      drawIfDirty();
      return;
    }
    app.finish();
    return;
  }
  if (input.pressed(BTN_LEFT)) {
    moveLeft();
    drawIfDirty();
    return;
  }
  if (input.pressed(BTN_RIGHT)) {
    moveRight();
    drawIfDirty();
    return;
  }
  if (input.pressed(BTN_UP)) {
    moveUp();
    drawIfDirty();
    return;
  }
  if (input.pressed(BTN_DOWN)) {
    moveDown();
    drawIfDirty();
    return;
  }
  if (input.pressed(BTN_OK)) {
    if (selected < 0) {
      if (ownPiece(cursor)) {
        selected = cursor;
        redrawA = cursor;
        redrawB = -1;
      }
    } else {
      let oldSelected = selected;
      if (cursor === selected) {
        selected = -1;
        redrawA = oldSelected;
        redrawB = -1;
      } else {
        if (valid(selected, cursor)) {
          movePiece(selected, cursor);
        } else {
          redrawA = oldSelected;
          redrawB = cursor;
        }
      }
    }
    dirty = true;
    drawIfDirty();
    return;
  }
  drawIfDirty();
};
