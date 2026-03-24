#include "ini.hh"

#include "string.hh"

#include <cctype>
#include <iterator>
#include <sstream>
#include <utility>

namespace oro::runtime::INI {
  using runtime::InputFileStream;
  using runtime::String;
  using runtime::StringStream;
  using runtime::Vector;
  using runtime::Error;

  namespace detail {
    inline bool isWhitespace (char c) {
      return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    inline bool isHexDigit (char c) {
      return (c >= '0' && c <= '9')
        || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
    }

    inline int hexValue (char c) {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }

      if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
      }

      return 10 + (c - 'A');
    }

    String ltrimCopy (const String& value) {
      size_t index = 0;
      while (index < value.size() && isWhitespace(value[index])) {
        ++index;
      }
      return value.substr(index);
    }

    String rtrimCopy (const String& value) {
      if (value.empty()) {
        return value;
      }

      size_t index = value.size() - 1;

      while (index < value.size() && isWhitespace(value[index])) {
        if (index == 0) {
          return "";
        }
        --index;
      }

      return value.substr(0, index + 1);
    }

    String stripBom (const String& value) {
      if (value.size() >= 3
          && static_cast<unsigned char>(value[0]) == 0xEF
          && static_cast<unsigned char>(value[1]) == 0xBB
          && static_cast<unsigned char>(value[2]) == 0xBF) {
        return value.substr(3);
      }

      if (value.size() >= 2) {
        const auto first = static_cast<unsigned char>(value[0]);
        const auto second = static_cast<unsigned char>(value[1]);
        if ((first == 0xFE && second == 0xFF) || (first == 0xFF && second == 0xFE)) {
          return value.substr(2);
        }
      }

      return value;
    }

    bool isEscaped (const String& value, size_t index) {
      if (index == 0 || index > value.size()) {
        return false;
      }

      size_t count = 0;
      size_t cursor = index;

      while (cursor > 0) {
        --cursor;
        if (value[cursor] == '\\') {
          ++count;
        } else {
          break;
        }
      }

      return (count % 2) == 1;
    }

    bool hasTerminatingQuote (const String& value, char quote) {
      for (size_t i = 1; i < value.size(); ++i) {
        if (value[i] == quote && !isEscaped(value, i)) {
          return true;
        }
      }
      return false;
    }

    size_t findKeyValueSeparator (const String& line) {
      bool insideQuotes = false;
      char quote = '\0';

      for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if ((c == '"' || c == '\'') && !isEscaped(line, i)) {
          if (insideQuotes && c == quote) {
            insideQuotes = false;
            quote = '\0';
          } else if (!insideQuotes) {
            insideQuotes = true;
            quote = c;
          }
          continue;
        }

        if (!insideQuotes && (c == '=' || c == ':')) {
          return i;
        }
      }

      return String::npos;
    }

    size_t findClosingBracket (const String& line, size_t openingIndex) {
      for (size_t i = openingIndex + 1; i < line.size(); ++i) {
        if (line[i] == ']' && !isEscaped(line, i)) {
          return i;
        }
      }
      return String::npos;
    }

    String removeInlineComment (const String& source) {
      bool insideQuotes = false;
      char quote = '\0';

      for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];

        if ((c == '"' || c == '\'') && !isEscaped(source, i)) {
          if (insideQuotes && c == quote) {
            insideQuotes = false;
            quote = '\0';
          } else if (!insideQuotes) {
            insideQuotes = true;
            quote = c;
          }
          continue;
        }

        if (!insideQuotes && (c == ';' || c == '#')) {
          return rtrimCopy(source.substr(0, i));
        }
      }

      return rtrimCopy(source);
    }

    String unescape (const String& source) {
      String result;
      result.reserve(source.size());

      bool escape = false;
      for (const char c : source) {
        if (escape) {
          switch (c) {
            case '\\':
            case ';':
            case '#':
            case '[':
            case ']':
            case '"':
            case '\'':
            case '=':
            case ':':
              result.push_back(c);
              break;
            default:
              result.push_back('\\');
              result.push_back(c);
              break;
          }
          escape = false;
          continue;
        }

        if (c == '\\') {
          escape = true;
          continue;
        }

        result.push_back(c);
      }

      if (escape) {
        result.push_back('\\');
      }

      return string::trim(result);
    }

    String decodeQuotedValue (const String& source) {
      if (source.empty()) {
        return source;
      }

      const char quote = source.front();
      String body = source.substr(1, source.size() - 2);

      String result;
      result.reserve(body.size());

      bool escape = false;
      for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];

        if (escape) {
          escape = false;
          if (quote == '\'') {
            switch (c) {
              case '\\': result.push_back('\\'); break;
              case '\'': result.push_back('\''); break;
              default:
                result.push_back('\\');
                result.push_back(c);
                break;
            }
            continue;
          }

          switch (c) {
            case '\\': result.push_back('\\'); break;
            case '"': result.push_back('"'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case '0': result.push_back('\0'); break;
            case 'x': {
              if (i + 2 < body.size()
                  && isHexDigit(body[i + 1])
                  && isHexDigit(body[i + 2])) {
                const int upper = hexValue(body[i + 1]);
                const int lower = hexValue(body[i + 2]);
                const char value = static_cast<char>((upper << 4) | lower);
                result.push_back(value);
                i += 2;
              } else {
                result.push_back('x');
              }
              break;
            }
            default:
              result.push_back(c);
              break;
          }
          continue;
        }

        if (c == '\\') {
          escape = true;
          continue;
        }

        result.push_back(c);
      }

      if (escape) {
        result.push_back('\\');
      }

      return result;
    }

    bool needsQuoting (const String& value) {
      if (value.empty()) {
        return true;
      }

      if (isWhitespace(value.front()) || isWhitespace(value.back())) {
        return true;
      }

      for (const char c : value) {
        if (c == '\n' || c == '\r' || c == '\t' || c == '"' || c == ';' || c == '#' || c == '=') {
          return true;
        }
      }

      return false;
    }

    String escapeForSerialization (const String& value) {
      String result;
      result.reserve(value.size() + 2);

      if (!needsQuoting(value)) {
        return value;
      }

      result.push_back('"');
      for (const char c : value) {
        switch (c) {
          case '\\': result.append("\\\\"); break;
          case '"': result.append("\\\""); break;
          case '\n': result.append("\\n"); break;
          case '\r': result.append("\\r"); break;
          case '\t': result.append("\\t"); break;
          case '\0': result.append("\\0"); break;
          default: result.push_back(c); break;
        }
      }
      result.push_back('"');
      return result;
    }

    bool endsWith (const String& value, const String& suffix) {
      if (suffix.size() > value.size()) {
        return false;
      }
      return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    String joinList (const Value::List& list, bool useNewlines) {
      if (list.empty()) {
        return "";
      }

      StringStream stream;
      for (size_t i = 0; i < list.size(); ++i) {
        if (useNewlines) {
          stream << list[i] << "\n";
        } else {
          if (i > 0) {
            stream << " ";
          }
          stream << list[i];
        }
      }
      return stream.str();
    }

    String sectionPrefix (const String& section, const String& separator) {
      if (section.empty()) {
        return "";
      }
      String normalized = string::replace(section, "\\.", separator);
      return normalized + separator;
    }

    size_t firstNonWhitespaceIndex (const String& value) {
      size_t index = 0;
      while (index < value.size() && isWhitespace(value[index])) {
        ++index;
      }
      return index;
    }
  } // namespace detail

  ParseError::ParseError (const String& message, size_t line, size_t column)
    : Error(message),
      errorLine(line),
      errorColumn(column) {
  }

  size_t ParseError::line () const {
    return errorLine;
  }

  size_t ParseError::column () const {
    return errorColumn;
  }

  Value::Value ()
    : valueType(ValueType::Scalar),
      scalar(""),
      list() {
  }

  Value::Value (const String& value)
    : valueType(ValueType::Scalar),
      scalar(value),
      list() {
  }

  Value::Value (String&& value)
    : valueType(ValueType::Scalar),
      scalar(std::move(value)),
      list() {
  }

  Value::Value (const List& values)
    : valueType(ValueType::List),
      scalar(),
      list(values) {
  }

  Value::Value (List&& values)
    : valueType(ValueType::List),
      scalar(),
      list(std::move(values)) {
  }

  ValueType Value::type () const {
    return valueType;
  }

  bool Value::isScalar () const {
    return valueType == ValueType::Scalar;
  }

  bool Value::isList () const {
    return valueType == ValueType::List;
  }

  const String& Value::asString () const {
    if (!isScalar()) {
      throw Error("INI value is not scalar");
    }
    return scalar;
  }

  const Value::List& Value::asList () const {
    if (!isList()) {
      throw Error("INI value is not a list");
    }
    return list;
  }

  void Value::set (const String& value) {
    valueType = ValueType::Scalar;
    scalar = value;
    list.clear();
  }

  void Value::set (String&& value) {
    valueType = ValueType::Scalar;
    scalar = std::move(value);
    list.clear();
  }

  void Value::setList (const List& values) {
    valueType = ValueType::List;
    list = values;
    scalar.clear();
  }

  void Value::setList (List&& values) {
    valueType = ValueType::List;
    list = std::move(values);
    scalar.clear();
  }

  void Value::append (const String& value) {
    if (!isList()) {
      List converted;
      converted.emplace_back(std::move(scalar));
      setList(std::move(converted));
    }
    list.push_back(value);
  }

  void Value::append (String&& value) {
    if (!isList()) {
      List converted;
      converted.emplace_back(std::move(scalar));
      setList(std::move(converted));
    }
    list.push_back(std::move(value));
  }

  Section::Section ()
    : sectionName(""),
      values(),
      insertion(),
      explicitLists() {
  }

  Section::Section (const String& name)
    : sectionName(name),
      values(),
      insertion(),
      explicitLists() {
  }

  const String& Section::name () const {
    return sectionName;
  }

  bool Section::empty () const {
    return values.empty();
  }

  bool Section::has (const String& key) const {
    return values.find(key) != values.end();
  }

  Value& Section::set (const String& key, const String& value, bool append, bool forceList) {
    auto iterator = values.find(key);

    if (iterator == values.end()) {
      if (append || forceList) {
        Value::List list = { value };
        auto [it, inserted] = values.emplace(key, Value(std::move(list)));
        if (inserted) {
          insertion.push_back(key);
          if (forceList) {
            explicitLists.insert(key);
          }
        }
        return it->second;
      }

      auto [it, inserted] = values.emplace(key, Value(value));
      if (inserted) {
        insertion.push_back(key);
        if (forceList) {
          explicitLists.insert(key);
          Value::List list = { value };
          it->second.setList(std::move(list));
        }
      }
      return it->second;
    }

    if (!append) {
      if (forceList) {
        explicitLists.insert(key);
        Value::List list = { value };
        iterator->second.setList(std::move(list));
      } else {
        explicitLists.erase(key);
        iterator->second.set(value);
      }
      return iterator->second;
    }

    if (forceList) {
      explicitLists.insert(key);
    }

    if (append || iterator->second.isList()) {
      iterator->second.append(value);
    } else {
      iterator->second.set(value);
    }

    return iterator->second;
  }

  Value& Section::set (const String& key, String&& value, bool append, bool forceList) {
    auto iterator = values.find(key);

    if (iterator == values.end()) {
      if (append || forceList) {
        Value::List list;
        list.emplace_back(std::move(value));
        auto [it, inserted] = values.emplace(key, Value(std::move(list)));
        if (inserted) {
          insertion.push_back(key);
          if (forceList) {
            explicitLists.insert(key);
          }
        }
        return it->second;
      }

      auto [it, inserted] = values.emplace(key, Value(std::move(value)));
      if (inserted) {
        insertion.push_back(key);
        if (forceList) {
          explicitLists.insert(key);
        }
      }
      return it->second;
    }

    if (!append) {
      if (forceList) {
        explicitLists.insert(key);
        Value::List list;
        list.emplace_back(std::move(value));
        iterator->second.setList(std::move(list));
      } else {
        explicitLists.erase(key);
        iterator->second.set(std::move(value));
      }
      return iterator->second;
    }

    if (forceList) {
      explicitLists.insert(key);
    }

    if (append || iterator->second.isList()) {
      iterator->second.append(std::move(value));
    } else {
      iterator->second.set(std::move(value));
    }

    return iterator->second;
  }

  Value& Section::value (const String& key) {
    auto iterator = values.find(key);
    if (iterator == values.end()) {
      throw Error("INI key not found: " + key);
    }
    return iterator->second;
  }

  const Value& Section::value (const String& key) const {
    auto iterator = values.find(key);
    if (iterator == values.end()) {
      throw Error("INI key not found: " + key);
    }
    return iterator->second;
  }

  const runtime::Map<String, Value>& Section::entries () const {
    return values;
  }

  const Vector<String>& Section::order () const {
    return insertion;
  }

  bool Section::isExplicitList (const String& key) const {
    return explicitLists.find(key) != explicitLists.end();
  }

  Document::Document ()
    : sectionMap(),
      orderedSections() {
    sectionMap.emplace("", Section(""));
    orderedSections.push_back("");
  }

  bool Document::hasSection (const String& name) const {
    return sectionMap.find(name) != sectionMap.end();
  }

  Section& Document::section (const String& name) {
    auto iterator = sectionMap.find(name);
    if (iterator != sectionMap.end()) {
      return iterator->second;
    }

    auto [it, inserted] = sectionMap.emplace(name, Section(name));
    if (inserted) {
      orderedSections.push_back(name);
    }
    return it->second;
  }

  const Section& Document::section (const String& name) const {
    auto iterator = sectionMap.find(name);
    if (iterator == sectionMap.end()) {
      throw Error("INI section not found: " + name);
    }
    return iterator->second;
  }

  Section& Document::root () {
    return section("");
  }

  const Section& Document::root () const {
    const auto iterator = sectionMap.find("");
    if (iterator == sectionMap.end()) {
      throw Error("INI root section missing");
    }
    return iterator->second;
  }

  const Document::SectionMap& Document::sections () const {
    return sectionMap;
  }

  const Vector<String>& Document::sectionOrder () const {
    return orderedSections;
  }

  Map<String, String> Document::flatten (const String& keyPathSeparator) const {
    Map<String, String> flattened;

    for (const auto& sectionName : orderedSections) {
      const auto& section = sectionMap.at(sectionName);
      const String prefix = detail::sectionPrefix(sectionName, keyPathSeparator);

      for (const auto& key : section.order()) {
        const auto& entry = section.value(key);
        String composedKey = prefix.empty() ? key : prefix + key;

        if (entry.isList()) {
          const bool useNewlines = detail::endsWith(composedKey, "_headers")
            || detail::endsWith(composedKey, "tls_pins")
            || detail::endsWith(composedKey, "tls.pins");
          flattened[composedKey] = detail::joinList(entry.asList(), useNewlines);
        } else {
          flattened[composedKey] = entry.asString();
        }
      }
    }

    return flattened;
  }

  static void ensureInlineCommentRemainder (const String& line, size_t closingBracket, size_t lineNumber) {
    if (closingBracket + 1 >= line.size()) {
      return;
    }

    const String remainder = string::trim(line.substr(closingBracket + 1));
    if (remainder.empty()) {
      return;
    }

    const char lead = remainder[0];
    if (lead != ';' && lead != '#') {
      throw ParseError("Invalid characters after section header", lineNumber, closingBracket + 2);
    }
  }

  Document parseDocument (const String& source) {
    Document document;
    Section* currentSection = &document.root();
    String currentSectionName = "";

    std::istringstream stream(source);
    String rawLine;
    size_t lineNumber = 0;

    while (std::getline(stream, rawLine)) {
      ++lineNumber;

      if (lineNumber == 1) {
        rawLine = detail::stripBom(rawLine);
      }

      if (!rawLine.empty() && rawLine.back() == '\r') {
        rawLine.pop_back();
      }

      String trimmed = string::trim(rawLine);
      if (trimmed.empty()) {
        continue;
      }

      const char lead = trimmed[0];

      if (lead == ';' || lead == '#') {
        continue;
      }

      if (lead == '[') {
        const size_t opening = rawLine.find('[');
        const size_t closing = opening == String::npos
          ? String::npos
          : detail::findClosingBracket(rawLine, opening);

        if (closing == String::npos) {
          throw ParseError("Unterminated section header", lineNumber, rawLine.size());
        }

        ensureInlineCommentRemainder(rawLine, closing, lineNumber);

        String headerContent = rawLine.substr(opening + 1, closing - opening - 1);
        headerContent = detail::removeInlineComment(headerContent);
        String cleanedHeader = string::trim(headerContent);

        const bool relative = !cleanedHeader.empty() && cleanedHeader.front() == '.';
        String decoded = detail::unescape(cleanedHeader);

        if (relative) {
          decoded = decoded.substr(1);
          if (currentSectionName.empty()) {
            throw ParseError("Relative section declared without an active section", lineNumber, detail::firstNonWhitespaceIndex(rawLine) + 1);
          }

          if (!decoded.empty()) {
            if (!currentSectionName.empty()) {
              currentSectionName.append(".");
            }
            currentSectionName.append(decoded);
          }
        } else {
          currentSectionName = decoded;
        }

        currentSection = &document.section(currentSectionName);
        continue;
      }

      const size_t separatorIndex = detail::findKeyValueSeparator(rawLine);

      String key;
      String rawValue;

      if (separatorIndex == String::npos) {
        key = string::trim(rawLine);
        rawValue = "";
      } else {
        key = string::trim(rawLine.substr(0, separatorIndex));
        rawValue = rawLine.substr(separatorIndex + 1);
      }

      if (key.empty()) {
        throw ParseError("Missing key in assignment", lineNumber, 1);
      }

      bool forceList = false;
      if (key.size() >= 2 && key.compare(key.size() - 2, 2, "[]") == 0) {
        forceList = true;
        key = string::trim(key.substr(0, key.size() - 2));
      }

      rawValue = detail::ltrimCopy(rawValue);
      String valuePortion = rawValue;

      bool quoted = false;
      char quote = '\0';

      if (!valuePortion.empty() && (valuePortion.front() == '"' || valuePortion.front() == '\'')) {
        quoted = true;
        quote = valuePortion.front();
        while (!detail::hasTerminatingQuote(valuePortion, quote)) {
          String continuation;
          if (!std::getline(stream, continuation)) {
            throw ParseError("Unterminated quoted value", lineNumber, rawLine.find(quote) + 1);
          }
          ++lineNumber;
          if (!continuation.empty() && continuation.back() == '\r') {
            continuation.pop_back();
          }
          valuePortion.append("\n");
          valuePortion.append(continuation);
        }
      }

      valuePortion = detail::removeInlineComment(valuePortion);

      String value;
      if (quoted) {
        if (valuePortion.size() < 2 || valuePortion.front() != quote || valuePortion.back() != quote || detail::isEscaped(valuePortion, valuePortion.size() - 1)) {
          throw ParseError("Invalid quoted value", lineNumber, rawLine.find(quote) + 1);
        }
        value = detail::decodeQuotedValue(valuePortion);
      } else {
        value = string::trim(valuePortion);
      }

      if (forceList || currentSection->has(key)) {
        currentSection->set(key, std::move(value), true, forceList);
      } else {
        currentSection->set(key, std::move(value), false, forceList);
      }
    }

    return document;
  }

  Document parseFile (const Path& path) {
    InputFileStream stream(path);
    if (!stream.is_open()) {
      throw Error("Unable to open INI file: " + path.string());
    }

    StringStream buffer;
    buffer << stream.rdbuf();
    return parseDocument(buffer.str());
  }

  String serialize (const Document& document) {
    StringStream output;
    const auto& order = document.sectionOrder();

    for (size_t i = 0; i < order.size(); ++i) {
      const auto& sectionName = order[i];
      const auto& section = document.section(sectionName);
      bool wroteSectionContent = false;

      if (!sectionName.empty()) {
        output << "[" << sectionName << "]\n";
        wroteSectionContent = true;
      }

      for (const auto& key : section.order()) {
        const auto& entry = section.value(key);
        const bool explicitList = section.isExplicitList(key);

        if (entry.isList()) {
          const auto& list = entry.asList();
          if (explicitList) {
            for (const auto& item : list) {
              output << key << "[] = " << detail::escapeForSerialization(item) << "\n";
            }
          } else {
            for (const auto& item : list) {
              output << key << " = " << detail::escapeForSerialization(item) << "\n";
            }
          }
        } else if (explicitList) {
          output << key << "[] = " << detail::escapeForSerialization(entry.asString()) << "\n";
        } else {
          output << key << " = " << detail::escapeForSerialization(entry.asString()) << "\n";
        }
        wroteSectionContent = true;
      }

      if (i + 1 < order.size() && wroteSectionContent) {
        output << "\n";
      }
    }

    return output.str();
  }

  Map<String, String> parse (const String& source) {
    return parse(source, "_");
  }

  Map<String, String> parse (const String& source, const String& keyPathSeparator) {
    return parseDocument(source).flatten(keyPathSeparator);
  }

  Map<String, String> parseFileFlat (const Path& path) {
    return parseFile(path).flatten("_");
  }

  Map<String, String> parseFileFlat (const Path& path, const String& keyPathSeparator) {
    return parseFile(path).flatten(keyPathSeparator);
  }

  String serialize (const Map<String, String>& map) {
    StringStream stream;
    for (auto iterator = map.begin(); iterator != map.end(); ++iterator) {
      stream << iterator->first << " = " << iterator->second;
      if (std::next(iterator) != map.end()) {
        stream << "\n";
      }
    }
    return stream.str();
  }
} // namespace oro::runtime::INI
