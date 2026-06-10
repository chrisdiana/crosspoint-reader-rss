#include "ScriptAppLoader.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

namespace ScriptAppLoader {

// Parse @name / @icon from the first HEADER_SCAN_BYTES of a .js file.
// Lines must start with "// @key value".
static constexpr size_t HEADER_SCAN_BYTES = 512;
static constexpr size_t NAME_BUF_LEN = 128;

UIIcon iconFromString(const char* name) {
  if (!name) return UIIcon::File;
  if (strcmp(name, "Folder") == 0) return UIIcon::Folder;
  if (strcmp(name, "Text") == 0) return UIIcon::Text;
  if (strcmp(name, "Image") == 0) return UIIcon::Image;
  if (strcmp(name, "Book") == 0) return UIIcon::Book;
  if (strcmp(name, "Recent") == 0) return UIIcon::Recent;
  if (strcmp(name, "Settings") == 0) return UIIcon::Settings;
  if (strcmp(name, "Transfer") == 0) return UIIcon::Transfer;
  if (strcmp(name, "Library") == 0) return UIIcon::Library;
  if (strcmp(name, "Wifi") == 0) return UIIcon::Wifi;
  if (strcmp(name, "Hotspot") == 0) return UIIcon::Hotspot;
  if (strcmp(name, "Bookmark") == 0) return UIIcon::Bookmark;
  return UIIcon::File;
}

// Extract filename stem (no directory, no extension) from a full path.
static std::string stemFromPath(const char* path) {
  const char* slash = strrchr(path, '/');
  const char* start = slash ? slash + 1 : path;
  const char* dot = strrchr(start, '.');
  return dot ? std::string(start, dot - start) : std::string(start);
}

static void parseMetadata(const char* buf, size_t len, std::string& outName, UIIcon& outIcon) {
  const char* p = buf;
  const char* end = buf + len;
  while (p < end) {
    // Skip whitespace
    while (p < end && (*p == ' ' || *p == '\r')) p++;
    if (p >= end || *p == '\n') { p++; continue; }

    // Only process comment lines
    if (p + 2 < end && p[0] == '/' && p[1] == '/') {
      p += 2;
      while (p < end && *p == ' ') p++;

      if (p + 5 < end && strncmp(p, "@name", 5) == 0) {
        p += 5;
        while (p < end && *p == ' ') p++;
        const char* valStart = p;
        while (p < end && *p != '\n' && *p != '\r') p++;
        if (p > valStart) outName = std::string(valStart, p - valStart);
      } else if (p + 5 < end && strncmp(p, "@icon", 5) == 0) {
        p += 5;
        while (p < end && *p == ' ') p++;
        const char* valStart = p;
        while (p < end && *p != '\n' && *p != '\r') p++;
        if (p > valStart) {
          char iconName[NAME_BUF_LEN] = {};
          size_t iconLen = std::min<size_t>(p - valStart, NAME_BUF_LEN - 1);
          memcpy(iconName, valStart, iconLen);
          outIcon = iconFromString(iconName);
        }
      }
    }

    // Advance to next line
    while (p < end && *p != '\n') p++;
    if (p < end) p++;
  }
}

std::vector<ScriptApp> loadScriptApps() {
  std::vector<ScriptApp> apps;

  auto dir = Storage.open(APPS_DIR);
  if (!dir || !dir.isDirectory()) {
    return apps;
  }

  char nameBuf[NAME_BUF_LEN] = {};
  char headerBuf[HEADER_SCAN_BYTES] = {};

  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(nameBuf, sizeof(nameBuf));

    // Only process .js files
    const size_t nameLen = strlen(nameBuf);
    if (nameLen < 4 || strcmp(nameBuf + nameLen - 3, ".js") != 0) continue;

    // Build full path
    std::string fullPath = std::string(APPS_DIR) + "/" + nameBuf;

    // Read header for metadata
    size_t bytesRead = Storage.readFileToBuffer(fullPath.c_str(), headerBuf, sizeof(headerBuf) - 1);
    if (bytesRead == 0) {
      LOG_ERR("SCRIPTS", "Could not read: %s", fullPath.c_str());
      continue;
    }
    headerBuf[bytesRead] = '\0';

    ScriptApp app;
    app.scriptPath = fullPath;
    app.name = stemFromPath(nameBuf);
    app.icon = UIIcon::File;

    parseMetadata(headerBuf, bytesRead, app.name, app.icon);

    apps.push_back(std::move(app));
    LOG_DBG("SCRIPTS", "Found: %s (%s)", app.name.c_str(), app.scriptPath.c_str());
  }

  return apps;
}

}  // namespace ScriptAppLoader
