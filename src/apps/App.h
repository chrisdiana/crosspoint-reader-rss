#pragma once

#include <string>
#include <memory>
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h" // For UIIcon

class App {
 public:
  virtual ~App() = default;
  virtual std::string getName() const = 0;
  virtual UIIcon getIcon() const { return UIIcon::File; }
  virtual std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) = 0;
};
