// @name Dice & 8-Ball
// @icon File

let mode = 0;
let seed = 12345;
let d6 = 1;
let d20 = 1;
let arrow = 0;
let ball = 0;
let dirty = true;

let rand = function(max) {
  seed = (seed * 1103515245 + 12345) % 2147483647;
  storage.write("/.crosspoint/apps/dice-seed.txt", seed);
  return seed % max;
};

let roll = function() {
  if (mode === 0) d6 = rand(6) + 1;
  if (mode === 1) d20 = rand(20) + 1;
  if (mode === 2) arrow = rand(8);
  if (mode === 3) ball = rand(8);
};

let drawTabs = function() {
  renderer.text(FONT_SMALL, 26, 82, mode === 0 ? "[D6]" : "D6");
  renderer.text(FONT_SMALL, 126, 82, mode === 1 ? "[D20]" : "D20");
  renderer.text(FONT_SMALL, 236, 82, mode === 2 ? "[Arrow]" : "Arrow");
  renderer.text(FONT_SMALL, 370, 82, mode === 3 ? "[8-Ball]" : "8-Ball");
};

let answer = function(n) {
  if (n === 0) return "It is certain";
  if (n === 1) return "Yes";
  if (n === 2) return "Most likely";
  if (n === 3) return "Ask again later";
  if (n === 4) return "Cannot predict now";
  if (n === 5) return "Do not count on it";
  if (n === 6) return "My reply is no";
  return "Very doubtful";
};

let arrowText = function(n) {
  if (n === 0) return "North";
  if (n === 1) return "North East";
  if (n === 2) return "East";
  if (n === 3) return "South East";
  if (n === 4) return "South";
  if (n === 5) return "South West";
  if (n === 6) return "West";
  return "North West";
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Dice & 8-Ball");
  drawTabs();
  renderer.rect(28, 130, renderer.width() - 56, 430);
  if (mode === 0) {
    renderer.text(FONT_HEADING, 214, 260, d6);
    renderer.text(FONT_SMALL, 166, 342, "D6 result");
  }
  if (mode === 1) {
    renderer.text(FONT_HEADING, 210, 260, d20);
    renderer.text(FONT_SMALL, 166, 342, "D20 result");
  }
  if (mode === 2) {
    renderer.text(FONT_HEADING, 140, 250, arrowText(arrow));
    renderer.text(FONT_SMALL, 160, 342, "Arrow direction");
  }
  if (mode === 3) {
    renderer.text(FONT_BODY, 76, 250, answer(ball));
    renderer.text(FONT_SMALL, 166, 342, "Magic 8-Ball");
  }
  renderer.text(FONT_SMALL, 24, renderer.height() - 42, "LEFT/RIGHT mode, OK roll, BACK exits");
  renderer.flush();
};

let onEnter = function() {
  seed = storage.readNumber("/.crosspoint/apps/dice-seed.txt", 12345);
  roll();
  dirty = true;
};

let loop = function() {
  if (input.pressed(BTN_BACK)) { app.finish(); return; }
  if (input.pressed(BTN_LEFT)) { mode = mode - 1; if (mode < 0) mode = 3; dirty = true; }
  if (input.pressed(BTN_RIGHT)) { mode = mode + 1; if (mode > 3) mode = 0; dirty = true; }
  if (input.pressed(BTN_OK)) { roll(); dirty = true; }
  if (dirty) { dirty = false; draw(); }
};
