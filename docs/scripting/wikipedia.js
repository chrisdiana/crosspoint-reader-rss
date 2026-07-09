// @name Wikipedia
// @icon Bookmark

let topic = 0;
let status = "Press OK to download";
let dirty = true;

let title = function() {
  if (topic === 0) return "E Ink";
  if (topic === 1) return "JavaScript";
  if (topic === 2) return "Chess";
  if (topic === 3) return "Sudoku";
  return "Calculator";
};

let url = function() {
  if (topic === 0) return "https://en.wikipedia.org/api/rest_v1/page/summary/E_Ink";
  if (topic === 1) return "https://en.wikipedia.org/api/rest_v1/page/summary/JavaScript";
  if (topic === 2) return "https://en.wikipedia.org/api/rest_v1/page/summary/Chess";
  if (topic === 3) return "https://en.wikipedia.org/api/rest_v1/page/summary/Sudoku";
  return "https://en.wikipedia.org/api/rest_v1/page/summary/Calculator";
};

let path = function() {
  if (topic === 0) return "/.crosspoint/apps/wiki-eink.json";
  if (topic === 1) return "/.crosspoint/apps/wiki-javascript.json";
  if (topic === 2) return "/.crosspoint/apps/wiki-chess.json";
  if (topic === 3) return "/.crosspoint/apps/wiki-sudoku.json";
  return "/.crosspoint/apps/wiki-calculator.json";
};

let fetchArticle = function() {
  status = "Fetching summary...";
  draw();
  let body = http.get(url());
  if (body) {
    storage.write(path(), body);
    status = "Saved summary JSON";
  } else {
    status = "Request failed";
  }
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Wikipedia");
  renderer.text(FONT_BODY, 34, 112, title());
  renderer.text(FONT_SMALL, 34, 168, status);
  renderer.text(FONT_SMALL, 34, 224, "Downloads REST summary JSON");
  renderer.text(FONT_SMALL, 34, 260, "Saved under /.crosspoint/apps/");
  renderer.text(FONT_SMALL, 24, renderer.height() - 42, "LEFT/RIGHT topic, OK fetch, BACK exits");
  renderer.flush();
};

let onEnter = function() {
  dirty = true;
};

let loop = function() {
  if (input.pressed(BTN_BACK)) { app.finish(); return; }
  if (input.pressed(BTN_LEFT)) { topic = topic - 1; if (topic < 0) topic = 4; status = "Press OK to download"; dirty = true; }
  if (input.pressed(BTN_RIGHT)) { topic = topic + 1; if (topic > 4) topic = 0; status = "Press OK to download"; dirty = true; }
  if (input.pressed(BTN_OK)) { fetchArticle(); dirty = true; }
  if (dirty) { dirty = false; draw(); }
};
