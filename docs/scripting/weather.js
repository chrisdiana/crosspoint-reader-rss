// @name Weather
// @icon Wifi

let city = 0;
let status = "Press OK to fetch";
let dirty = true;

let cityName = function() {
  if (city === 0) return "New York";
  if (city === 1) return "Los Angeles";
  if (city === 2) return "Chicago";
  if (city === 3) return "London";
  return "Tokyo";
};

let cityUrl = function() {
  if (city === 0) return "https://api.open-meteo.com/v1/forecast?latitude=40.7128&longitude=-74.0060&current_weather=true";
  if (city === 1) return "https://api.open-meteo.com/v1/forecast?latitude=34.0522&longitude=-118.2437&current_weather=true";
  if (city === 2) return "https://api.open-meteo.com/v1/forecast?latitude=41.8781&longitude=-87.6298&current_weather=true";
  if (city === 3) return "https://api.open-meteo.com/v1/forecast?latitude=51.5074&longitude=-0.1278&current_weather=true";
  return "https://api.open-meteo.com/v1/forecast?latitude=35.6762&longitude=139.6503&current_weather=true";
};

let fetchWeather = function() {
  status = "Fetching...";
  draw();
  let body = http.get(cityUrl());
  if (body) {
    storage.write("/.crosspoint/apps/weather-cache.txt", body);
    status = "Saved raw weather JSON";
  } else {
    status = "Request failed";
  }
};

let draw = function() {
  renderer.clear();
  renderer.text(FONT_HEADING, 24, 28, "Weather");
  renderer.text(FONT_BODY, 34, 112, cityName());
  renderer.text(FONT_SMALL, 34, 168, status);
  renderer.text(FONT_SMALL, 34, 224, "Open-Meteo current weather");
  renderer.text(FONT_SMALL, 34, 260, "Raw JSON is cached to SD.");
  renderer.text(FONT_SMALL, 24, renderer.height() - 42, "LEFT/RIGHT city, OK fetch, BACK exits");
  renderer.flush();
};

let onEnter = function() {
  dirty = true;
};

let loop = function() {
  if (input.pressed(BTN_BACK)) { app.finish(); return; }
  if (input.pressed(BTN_LEFT)) { city = city - 1; if (city < 0) city = 4; status = "Press OK to fetch"; dirty = true; }
  if (input.pressed(BTN_RIGHT)) { city = city + 1; if (city > 4) city = 0; status = "Press OK to fetch"; dirty = true; }
  if (input.pressed(BTN_OK)) { fetchWeather(); dirty = true; }
  if (dirty) { dirty = false; draw(); }
};
