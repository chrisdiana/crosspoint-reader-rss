#include "TestAppActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TestAppActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void TestAppActivity::onExit() {
  Activity::onExit();
}

void TestAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void TestAppActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Draw header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  // Calculate body content bounds
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Draw centered "Hello World!" text in the content body
  const int centerY = contentTop + contentHeight / 2 - renderer.getLineHeight(UI_12_FONT_ID) / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY, "Hello World!", true, EpdFontFamily::BOLD);

  // Draw back button hint at the bottom
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), nullptr, nullptr, nullptr);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
