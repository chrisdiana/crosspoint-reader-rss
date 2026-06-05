#include "HtmlArticleExtractor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string_view>

namespace {
constexpr size_t MAX_EXTRACTED_TEXT_BYTES = 8 * 1024;
constexpr size_t MAX_DECODE_BUFFER_BYTES = 12 * 1024;

char lowerAsciiChar(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool startsWithAt(const std::string& text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  return pos + len <= text.size() && text.compare(pos, len, prefix) == 0;
}

bool startsWithAtCaseInsensitive(std::string_view text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  if (pos + len > text.size()) return false;
  for (size_t i = 0; i < len; i++) {
    if (lowerAsciiChar(text[pos + i]) != lowerAsciiChar(prefix[i])) return false;
  }
  return true;
}

size_t findCaseInsensitive(std::string_view text, const char* needle, size_t start = 0) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) return start <= text.size() ? start : std::string::npos;
  if (needleLen > text.size() || start > text.size() - needleLen) return std::string::npos;
  for (size_t pos = start; pos <= text.size() - needleLen; pos++) {
    if (startsWithAtCaseInsensitive(text, pos, needle)) return pos;
  }
  return std::string::npos;
}

size_t findChar(std::string_view text, char needle, size_t start = 0) {
  const size_t found = text.find(needle, start);
  return found == std::string_view::npos ? std::string::npos : found;
}

void trimInPlace(std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
  if (end < value.size()) value.erase(end);
  if (start > 0) value.erase(0, start);
}

bool tagNameBoundary(std::string_view text, size_t pos) {
  return pos >= text.size() || text[pos] == '>' || text[pos] == '/' || std::isspace(static_cast<unsigned char>(text[pos]));
}

bool tagMatches(const std::string& tagLower, const char* name) {
  return startsWithAt(tagLower, 0, name) && tagNameBoundary(tagLower, strlen(name));
}

bool tagHasContentHint(const std::string& tagText) {
  const std::string lower = toLowerAscii(tagText);
  const char* hints[] = {"article", "content", "post", "story", "entry", "body", "main"};
  for (const char* hint : hints) {
    if (lower.find(hint) != std::string::npos) return true;
  }
  return false;
}

void appendUtf8(std::string& out, unsigned long cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string decodeJsonString(const std::string& value) {
  std::string out;
  out.reserve(std::min<size_t>(value.size(), 1024));
  for (size_t i = 0; i < value.size(); i++) {
    if (out.size() >= MAX_DECODE_BUFFER_BYTES) break;
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out.push_back(value[i]);
      continue;
    }

    const char next = value[++i];
    switch (next) {
      case '"':
      case '\\':
      case '/':
        out.push_back(next);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\n');
        break;
      case 't':
        out.push_back(' ');
        break;
      case 'u': {
        if (i + 4 <= value.size()) {
          const std::string hex = value.substr(i + 1, 4);
          char* end = nullptr;
          const unsigned long cp = strtoul(hex.c_str(), &end, 16);
          if (end && *end == '\0') appendUtf8(out, cp);
          i += 4;
        }
        break;
      }
      default:
        out.push_back(next);
        break;
    }
  }
  return out;
}

std::string decodeEntities(const std::string& text) {
  std::string out;
  out.reserve(std::min<size_t>(text.size(), 1024));
  for (size_t i = 0; i < text.size(); i++) {
    if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    if (text[i] != '&') {
      out.push_back(text[i]);
      continue;
    }

    const size_t semi = text.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
      out.push_back(text[i]);
      continue;
    }

    const std::string entity = text.substr(i + 1, semi - i - 1);
    const std::string lower = toLowerAscii(entity);
    if (lower == "amp") {
      out.push_back('&');
    } else if (lower == "lt") {
      out.push_back('<');
    } else if (lower == "gt") {
      out.push_back('>');
    } else if (lower == "quot") {
      out.push_back('"');
    } else if (lower == "apos" || lower == "#39") {
      out.push_back('\'');
    } else if (lower == "nbsp") {
      out.push_back(' ');
    } else if (!lower.empty() && lower[0] == '#') {
      char* end = nullptr;
      const int base = lower.size() > 2 && lower[1] == 'x' ? 16 : 10;
      const char* start = lower.c_str() + (base == 16 ? 2 : 1);
      const unsigned long cp = strtoul(start, &end, base);
      if (end && *end == '\0') {
        appendUtf8(out, cp);
      }
    } else {
      out.push_back('&');
      out += entity;
      out.push_back(';');
    }
    i = semi;
  }
  return out;
}

std::string extractJsonStringValue(const std::string& html, const char* key) {
  const std::string quotedKey = std::string("\"") + key + "\"";
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, quotedKey.c_str(), pos)) != std::string::npos) {
    const size_t colon = html.find(':', pos + quotedKey.size());
    if (colon == std::string::npos) return {};
    size_t valueStart = colon + 1;
    while (valueStart < html.size() && std::isspace(static_cast<unsigned char>(html[valueStart]))) valueStart++;
    if (valueStart >= html.size() || html[valueStart] != '"') {
      pos = colon + 1;
      continue;
    }

    std::string encoded;
    encoded.reserve(1024);
    bool escaped = false;
    for (size_t i = valueStart + 1; i < html.size(); i++) {
      if (encoded.size() >= MAX_DECODE_BUFFER_BYTES) return decodeJsonString(encoded);
      const char c = html[i];
      if (escaped) {
        encoded.push_back('\\');
        encoded.push_back(c);
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        return decodeJsonString(encoded);
      } else {
        encoded.push_back(c);
      }
    }
    return {};
  }
  return {};
}

bool hasOddTrailingBackslashes(const std::string& value) {
  size_t count = 0;
  for (size_t i = value.size(); i > 0 && value[i - 1] == '\\'; i--) count++;
  return count % 2 == 1;
}

std::string htmlToText(std::string_view html);

std::string extractEntityEncodedJsonStringValue(const std::string& html, const char* key) {
  const std::string quotedKey = std::string("&quot;") + key + "&quot;";
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, quotedKey.c_str(), pos)) != std::string::npos) {
    const size_t colon = html.find(':', pos + quotedKey.size());
    if (colon == std::string::npos) break;
    const size_t valueStart = findCaseInsensitive(html, "&quot;", colon + 1);
    if (valueStart == std::string::npos) break;

    std::string encoded;
    encoded.reserve(1024);
    size_t i = valueStart + 6;
    for (; i < html.size();) {
      if (encoded.size() >= MAX_DECODE_BUFFER_BYTES) {
        const std::string jsonString = decodeEntities(encoded);
        const std::string decoded = decodeJsonString(jsonString);
        if (htmlToText(decoded).size() >= 120) return decoded;
        break;
      }
      if (startsWithAtCaseInsensitive(html, i, "&quot;") && !hasOddTrailingBackslashes(encoded)) {
        const std::string jsonString = decodeEntities(encoded);
        const std::string decoded = decodeJsonString(jsonString);
        if (htmlToText(decoded).size() >= 120) return decoded;
        i += 6;
        break;
      }
      if (startsWithAtCaseInsensitive(html, i, "&quot;")) {
        encoded += "&quot;";
        i += 6;
      } else {
        encoded.push_back(html[i]);
        i++;
      }
    }

    if (i >= html.size() && encoded.size() >= 120) {
      const std::string jsonString = decodeEntities(encoded);
      const std::string decoded = decodeJsonString(jsonString);
      if (htmlToText(decoded).size() >= 120) return decoded;
    }
    pos = i;
  }
  return {};
}

std::string_view largestElementContent(std::string_view html, const char* tag, bool requireContentHint = false) {
  const std::string open = std::string("<") + tag;
  const std::string close = std::string("</") + tag + ">";
  std::string_view best;
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, open.c_str(), pos)) != std::string::npos) {
    const size_t nameEnd = pos + open.size();
    if (!tagNameBoundary(html, nameEnd)) {
      pos = nameEnd;
      continue;
    }
    const size_t startEnd = findChar(html, '>', pos);
    if (startEnd == std::string::npos) break;
    const std::string tagText(html.substr(pos, startEnd - pos + 1));
    if (requireContentHint && !tagHasContentHint(tagText)) {
      pos = startEnd + 1;
      continue;
    }

    const size_t end = findCaseInsensitive(html, close.c_str(), startEnd + 1);
    if (end == std::string::npos || end <= startEnd) {
      pos = startEnd + 1;
      continue;
    }
    const std::string_view content = html.substr(startEnd + 1, end - startEnd - 1);
    if (content.size() > best.size()) best = content;
    pos = end + close.size();
  }
  return best;
}

void appendSpace(std::string& out) {
  if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) return;
  if (!out.empty() && out.back() != ' ' && out.back() != '\n') out.push_back(' ');
}

void appendBreak(std::string& out) {
  if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) return;
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.empty()) return;
  if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') return;
  if (out.back() != '\n') out.push_back('\n');
  out.push_back('\n');
}

std::string htmlToText(std::string_view html) {
  std::string out;
  out.reserve(std::min<size_t>(html.size(), 2048));
  bool lastSpace = false;

  for (size_t i = 0; i < html.size(); i++) {
    if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    const char c = html[i];
    if (c == '<') {
      const size_t tagEnd = findChar(html, '>', i + 1);
      if (tagEnd == std::string::npos) break;
      const std::string tagLower = toLowerAscii(std::string(html.substr(i + 1, std::min<size_t>(tagEnd - i - 1, 32))));
      const char* skipTags[] = {"script", "style", "noscript", "svg", "nav", "footer"};
      bool skipped = false;
      for (const char* skipTag : skipTags) {
        if (tagMatches(tagLower, skipTag)) {
          const std::string close = std::string("</") + skipTag;
          const size_t closeStart = findCaseInsensitive(html, close.c_str(), tagEnd + 1);
          if (closeStart != std::string::npos) {
          const size_t closeEnd = findChar(html, '>', closeStart);
            i = closeEnd == std::string::npos ? html.size() : closeEnd;
          } else {
            i = tagEnd;
          }
          appendSpace(out);
          lastSpace = true;
          skipped = true;
          break;
        }
      }
      if (skipped) continue;

      if (tagMatches(tagLower, "p") || startsWithAt(tagLower, 0, "/p") || tagMatches(tagLower, "br") ||
          tagMatches(tagLower, "li") || startsWithAt(tagLower, 0, "/div") || tagMatches(tagLower, "h1") ||
          tagMatches(tagLower, "h2") || tagMatches(tagLower, "h3")) {
        appendBreak(out);
        lastSpace = false;
      } else {
        appendSpace(out);
        lastSpace = true;
      }
      i = tagEnd;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!lastSpace) {
        out.push_back(' ');
        lastSpace = true;
      }
    } else {
      out.push_back(c);
      lastSpace = false;
    }
  }

  std::string decoded = decodeEntities(out);
  std::string normalized;
  normalized.reserve(std::min<size_t>(decoded.size(), 1024));
  int consecutiveNewlines = 0;
  bool spacePending = false;
  for (char c : decoded) {
    if (normalized.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    if (c == '\n') {
      while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
      consecutiveNewlines++;
      if (consecutiveNewlines <= 2) normalized.push_back('\n');
      spacePending = false;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      if (!normalized.empty() && normalized.back() != '\n') spacePending = true;
    } else {
      if (spacePending && !normalized.empty()) normalized.push_back(' ');
      normalized.push_back(c);
      consecutiveNewlines = 0;
      spacePending = false;
    }
  }
  trimInPlace(normalized);
  return normalized;
}

std::string paragraphText(std::string_view html) {
  std::string out;
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, "<p", pos)) != std::string::npos) {
    if (out.size() >= MAX_EXTRACTED_TEXT_BYTES) break;
    const size_t nameEnd = pos + 2;
    if (!tagNameBoundary(html, nameEnd)) {
      pos = nameEnd;
      continue;
    }

    const size_t startEnd = findChar(html, '>', pos);
    if (startEnd == std::string::npos) break;
    const size_t end = findCaseInsensitive(html, "</p>", startEnd + 1);
    if (end == std::string::npos) {
      pos = startEnd + 1;
      continue;
    }

    const std::string text = htmlToText(html.substr(startEnd + 1, end - startEnd - 1));
    // Skip tiny boilerplate/link-only paragraphs, but keep short quoted leads
    // once they are part of a larger paragraph set.
    if (text.size() >= 30 || (!out.empty() && text.size() >= 15)) {
      if (!out.empty()) out += "\n\n";
      out.append(text, 0, MAX_EXTRACTED_TEXT_BYTES - std::min(out.size(), MAX_EXTRACTED_TEXT_BYTES));
    }
    pos = end + 4;
  }
  trimInPlace(out);
  return out;
}
}  // namespace

std::string HtmlArticleExtractor::extractReadableText(const std::string& html) {
  if (html.empty()) return {};

  const std::string jsonArticleBody = extractJsonStringValue(html, "articlebody");
  if (jsonArticleBody.size() >= 120) return htmlToText(jsonArticleBody);

  const std::string encodedHtmlPayload = extractEntityEncodedJsonStringValue(html, "html");
  if (encodedHtmlPayload.size() >= 120) {
    const std::string text = htmlToText(encodedHtmlPayload);
    if (text.size() >= 120) return text;
  }

  std::string_view htmlView(html);
  std::string_view content = largestElementContent(htmlView, "article");
  if (content.empty()) content = largestElementContent(htmlView, "main");
  if (content.empty()) content = largestElementContent(htmlView, "section", true);
  if (content.empty()) content = largestElementContent(htmlView, "div", true);
  if (content.empty()) content = largestElementContent(htmlView, "body");
  if (content.empty()) content = htmlView;

  std::string text = htmlToText(content);
  if (text.size() >= 120) return text;

  std::string_view body = largestElementContent(htmlView, "body");
  if (body.empty()) body = htmlView;
  text = paragraphText(body);
  return text.size() >= 120 ? text : std::string{};
}
