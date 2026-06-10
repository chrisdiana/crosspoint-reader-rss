#pragma once
#include <string>
#include "components/themes/BaseTheme.h"

struct ScriptApp {
  std::string name;
  UIIcon icon = UIIcon::File;
  std::string scriptPath;
};
