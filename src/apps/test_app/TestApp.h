#pragma once

#include "../App.h"

class TestApp : public App {
 private:
  std::string name;
  UIIcon icon;

 public:
  explicit TestApp(std::string name = "Test App", UIIcon icon = UIIcon::Text)
      : name(std::move(name)), icon(icon) {}

  std::string getName() const override { return name; }
  UIIcon getIcon() const override { return icon; }
  std::unique_ptr<Activity> createActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) override;
};
