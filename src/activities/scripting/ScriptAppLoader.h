#pragma once
#include <vector>
#include "ScriptApp.h"

namespace ScriptAppLoader {

constexpr char APPS_DIR[] = "/.crosspoint/apps";
constexpr char SCRIPT_EXT[] = ".js";

// Scan APPS_DIR for *.js files and return metadata parsed from leading comment headers.
// Each file may declare:
//   // @name My App
//   // @icon Folder
// Missing fields fall back to filename (sans extension) and UIIcon::File.
std::vector<ScriptApp> loadScriptApps();

// Parse UIIcon from a string name matching the UIIcon enum. Returns UIIcon::File on no-match.
UIIcon iconFromString(const char* name);

}  // namespace ScriptAppLoader
