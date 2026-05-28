#include "TxtReaderActivity.h"

#include <algorithm>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SdCardFontSystem.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes
}  // namespace

void TxtReaderActivity::onEnter() {
  Activity::onEnter();

  if (!txt) {
    return;
  }

  sdFontSystem.ensureLoaded(renderer);
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  txt.reset();
}

void TxtReaderActivity::loop() {
  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(txt ? txt->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home (or pops to caller if it's a Wikipedia/RSS/Reddit downloaded link)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (txt && (txt->getPath().rfind("/apps/wikipedia/", 0) == 0 ||
                txt->getPath().rfind("/apps/websites/", 0) == 0 ||
                txt->getPath().rfind("/websites/", 0) == 0 ||
                txt->getPath().rfind("/apps/webbrowser/", 0) == 0 ||
                txt->getPath().rfind("/apps/rss/", 0) == 0)) {
      activityManager.popActivity();
    } else {
      onGoHome();
    }
    return;
  }

  const auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < static_cast<int>(pageOffsets.size()) - 1) {
      currentPage++;
      requestUpdate();
    } else {
      // Discover next page's offset on-the-fly
      size_t currentOffset = pageOffsets[currentPage];
      std::vector<std::string> tempLines;
      size_t nextOffset = currentOffset;
      if (loadPageAtOffset(currentOffset, tempLines, nextOffset) && nextOffset > currentOffset && nextOffset < txt->getFileSize()) {
        pageOffsets.push_back(nextOffset);
        currentPage++;
        totalPages = pageOffsets.size();
        requestUpdate();
      } else {
        // Do nothing when reaching the end of the file; don't scroll forward and don't exit to home.
      }
    }
  }
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Load saved progress
  loadProgress();

  initialized = true;
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // Prime the SD card font's advance table with this chunk's codepoints.
  // Without this, every getTextAdvanceX() call in the wrap loop below triggers
  // on-demand glyph loads through the 8-slot overflow ring buffer, which
  // thrashes for any text with more than 8 unique chars (i.e. all English),
  // floods the heap with short-lived bitmap allocations, and eventually
  // corrupts FreeRTOS state. The advance table persists across calls per
  // font, so the cost amortizes to ~ASCII-size after the first chunk.
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

  bool isHtml = FsHelpers::checkFileExtension(txt->getPath(), ".html") ||
                FsHelpers::checkFileExtension(txt->getPath(), ".htm");

  if (isHtml) {
    size_t i = 0;
    std::string cleanLine = "";
    char marker = '\7';
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    int indent = 0;
    bool lastWasSpace = true;

    // Track active tags
    bool insideH1 = false;
    bool insideH2 = false;
    bool insideH3 = false;
    bool insideBlockquote = false;

    while (i < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
      // 1. Handle HTML Comments: <!-- ... -->
      if (i + 4 <= chunkSize && 
          buffer[i] == '<' && buffer[i+1] == '!' && buffer[i+2] == '-' && buffer[i+3] == '-') {
        i += 4;
        while (i < chunkSize) {
          if (i + 3 <= chunkSize && buffer[i] == '-' && buffer[i+1] == '-' && buffer[i+2] == '>') {
            i += 3;
            break;
          }
          i++;
        }
        continue;
      }

      // 2. Handle Tags
      if (buffer[i] == '<') {
        size_t tagStart = i;
        size_t tagEnd = i;
        while (tagEnd < chunkSize && buffer[tagEnd] != '>') {
          tagEnd++;
        }
        
        if (tagEnd >= chunkSize) {
          i = chunkSize;
          break;
        }
        
        std::string tagContent(reinterpret_cast<char*>(buffer + tagStart + 1), tagEnd - tagStart - 1);
        i = tagEnd + 1; // Move past '>'
        
        std::string tagName = "";
        size_t firstSpace = tagContent.find_first_of(" \t\r\n/");
        if (firstSpace != std::string::npos) {
          tagName = tagContent.substr(0, firstSpace);
        } else {
          tagName = tagContent;
        }
        std::transform(tagName.begin(), tagName.end(), tagName.begin(), ::tolower);
        
        bool isClosing = (!tagContent.empty() && tagContent[0] == '/');
        if (isClosing && !tagName.empty() && tagName[0] == '/') {
          tagName = tagName.substr(1);
        }
        
        // If it is a style/script/head tag, skip content until the closing tag
        if (!isClosing && (tagName == "style" || tagName == "script" || tagName == "head")) {
          std::string closeTagPattern = "</" + tagName + ">";
          while (i < chunkSize) {
            if (i + closeTagPattern.length() <= chunkSize) {
              std::string testStr(reinterpret_cast<char*>(buffer + i), closeTagPattern.length());
              std::transform(testStr.begin(), testStr.end(), testStr.begin(), ::tolower);
              if (testStr == closeTagPattern) {
                i += closeTagPattern.length();
                break;
              }
            }
            i++;
          }
          continue;
        }
        
        // Process standard structural/styling tags
        if (tagName == "h1") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          insideH1 = !isClosing;
          marker = isClosing ? '\7' : '\1';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "h2") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          insideH2 = !isClosing;
          marker = isClosing ? '\7' : '\2';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "h3") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          insideH3 = !isClosing;
          marker = isClosing ? '\7' : '\3';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD;
          indent = 0;
        } else if (tagName == "blockquote") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          insideBlockquote = !isClosing;
          marker = isClosing ? '\7' : '\4';
          style = isClosing ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
          indent = isClosing ? 0 : 15;
        } else if (tagName == "li") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          if (!isClosing) {
            marker = '\5';
            indent = 15;
            cleanLine = "•  ";
          } else {
            marker = '\7';
            indent = 0;
          }
        } else if (tagName == "hr") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          std::string hrStr = "";
          hrStr += '\6';
          outLines.push_back(hrStr);
        } else if (tagName == "p" || tagName == "div" || tagName == "br") {
          if (!cleanLine.empty()) {
            wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
            cleanLine.clear();
          }
          if (insideBlockquote) {
            marker = '\4';
            style = EpdFontFamily::ITALIC;
            indent = 15;
          } else {
            marker = '\7';
            style = EpdFontFamily::REGULAR;
            indent = 0;
          }
        }
        
        lastWasSpace = true;
        continue;
      }
      
      // 3. Handle HTML character entities
      if (buffer[i] == '&') {
        size_t entEnd = i;
        while (entEnd < chunkSize && entEnd - i < 10 && buffer[entEnd] != ';') {
          entEnd++;
        }
        if (entEnd < chunkSize && buffer[entEnd] == ';') {
          std::string entity(reinterpret_cast<char*>(buffer + i + 1), entEnd - i - 1);
          int code = 0;
          bool decoded = false;
          
          if (!entity.empty() && entity[0] == '#') {
            decoded = true;
            if (entity.length() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
              // Hexadecimal
              for (size_t j = 2; j < entity.length(); j++) {
                char ch = entity[j];
                if (ch >= '0' && ch <= '9') code = code * 16 + (ch - '0');
                else if (ch >= 'a' && ch <= 'f') code = code * 16 + (ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') code = code * 16 + (ch - 'A' + 10);
              }
            } else {
              // Decimal
              for (size_t j = 1; j < entity.length(); j++) {
                char ch = entity[j];
                if (ch >= '0' && ch <= '9') {
                  code = code * 10 + (ch - '0');
                }
              }
            }
          } else {
            // Named entities
            if (entity == "nbsp") { code = 32; decoded = true; }
            else if (entity == "amp") { code = 38; decoded = true; }
            else if (entity == "lt") { code = 60; decoded = true; }
            else if (entity == "gt") { code = 62; decoded = true; }
            else if (entity == "quot") { code = 34; decoded = true; }
            else if (entity == "apos" || entity == "#39") { code = 39; decoded = true; }
            else if (entity == "ldquo") { code = 8220; decoded = true; }
            else if (entity == "rdquo") { code = 8221; decoded = true; }
            else if (entity == "lsquo") { code = 8216; decoded = true; }
            else if (entity == "rsquo") { code = 8217; decoded = true; }
            else if (entity == "ndash") { code = 8211; decoded = true; }
            else if (entity == "mdash") { code = 8212; decoded = true; }
            else if (entity == "hellip") { code = 8230; decoded = true; }
            else if (entity == "euro") { code = 8364; decoded = true; }
            else if (entity == "copy") { code = 169; decoded = true; }
            else if (entity == "reg") { code = 174; decoded = true; }
            else if (entity == "trade") { code = 8482; decoded = true; }
          }
          
          if (decoded && code > 0) {
            std::string utf8_char = "";
            if (code <= 0x7F) {
              utf8_char += static_cast<char>(code);
            } else if (code <= 0x7FF) {
              utf8_char += static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
              utf8_char += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code <= 0xFFFF) {
              utf8_char += static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
              utf8_char += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              utf8_char += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code <= 0x10FFFF) {
              utf8_char += static_cast<char>(0xF0 | ((code >> 18) & 0x07));
              utf8_char += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
              utf8_char += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              utf8_char += static_cast<char>(0x80 | (code & 0x3F));
            }
            
            for (char uc : utf8_char) {
              if (uc == ' ') {
                if (!lastWasSpace) {
                  cleanLine += ' ';
                  lastWasSpace = true;
                }
              } else {
                cleanLine += uc;
                lastWasSpace = false;
              }
            }
            i = entEnd + 1;
            continue;
          }
        }
      }

      char c = buffer[i];
      if (isspace(c)) {
        if (!lastWasSpace) {
          cleanLine += ' ';
          lastWasSpace = true;
        }
      } else {
        cleanLine += c;
        lastWasSpace = false;
      }

      i++;
    }

    if (!cleanLine.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      wrapAndPushHtmlLine(cleanLine, marker, style, indent, outLines);
      cleanLine.clear();
    }

    pos = i;
  } else {
    while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
      // Find end of line
      size_t lineEnd = pos;
      while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
        lineEnd++;
      }

      // Check if we have a complete line
      bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

      if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
        // Incomplete line and we already have some lines, stop here
        break;
      }

      // Calculate the actual length of line content in the buffer (excluding newline)
      size_t lineContentLen = lineEnd - pos;

      // Check for carriage return
      bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
      size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

      // Extract line content for display (without CR/LF)
      std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

      // Track position within this source line (in bytes from pos)
      size_t lineBytePos = 0;

      bool isMarkdown = FsHelpers::hasMarkdownExtension(txt->getPath());
      char marker = '\7'; // Default: normal text
      EpdFontFamily::Style style = EpdFontFamily::REGULAR;
      int indent = 0;
      std::string cleanLine = line;

      if (isMarkdown) {
        if (line.rfind("# ", 0) == 0) {
          marker = '\1'; // H1
          style = EpdFontFamily::BOLD;
          cleanLine = line.substr(2);
        } else if (line.rfind("## ", 0) == 0) {
          marker = '\2'; // H2
          style = EpdFontFamily::BOLD;
          cleanLine = line.substr(3);
        } else if (line.rfind("### ", 0) == 0) {
          marker = '\3'; // H3
          style = EpdFontFamily::BOLD;
          cleanLine = line.substr(4);
        } else if (line.rfind("> ", 0) == 0) {
          marker = '\4'; // Blockquote
          style = EpdFontFamily::ITALIC;
          indent = 15;
          cleanLine = line.substr(2);
        } else if (line.rfind("- ", 0) == 0) {
          marker = '\5'; // Bullet point
          indent = 15;
          cleanLine = "•  " + line.substr(2);
        } else if (line.rfind("* ", 0) == 0) {
          marker = '\5'; // Bullet point
          indent = 15;
          cleanLine = "•  " + line.substr(2);
        } else if (line == "---" || line == "***" || line == "___") {
          marker = '\6'; // Horizontal rule
          cleanLine = "";
        }
      }

      bool firstSegment = true;
      do {
        if (cleanLine.empty() && marker != '\6') {
          std::string wrapped = "";
          wrapped += marker;
          outLines.push_back(wrapped);
          break;
        }

        if (marker == '\6') {
          std::string wrapped = "";
          wrapped += marker;
          outLines.push_back(wrapped);
          break;
        }

        int currentIndent = firstSegment ? indent : (marker == '\5' ? 15 : indent);
        int maxW = viewportWidth - currentIndent;

        int lineWidth = renderer.getTextAdvanceX(cachedFontId, cleanLine.c_str(), style);

        if (lineWidth <= maxW) {
          std::string wrapped = "";
          wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
          wrapped += cleanLine;
          outLines.push_back(wrapped);
          lineBytePos = displayLen;  // Consumed entire display content
          cleanLine.clear();
          break;
        }

        // Find break point using binary search
        size_t low = 0;
        size_t high = cleanLine.length();
        size_t breakPos = 0;

        while (low <= high) {
          size_t mid = low + (high - low) / 2;
          // Make sure mid is at a UTF-8 character boundary
          while (mid > low && (cleanLine[mid] & 0xC0) == 0x80) {
            mid--;
          }

          std::string testStr = cleanLine.substr(0, mid);
          int testWidth = renderer.getTextAdvanceX(cachedFontId, testStr.c_str(), style);

          if (testWidth <= maxW) {
            breakPos = mid;
            low = mid + 1;
            // Advance low to next UTF-8 boundary
            while (low <= high && low < cleanLine.length() && (cleanLine[low] & 0xC0) == 0x80) {
              low++;
            }
          } else {
            if (mid == 0) {
              breakPos = 0;
              break;
            }
            high = mid - 1;
          }
        }

        if (breakPos == 0) {
          // Fallback: at least one character
          breakPos = 1;
          while (breakPos < cleanLine.length() && (cleanLine[breakPos] & 0xC0) == 0x80) {
            breakPos++;
          }
        }

        // Try to break at space if we are not at the end of the line
        if (breakPos < cleanLine.length()) {
          size_t spacePos = cleanLine.rfind(' ', breakPos);
          if (spacePos != std::string::npos && spacePos > 0) {
            if (spacePos > breakPos - 20 || spacePos > cleanLine.length() / 2) {
              breakPos = spacePos;
            }
          }
        }

        std::string wrapped = "";
        wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
        wrapped += cleanLine.substr(0, breakPos);
        outLines.push_back(wrapped);

        // Skip space at break point
        size_t skipChars = breakPos;
        if (breakPos < cleanLine.length() && cleanLine[breakPos] == ' ') {
          skipChars++;
        }

        size_t sourceConsumed = skipChars;
        if (firstSegment) {
          if (marker == '\1') {
            sourceConsumed = skipChars + 2;
          } else if (marker == '\2') {
            sourceConsumed = skipChars + 3;
          } else if (marker == '\3') {
            sourceConsumed = skipChars + 4;
          } else if (marker == '\4') {
            sourceConsumed = skipChars + 2;
          } else if (marker == '\5') {
            if (skipChars <= 5) {
              sourceConsumed = 2;
            } else {
              sourceConsumed = 2 + (skipChars - 5);
            }
          }
        }
        lineBytePos += sourceConsumed;

        cleanLine = cleanLine.substr(skipChars);
        firstSegment = false;
      } while (!cleanLine.empty() && static_cast<int>(outLines.size()) < linesPerPage);

      // Determine how much of the source buffer we consumed
      if (cleanLine.empty()) {
        // Fully consumed this source line, move past the newline
        pos = lineEnd + 1;
      } else {
        // Partially consumed - page is full mid-line
        // Move pos to where we stopped in the line (NOT past the line)
        pos = pos + lineBytePos;
        break;
      }
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  // Check if settings changed since initialization
  if (initialized && (cachedFontId != SETTINGS.getReaderFontId() ||
                       cachedScreenMargin != SETTINGS.screenMargin ||
                       cachedParagraphAlignment != SETTINGS.paragraphAlignment)) {
    initialized = false;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= static_cast<int>(pageOffsets.size())) currentPage = pageOffsets.size() - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    bool isFormatted = FsHelpers::hasMarkdownExtension(txt->getPath()) ||
                       FsHelpers::checkFileExtension(txt->getPath(), ".html") ||
                       FsHelpers::checkFileExtension(txt->getPath(), ".htm");

    for (const auto& rawLine : currentPageLines) {
      if (rawLine.empty()) {
        y += lineHeight;
        continue;
      }

      std::string line = rawLine;
      EpdFontFamily::Style style = EpdFontFamily::REGULAR;
      int indent = 0;
      bool isH1 = false;
      bool isHR = false;
      bool isQuote = false;

      if (isFormatted) {
        char type = line[0];
        line = line.substr(1);

        if (type == '\1') { // H1
          style = EpdFontFamily::BOLD;
          isH1 = true;
        } else if (type == '\2' || type == '\3') { // H2, H3
          style = EpdFontFamily::BOLD;
        } else if (type == '\4') { // Quote
          style = EpdFontFamily::ITALIC;
          indent = 15;
          isQuote = true;
        } else if (type == '\5') { // Bullet
          indent = 15;
        } else if (type == '\6') { // HR
          isHR = true;
        }
      }

      if (isHR) {
        int startX = cachedOrientedMarginLeft + 10;
        int endX = cachedOrientedMarginLeft + viewportWidth - 10;
        int lineY = y + lineHeight / 2;
        renderer.drawLine(startX, lineY, endX, lineY, true);
      } else if (!line.empty()) {
        int x = cachedOrientedMarginLeft + indent;

        // Apply text alignment
        switch (cachedParagraphAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), style);
            x = cachedOrientedMarginLeft + indent + (viewportWidth - indent - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), style);
            x = cachedOrientedMarginLeft + viewportWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        if (isQuote) {
          int barX = cachedOrientedMarginLeft + 5;
          renderer.fillRect(barX, y, 2, lineHeight, true);
        }

        renderer.drawText(cachedFontId, x, y, line.c_str(), true, style);

        if (isH1) {
          int lineY = y + lineHeight - 2;
          renderer.drawLine(cachedOrientedMarginLeft, lineY, cachedOrientedMarginLeft + viewportWidth, lineY, true);
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  const size_t fileSize = txt->getFileSize();
  const float progress = fileSize > 0 ? (pageOffsets[currentPage] * 100.0f) / fileSize : 0;
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = txt->getTitle();
  }

  int estimatedTotalPages = 1;
  if (pageOffsets[currentPage] > 0) {
    estimatedTotalPages = (fileSize * (currentPage + 1)) / pageOffsets[currentPage];
  } else {
    estimatedTotalPages = fileSize / 1500;
  }
  if (estimatedTotalPages < currentPage + 1) {
    estimatedTotalPages = currentPage + 1;
  }

  GUI.drawStatusBar(renderer, progress, currentPage + 1, estimatedTotalPages, title);
}

void TxtReaderActivity::saveProgress() const {
  HalFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    size_t offset = pageOffsets[currentPage];
    uint8_t data[4];
    data[0] = offset & 0xFF;
    data[1] = (offset >> 8) & 0xFF;
    data[2] = (offset >> 16) & 0xFF;
    data[3] = (offset >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::loadProgress() {
  size_t savedOffset = 0;
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      savedOffset = data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
    }
    f.close();
  }

  pageOffsets.clear();
  pageOffsets.push_back(0);

  size_t currentOffset = 0;
  const size_t fileSize = txt->getFileSize();

  while (currentOffset < savedOffset && currentOffset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = currentOffset;
    if (!loadPageAtOffset(currentOffset, tempLines, nextOffset) || nextOffset <= currentOffset) {
      break;
    }
    currentOffset = nextOffset;
    if (currentOffset < fileSize) {
      pageOffsets.push_back(currentOffset);
    }
  }

  currentPage = pageOffsets.size() - 1;
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded progress: offset %zu, page %d", savedOffset, currentPage);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;

  const size_t fileSize = txt ? txt->getFileSize() : 0;
  int estimatedTotalPages = 1;
  if (currentPage < static_cast<int>(pageOffsets.size()) && pageOffsets[currentPage] > 0) {
    estimatedTotalPages = (fileSize * (currentPage + 1)) / pageOffsets[currentPage];
  } else {
    estimatedTotalPages = fileSize / 1500;
  }
  if (estimatedTotalPages < currentPage + 1) {
    estimatedTotalPages = currentPage + 1;
  }

  info.totalPages = estimatedTotalPages;
  info.progressPercent = estimatedTotalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / estimatedTotalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}

void TxtReaderActivity::wrapAndPushHtmlLine(const std::string& line, char marker, EpdFontFamily::Style style, int indent, std::vector<std::string>& outLines) {
  std::string cleanLine = line;
  bool firstSegment = true;

  while (!cleanLine.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
    int currentIndent = firstSegment ? indent : (marker == '\5' ? 15 : indent);
    int maxW = viewportWidth - currentIndent;

    int lineWidth = renderer.getTextAdvanceX(cachedFontId, cleanLine.c_str(), style);

    if (lineWidth <= maxW) {
      std::string wrapped = "";
      wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
      wrapped += cleanLine;
      outLines.push_back(wrapped);
      break;
    }

    // Find break point using binary search
    size_t low = 0;
    size_t high = cleanLine.length();
    size_t breakPos = 0;

    while (low <= high) {
      size_t mid = low + (high - low) / 2;
      while (mid > low && (cleanLine[mid] & 0xC0) == 0x80) {
        mid--;
      }

      std::string testStr = cleanLine.substr(0, mid);
      int testWidth = renderer.getTextAdvanceX(cachedFontId, testStr.c_str(), style);

      if (testWidth <= maxW) {
        breakPos = mid;
        low = mid + 1;
        while (low <= high && low < cleanLine.length() && (cleanLine[low] & 0xC0) == 0x80) {
          low++;
        }
      } else {
        if (mid == 0) {
          breakPos = 0;
          break;
        }
        high = mid - 1;
      }
    }

    if (breakPos == 0) {
      breakPos = 1;
      while (breakPos < cleanLine.length() && (cleanLine[breakPos] & 0xC0) == 0x80) {
        breakPos++;
      }
    }

    if (breakPos < cleanLine.length()) {
      size_t spacePos = cleanLine.rfind(' ', breakPos);
      if (spacePos != std::string::npos && spacePos > 0) {
        if (spacePos > breakPos - 20 || spacePos > cleanLine.length() / 2) {
          breakPos = spacePos;
        }
      }
    }

    std::string wrapped = "";
    wrapped += (firstSegment ? marker : (marker == '\5' ? '\4' : marker));
    wrapped += cleanLine.substr(0, breakPos);
    outLines.push_back(wrapped);

    size_t skipChars = breakPos;
    if (breakPos < cleanLine.length() && cleanLine[breakPos] == ' ') {
      skipChars++;
    }

    cleanLine = cleanLine.substr(skipChars);
    firstSegment = false;
  }
}
