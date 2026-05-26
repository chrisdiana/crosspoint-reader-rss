#pragma once

#include "activities/Activity.h"
#include <string>

class TestAppActivity final : public Activity {
 private:
  std::string title;

 public:
  explicit TestAppActivity(std::string title, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TestApp", renderer, mappedInput), title(std::move(title)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
