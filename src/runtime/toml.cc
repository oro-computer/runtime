#include "toml.hh"
#include "string.hh"

#include <cctype>
#include <limits>
#include <sstream>

namespace oro::runtime::TOML {
  using runtime::String;
  using runtime::Vector;
  using runtime::Path;
  using runtime::InputFileStream;
  using runtime::StringStream;
  using runtime::Set;

  ParseError::ParseError (const String& message, size_t line, size_t column)
    : std::runtime_error(message),
      errorLine(line),
      errorColumn(column) {
  }

  size_t ParseError::line () const {
    return errorLine;
  }

  size_t ParseError::column () const {
    return errorColumn;
  }

  Value::Value () : data(std::monostate()), valueType(Type::Empty) {}

  Value::Value (bool value) : data(value), valueType(Type::Boolean) {}

  Value::Value (int64_t value) : data(value), valueType(Type::Integer) {}

  Value::Value (double value) : data(value), valueType(Type::Float) {}

  Value::Value (const String& value) : data(value), valueType(Type::String) {}

  Value::Value (String&& value)
    : data(std::move(value)),
      valueType(Type::String) {}

  Value::Value (const char* value)
    : data(String(value)),
      valueType(Type::String) {}

  Value::Value (const Array& value)
    : data(value),
      valueType(Type::Array) {}

  Value::Value (Array&& value)
    : data(std::move(value)),
      valueType(Type::Array) {}

  Value::Value (const Table& value)
    : data(value),
      valueType(Type::Table) {}

  Value::Value (Table&& value)
    : data(std::move(value)),
      valueType(Type::Table) {}

  Value::Value (const Date& value)
    : data(value),
      valueType(Type::Date) {}

  Value::Value (Date&& value)
    : data(std::move(value)),
      valueType(Type::Date) {}

  Value::Value (const Time& value)
    : data(value),
      valueType(Type::Time) {}

  Value::Value (Time&& value)
    : data(std::move(value)),
      valueType(Type::Time) {}

  Value::Value (const DateTime& value)
    : data(value),
      valueType(Type::DateTime) {}

  Value::Value (DateTime&& value)
    : data(std::move(value)),
      valueType(Type::DateTime) {}

  Type Value::type () const {
    return valueType;
  }

  bool Value::is (Type expected) const {
    return valueType == expected;
  }

  bool Value::isArray () const {
    return is(Type::Array);
  }

  bool Value::isTable () const {
    return is(Type::Table);
  }

  bool Value::asBool () const {
    if (!is(Type::Boolean)) {
      throw runtime::Error("TOML value is not a boolean");
    }

    return std::get<bool>(data);
  }

  int64_t Value::asInteger () const {
    if (!is(Type::Integer)) {
      throw runtime::Error("TOML value is not an integer");
    }

    return std::get<int64_t>(data);
  }

  double Value::asFloat () const {
    if (!is(Type::Float) && !is(Type::Integer)) {
      throw runtime::Error("TOML value is not numeric");
    }

    if (is(Type::Float)) {
      return std::get<double>(data);
    }

    return static_cast<double>(std::get<int64_t>(data));
  }

  const String& Value::asString () const {
    if (!is(Type::String)) {
      throw runtime::Error("TOML value is not a string");
    }

    return std::get<String>(data);
  }

  String& Value::asString () {
    if (!is(Type::String)) {
      throw runtime::Error("TOML value is not a string");
    }

    return std::get<String>(data);
  }

  const Value::Array& Value::asArray () const {
    if (!is(Type::Array)) {
      throw runtime::Error("TOML value is not an array");
    }

    return std::get<Array>(data);
  }

  Value::Array& Value::asArray () {
    if (!is(Type::Array)) {
      throw runtime::Error("TOML value is not an array");
    }

    return std::get<Array>(data);
  }

  const Value::Table& Value::asTable () const {
    if (!is(Type::Table)) {
      throw runtime::Error("TOML value is not a table");
    }

    return std::get<Table>(data);
  }

  Value::Table& Value::asTable () {
    if (!is(Type::Table)) {
      throw runtime::Error("TOML value is not a table");
    }

    return std::get<Table>(data);
  }

  const Date& Value::asDate () const {
    if (!is(Type::Date)) {
      throw runtime::Error("TOML value is not a date");
    }

    return std::get<Date>(data);
  }

  const Time& Value::asTime () const {
    if (!is(Type::Time)) {
      throw runtime::Error("TOML value is not a time");
    }

    return std::get<Time>(data);
  }

  const DateTime& Value::asDateTime () const {
    if (!is(Type::DateTime)) {
      throw runtime::Error("TOML value is not a datetime");
    }

    return std::get<DateTime>(data);
  }

  const Value::Variant& Value::variant () const {
    return data;
  }

  Value& Value::operator[] (const String& key) {
    return asTable()[key];
  }

  const Value& Value::operator[] (const String& key) const {
    const auto& table = asTable();
    const auto iterator = table.find(key);

    if (iterator == table.end()) {
      static Value empty;
      return empty;
    }

    return iterator->second;
  }

  namespace {
    class Parser {
      public:
        explicit Parser (const String& text);

        Value parse ();

      private:
        const String& source;
        size_t position = 0;
        size_t length = 0;
        size_t line = 1;
        size_t column = 1;

        Value root;
        Value::Table* currentTable = nullptr;
        Vector<String> currentPath;
        Set<String> closedTables;
        Set<String> definedTables;

        bool eof () const;
        char peek (size_t offset = 0) const;
        char consume ();
        bool match (char expected);
        void expect (char expected, const char* errorMessage);
        void skipWhitespace (bool includeNewlines = false);
        void skipWhitespaceAndComments ();
        void skipInlineWhitespace ();
        void skipComment ();
        void consumeLineTerminator ();

        [[noreturn]]
        void error (const String& message) const;

        void parseKeyValue ();
        void parseTableHeader ();

        Vector<String> parseKeyPath ();
        String parseKeySegment ();
        String parseBareKey ();

        Value parseValue (bool& closesTable);
        Value parseArray ();
        Value parseInlineTable (bool& closesTable);
        String parseBasicString (bool multiline);
        String parseLiteralString (bool multiline);
        String parseBareLiteralToken ();
        Value parseBareLiteral (const String& literal);
        Value parseBoolean (const String& literal);
        Value parseNumber (const String& literal);
        bool tryParseDate (const String& literal, Date& out);
        bool tryParseTime (const String& literal, Time& out);
        bool tryParseDateTime (const String& literal, DateTime& out);

        Vector<String> parseTablePath (bool isArray);
        void enterTable (const Vector<String>& path, bool isArray);
        void assign (const Vector<String>& keyPath, Value&& value, bool markClosed);
        Vector<String> makeAbsolutePath (const Vector<String>& relative) const;
        String pathToString (const Vector<String>& path) const;
        void ensureTableWritable (const Vector<String>& path) const;
        void markTableClosed (const Vector<String>& path);

        String parseEscapeSequence (bool multiline);
        String encodeCodePoint (uint32_t codepoint);
        uint32_t parseHex (size_t count);
        void skipMultilineLineEnding ();
    };
  }

  Parser::Parser (const String& text)
    : source(text),
      length(text.size()),
      root(Value::Table{}) {
    currentTable = &root.asTable();
  }

  Value Parser::parse () {
    skipWhitespaceAndComments();

    while (!eof()) {
      if (peek() == '[') {
        parseTableHeader();
      } else {
        parseKeyValue();
      }

      skipWhitespaceAndComments();
    }

    return root;
  }

  bool Parser::eof () const {
    return position >= length;
  }

  char Parser::peek (size_t offset) const {
    if (position + offset >= length) {
      return '\0';
    }

    return source[position + offset];
  }

  char Parser::consume () {
    if (eof()) {
      return '\0';
    }

    char character = source[position++];

    if (character == '\r') {
      if (position < length && source[position] == '\n') {
        position++;
      }

      line += 1;
      column = 1;
      return '\n';
    }

    if (character == '\n') {
      line += 1;
      column = 1;
    } else {
      column += 1;
    }

    return character;
  }

  bool Parser::match (char expected) {
    if (!eof() && peek() == expected) {
      consume();
      return true;
    }

    return false;
  }

  void Parser::expect (char expected, const char* errorMessage) {
    if (!match(expected)) {
      error(errorMessage);
    }
  }

  void Parser::skipWhitespace (bool includeNewlines) {
    while (!eof()) {
      const char character = peek();

      if (character == ' ' || character == '\t') {
        consume();
        continue;
      }

      if ((character == '\n' || character == '\r') && includeNewlines) {
        consume();
        continue;
      }

      break;
    }
  }

  void Parser::skipInlineWhitespace () {
    skipWhitespace(false);
  }

  void Parser::skipWhitespaceAndComments () {
    while (!eof()) {
      skipWhitespace(true);

      if (eof()) {
        break;
      }

      const char character = peek();

      // Treat both '#' (TOML) and leading ';' (INI-style) as line comments.
      if (character == '#' || character == ';') {
        skipComment();
        continue;
      }

      if (character == '\n' || character == '\r') {
        consume();
        continue;
      }

      if (character == ' ' || character == '\t') {
        consume();
        continue;
      }

      break;
    }
  }

  void Parser::skipComment () {
    if (eof()) {
      return;
    }

    const char start = peek();
    if (start != '#' && start != ';') {
      return;
    }

    while (!eof()) {
      const char character = consume();
      if (character == '\n') {
        break;
      }
    }
  }

  void Parser::consumeLineTerminator () {
    skipInlineWhitespace();

    if (!eof() && (peek() == '#' || peek() == ';')) {
      skipComment();
      skipInlineWhitespace();
    }

    if (eof()) {
      return;
    }

    char character = peek();

    if (character == '\n' || character == '\r') {
      skipWhitespace(true);
      return;
    }

    error("Expected newline or comment after value");
  }

  [[noreturn]]
  void Parser::error (const String& message) const {
    StringStream stream;
    stream << message << " (line " << line << ", column " << column << ")";
    throw ParseError(stream.str(), line, column);
  }

  Vector<String> Parser::makeAbsolutePath (const Vector<String>& relative) const {
    Vector<String> absolute = currentPath;
    absolute.insert(absolute.end(), relative.begin(), relative.end());
    return absolute;
  }

  String Parser::pathToString (const Vector<String>& path) const {
    String result;

    for (const auto& segment : path) {
      if (!result.empty()) {
        result += '.';
      }

      result += segment;
    }

    return result;
  }

  void Parser::ensureTableWritable (const Vector<String>& path) const {
    if (path.empty()) {
      return;
    }

    String prefix;
    for (size_t index = 0; index < path.size(); ++index) {
      if (!prefix.empty()) {
        prefix += '.';
      }

      prefix += path[index];

      if (closedTables.find(prefix) != closedTables.end()) {
        StringStream stream;
        stream << "Cannot modify table '" << prefix << "' defined inline";
        error(stream.str());
      }
    }
  }

  void Parser::markTableClosed (const Vector<String>& path) {
    if (path.empty()) {
      return;
    }

    closedTables.insert(pathToString(path));
  }

  void Parser::parseKeyValue () {
    const auto keyPath = parseKeyPath();

    skipInlineWhitespace();
    expect('=', "Expected '=' after key");
    skipInlineWhitespace();

    bool closesTable = false;
    Value value = parseValue(closesTable);

    assign(keyPath, std::move(value), closesTable);
    consumeLineTerminator();
  }

  void Parser::parseTableHeader () {
    consume(); // '['
    bool isArray = false;

    if (match('[')) {
      isArray = true;
    }

    skipInlineWhitespace();

    const auto path = parseTablePath(isArray);

    skipInlineWhitespace();

    if (isArray) {
      expect(']', "Expected closing bracket for array of tables");
      expect(']', "Expected closing bracket for array of tables");
    } else {
      expect(']', "Expected closing bracket for table");
    }

    skipInlineWhitespace();

    if (!eof() && peek() == '#') {
      skipComment();
      skipInlineWhitespace();
    }

    if (!eof()) {
      if (peek() == '\n' || peek() == '\r') {
        skipWhitespace(true);
      } else {
        error("Unexpected characters after table header");
      }
    }

    enterTable(path, isArray);
  }

  Vector<String> Parser::parseKeyPath () {
    Vector<String> path;

    while (true) {
      skipInlineWhitespace();

      if (eof()) {
        error("Unexpected end of file while reading key");
      }

      const auto segment = parseKeySegment();

      if (segment.empty()) {
        error("Empty key segment");
      }

      path.push_back(segment);
      skipInlineWhitespace();

      if (!match('.')) {
        break;
      }
    }

    if (path.empty()) {
      error("Expected key");
    }

    return path;
  }

  String Parser::parseKeySegment () {
    if (eof()) {
      error("Unexpected end of file while reading key segment");
    }

    const char character = peek();

    if (character == '"') {
      return parseBasicString(false);
    }

    if (character == '\'') {
      return parseLiteralString(false);
    }

    return parseBareKey();
  }

  String Parser::parseBareKey () {
    String key;

    while (!eof()) {
      const char character = peek();

      if (
        std::isalnum(static_cast<unsigned char>(character)) ||
        character == '-' ||
        character == '_'
      ) {
        key.push_back(consume());
        continue;
      }

      break;
    }

    if (key.empty()) {
      error("Invalid bare key");
    }

    return key;
  }

  Value Parser::parseValue (bool& closesTable) {
    closesTable = false;

    if (eof()) {
      error("Unexpected end of file while reading value");
    }

    const char character = peek();

    if (character == '"') {
      const bool multiline = peek(1) == '"' && peek(2) == '"';
      return Value(parseBasicString(multiline));
    }

    if (character == '\'') {
      const bool multiline = peek(1) == '\'' && peek(2) == '\'';
      return Value(parseLiteralString(multiline));
    }

    if (character == '[') {
      consume();
      return parseArray();
    }

    if (character == '{') {
      consume();
      closesTable = true;
      return parseInlineTable(closesTable);
    }

    const auto literal = parseBareLiteralToken();

    if (literal.empty()) {
      error("Expected value");
    }

    return parseBareLiteral(literal);
  }

  String Parser::parseBareLiteralToken () {
    String literal;

    while (!eof()) {
      const char character = peek();

      if (
        character == ' ' ||
        character == '\t' ||
        character == '\n' ||
        character == '\r' ||
        character == ',' ||
        character == ']' ||
        character == '}' ||
        character == '#' ||
        character == ';'
      ) {
        break;
      }

      literal.push_back(consume());
    }

    return literal;
  }

  Value Parser::parseArray () {
    Value::Array array;
    Type elementType = Type::Empty;

    skipWhitespace(true);

    if (match(']')) {
      return Value(std::move(array));
    }

    while (true) {
      skipWhitespace(true);
      while (!eof() && peek() == '#') {
        skipComment();
        skipWhitespace(true);
      }

      if (match(']')) {
        break;
      }

      bool closesTableElement = false;
      Value element = parseValue(closesTableElement);
      (void) closesTableElement;

      if (elementType == Type::Empty) {
        elementType = element.type();
      } else if (element.type() != elementType) {
        error("Mixed type arrays are not permitted");
      }

      array.emplace_back(std::move(element));

      skipWhitespace(true);
      while (!eof() && peek() == '#') {
        skipComment();
        skipWhitespace(true);
      }

      if (match(']')) {
        break;
      }

      if (match(',')) {
        continue;
      }

      error("Expected ',' or closing bracket in array");
    }

    return Value(std::move(array));
  }

  Value Parser::parseInlineTable (bool& closesTable) {
    closesTable = true;
    Value::Table table;

    skipInlineWhitespace();
    if (match('}')) {
      return Value(std::move(table));
    }

    while (true) {
      skipInlineWhitespace();
      if (eof()) {
        error("Unexpected end of inline table");
      }

      const auto keyPath = parseKeyPath();
      skipInlineWhitespace();
      expect('=', "Expected '=' within inline table");
      skipInlineWhitespace();

      bool nestedInline = false;
      Value value = parseValue(nestedInline);
      (void) nestedInline;

      Value::Table* target = &table;
      for (size_t index = 0; index < keyPath.size(); ++index) {
        const auto& key = keyPath[index];
        const bool isLast = index == keyPath.size() - 1;
        const auto iterator = target->find(key);

        if (isLast) {
          if (iterator != target->end()) {
            error("Duplicate key inside inline table");
          }
          target->emplace(key, std::move(value));
        } else {
          if (iterator == target->end()) {
            auto inserted = target->emplace(key, Value(Value::Table{}));
            target = &inserted.first->second.asTable();
          } else {
            if (!iterator->second.isTable()) {
              error("Inline table key conflicts with non-table value");
            }
            target = &iterator->second.asTable();
          }
        }
      }

      skipInlineWhitespace();
      if (match('}')) {
        break;
      }

      if (match(',')) {
        continue;
      }

      error("Expected ',' or '}' in inline table");
    }

    return Value(std::move(table));
  }

  String Parser::parseBasicString (bool multiline) {
    if (multiline) {
      consume();
      consume();
      consume();
      skipMultilineLineEnding();
    } else {
      consume();
    }

    String result;

    while (!eof()) {
      char character = consume();

      if (!multiline && (character == '\n' || character == '\r')) {
        error("Unescaped newline in basic string");
      }

      if (character == '"') {
        if (!multiline) {
          return result;
        }

        if (match('"')) {
          if (match('"')) {
            return result;
          }

          result.push_back('"');
          result.push_back('"');
          continue;
        }

        result.push_back('"');
        continue;
      }

      if (character == '\\') {
        if (multiline) {
          const char next = peek();
          if (next == '\n' || next == '\r') {
            skipMultilineLineEnding();
            continue;
          }
        }

        result += parseEscapeSequence(multiline);
        continue;
      }

      result.push_back(character);
    }

    error("Unterminated string");
  }

  String Parser::parseLiteralString (bool multiline) {
    if (multiline) {
      consume();
      consume();
      consume();
      skipMultilineLineEnding();
    } else {
      consume();
    }

    String result;

    while (!eof()) {
      char character = consume();

      if (!multiline && (character == '\n' || character == '\r')) {
        error("Unescaped newline in literal string");
      }

      if (character == '\'') {
        if (!multiline) {
          return result;
        }

        if (match('\'')) {
          if (match('\'')) {
            return result;
          }

          result.push_back('\'');
          result.push_back('\'');
          continue;
        }

        result.push_back('\'');
        continue;
      }

      result.push_back(character);
    }

    error("Unterminated literal string");
  }

  Value Parser::parseBareLiteral (const String& literal) {
    if (literal == "true" || literal == "false") {
      return parseBoolean(literal);
    }

    DateTime dateTime;
    if (tryParseDateTime(literal, dateTime)) {
      return Value(dateTime);
    }

    Date date;
    if (tryParseDate(literal, date)) {
      return Value(date);
    }

    Time time;
    if (tryParseTime(literal, time)) {
      return Value(time);
    }

    return parseNumber(literal);
  }

  Value Parser::parseBoolean (const String& literal) {
    if (literal == "true") {
      return Value(true);
    }

    if (literal == "false") {
      return Value(false);
    }

    error("Invalid boolean literal");
    return Value();
  }

  Value Parser::parseNumber (const String& literal) {
    if (literal == "nan" || literal == "+nan" || literal == "-nan") {
      return Value(std::numeric_limits<double>::quiet_NaN());
    }

    if (literal == "inf" || literal == "+inf") {
      return Value(std::numeric_limits<double>::infinity());
    }

    if (literal == "-inf") {
      return Value(-std::numeric_limits<double>::infinity());
    }

    size_t index = 0;
    bool negative = false;

    if (literal[index] == '+' || literal[index] == '-') {
      negative = literal[index] == '-';
      index += 1;
    }

    bool isFloat = false;
    for (size_t i = index; i < literal.size(); ++i) {
      const char character = literal[i];
      if (character == '.' || character == 'e' || character == 'E') {
        isFloat = true;
        break;
      }
    }

    if (isFloat) {
      String sanitized;
      if (negative) {
        sanitized.push_back('-');
      }

      bool seenDecimal = false;
      bool seenExponent = false;

      for (size_t i = index; i < literal.size(); ++i) {
        const char character = literal[i];

        if (character == '_') {
          if (i == index || i + 1 >= literal.size()) {
            error("Invalid placement of '_' in float literal");
          }

          const char prev = literal[i - 1];
          const char next = literal[i + 1];

          if (!std::isdigit(static_cast<unsigned char>(prev)) || !std::isdigit(static_cast<unsigned char>(next))) {
            error("Invalid placement of '_' in float literal");
          }

          continue;
        }

        if (character == '.') {
          if (seenDecimal || seenExponent) {
            error("Invalid float literal");
          }

          if (i == index || i + 1 >= literal.size()) {
            error("Invalid float literal");
          }

          const char prev = literal[i - 1];
          const char next = literal[i + 1];

          if (!std::isdigit(static_cast<unsigned char>(prev)) || !std::isdigit(static_cast<unsigned char>(next))) {
            error("Invalid float literal");
          }

          seenDecimal = true;
          sanitized.push_back('.');
          continue;
        }

        if (character == 'e' || character == 'E') {
          if (seenExponent) {
            error("Multiple exponents in float literal");
          }

          if (i + 1 >= literal.size()) {
            error("Invalid float literal");
          }

          const char next = literal[i + 1];
          if (
            next != '+' &&
            next != '-' &&
            !std::isdigit(static_cast<unsigned char>(next))
          ) {
            error("Invalid exponent in float literal");
          }

          seenExponent = true;
          sanitized.push_back('e');
          continue;
        }

        if (character == '+' || character == '-') {
          if (literal[i - 1] != 'e' && literal[i - 1] != 'E') {
            error("Invalid sign in float literal");
          }

          sanitized.push_back(character);
          continue;
        }

        if (!std::isdigit(static_cast<unsigned char>(character))) {
          error("Invalid character in float literal");
        }

        sanitized.push_back(character);
      }

      try {
        const double value = std::stod(sanitized);
        return Value(value);
      } catch (const std::exception&) {
        error("Float literal out of range");
      }

      return Value();
    }

    int base = 10;
    size_t digitIndex = index;

    if (digitIndex + 1 < literal.size() && literal[digitIndex] == '0') {
      const char prefix = literal[digitIndex + 1];
      if (prefix == 'x' || prefix == 'X') {
        base = 16;
        digitIndex += 2;
      } else if (prefix == 'o' || prefix == 'O') {
        base = 8;
        digitIndex += 2;
      } else if (prefix == 'b' || prefix == 'B') {
        base = 2;
        digitIndex += 2;
      } else if (literal.size() > digitIndex + 1 && std::isdigit(static_cast<unsigned char>(literal[digitIndex + 1]))) {
        error("Leading zeroes are not permitted in integer literals");
      }
    }

    auto isValidDigit = [base](char character) {
      switch (base) {
        case 2: return character == '0' || character == '1';
        case 8: return character >= '0' && character <= '7';
        case 10: return std::isdigit(static_cast<unsigned char>(character)) != 0;
        case 16:
          return
            std::isdigit(static_cast<unsigned char>(character)) ||
            (character >= 'a' && character <= 'f') ||
            (character >= 'A' && character <= 'F');
      }
      return false;
    };

    String digits;

    for (size_t i = digitIndex; i < literal.size(); ++i) {
      const char character = literal[i];

      if (character == '_') {
        if (digits.empty() || i == literal.size() - 1) {
          error("Invalid placement of '_' in integer literal");
        }

        const char prev = literal[i - 1];
        const char next = literal[i + 1];

        if (!isValidDigit(prev) || !isValidDigit(next)) {
          error("Invalid placement of '_' in integer literal");
        }

        continue;
      }

      if (!isValidDigit(character)) {
        error("Invalid digit in integer literal");
      }

      digits.push_back(character);
    }

    if (digits.empty()) {
      error("Invalid integer literal");
    }

    if (base == 10 && digits.size() > 1 && digits[0] == '0') {
      error("Leading zeroes are not permitted in integer literals");
    }

    String formatted = digits;

    if (negative) {
      formatted.insert(formatted.begin(), '-');
    }

    try {
      const int64_t value = std::stoll(formatted, nullptr, base);
      return Value(value);
    } catch (const std::exception&) {
      error("Integer literal out of range");
    }

    return Value();
  }

  bool Parser::tryParseDate (const String& literal, Date& out) {
    if (literal.size() != 10 || literal[4] != '-' || literal[7] != '-') {
      return false;
    }

    auto parseComponent = [] (const String& input, size_t offset, size_t count, int& value) -> bool {
      int result = 0;
      for (size_t i = 0; i < count; ++i) {
        const char character = input[offset + i];
        if (!std::isdigit(static_cast<unsigned char>(character))) {
          return false;
        }

        result = (result * 10) + (character - '0');
      }

      value = result;
      return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;

    if (
      !parseComponent(literal, 0, 4, year) ||
      !parseComponent(literal, 5, 2, month) ||
      !parseComponent(literal, 8, 2, day)
    ) {
      return false;
    }

    if (month < 1 || month > 12) {
      return false;
    }

    static const int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool isLeapYear =
      (year % 4 == 0 && year % 100 != 0) ||
      (year % 400 == 0);

    int maxDay = daysInMonth[month - 1];
    if (month == 2 && isLeapYear) {
      maxDay = 29;
    }

    if (day < 1 || day > maxDay) {
      return false;
    }

    out = { year, month, day };
    return true;
  }

  bool Parser::tryParseTime (const String& literal, Time& out) {
    if (literal.size() < 8 || literal[2] != ':' || literal[5] != ':') {
      return false;
    }

    auto parseComponent = [] (const String& input, size_t offset, size_t count, int& value) -> bool {
      int result = 0;
      for (size_t i = 0; i < count; ++i) {
        const char character = input[offset + i];
        if (!std::isdigit(static_cast<unsigned char>(character))) {
          return false;
        }

        result = (result * 10) + (character - '0');
      }

      value = result;
      return true;
    };

    int hour = 0;
    int minute = 0;
    int second = 0;

    if (
      !parseComponent(literal, 0, 2, hour) ||
      !parseComponent(literal, 3, 2, minute) ||
      !parseComponent(literal, 6, 2, second)
    ) {
      return false;
    }

    if (hour < 0 || hour > 23) {
      return false;
    }

    if (minute < 0 || minute > 59) {
      return false;
    }

    if (second < 0 || second > 60) {
      return false;
    }

    int nanosecond = 0;

    if (literal.size() > 8) {
      if (literal[8] != '.') {
        return false;
      }

      const String fractional = literal.substr(9);
      if (fractional.empty() || fractional.size() > 9) {
        return false;
      }

      for (const char character : fractional) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
          return false;
        }
      }

      String padded = fractional;
      while (padded.size() < 9) {
        padded.push_back('0');
      }

      nanosecond = std::stoi(padded);
    }

    out = { hour, minute, second, nanosecond };
    return true;
  }

  bool Parser::tryParseDateTime (const String& literal, DateTime& out) {
    size_t separator = literal.find_first_of("Tt ");
    if (separator == String::npos) {
      return false;
    }

    const String datePart = literal.substr(0, separator);
    String timeAndOffset = literal.substr(separator + 1);

    if (timeAndOffset.empty()) {
      return false;
    }

    Date date;
    if (!tryParseDate(datePart, date)) {
      return false;
    }

    bool hasOffset = false;
    int offsetMinutes = 0;
    String timePart = timeAndOffset;

    const char lastCharacter = timeAndOffset.back();
    if (lastCharacter == 'Z' || lastCharacter == 'z') {
      hasOffset = true;
      offsetMinutes = 0;
      timePart = timeAndOffset.substr(0, timeAndOffset.size() - 1);
    } else {
      const size_t offsetStart = timeAndOffset.find_last_of("+-");
      if (offsetStart != String::npos && offsetStart > 0) {
        const String offsetPart = timeAndOffset.substr(offsetStart);
        if (offsetPart.size() != 6 || offsetPart[3] != ':') {
          return false;
        }

        int hours = 0;
        int minutes = 0;
        auto parseComponent = [] (const String& input, size_t offset, size_t count, int& value) -> bool {
          int result = 0;
          for (size_t i = 0; i < count; ++i) {
            const char character = input[offset + i];
            if (!std::isdigit(static_cast<unsigned char>(character))) {
              return false;
            }

            result = (result * 10) + (character - '0');
          }

          value = result;
          return true;
        };

        if (
          !parseComponent(offsetPart, 1, 2, hours) ||
          !parseComponent(offsetPart, 4, 2, minutes)
        ) {
          return false;
        }

        if (hours < 0 || hours > 23) {
          return false;
        }

        if (minutes < 0 || minutes > 59) {
          return false;
        }

        offsetMinutes = (hours * 60) + minutes;
        if (offsetPart[0] == '-') {
          offsetMinutes *= -1;
        }

        hasOffset = true;
        timePart = timeAndOffset.substr(0, offsetStart);
      }
    }

    Time time;
    if (!tryParseTime(timePart, time)) {
      return false;
    }

    out = { date, time, hasOffset, offsetMinutes };
    return true;
  }

  Vector<String> Parser::parseTablePath (bool isArray) {
    Vector<String> path;

    while (true) {
      skipInlineWhitespace();

      if (eof()) {
        error("Unterminated table header");
      }

      if (peek() == ']') {
        break;
      }

      if (isArray && peek() == ']' && peek(1) == ']') {
        break;
      }

      const auto segment = parseKeySegment();
      if (segment.empty()) {
        error("Empty table key");
      }

      path.push_back(segment);
      skipInlineWhitespace();

      if (!match('.')) {
        break;
      }
    }

    if (path.empty()) {
      error("Table name may not be empty");
    }

    return path;
  }

  void Parser::enterTable (const Vector<String>& path, bool isArray) {
    ensureTableWritable(path);

    Value::Table* table = &root.asTable();

    for (size_t index = 0; index < path.size(); ++index) {
      const auto& key = path[index];
      const bool isLast = index == path.size() - 1;
      auto iterator = table->find(key);

      if (isLast) {
        if (isArray) {
          if (iterator == table->end()) {
            iterator = table->emplace(key, Value(Value::Array{})).first;
          } else if (!iterator->second.isArray()) {
            const auto tablePath = pathToString(path);
            error("Array of tables '" + tablePath + "' conflicts with existing non-array value");
          }

          auto& array = iterator->second.asArray();
          array.emplace_back(Value(Value::Table{}));
          currentTable = &array.back().asTable();
        } else {
          if (iterator == table->end()) {
            iterator = table->emplace(key, Value(Value::Table{})).first;
          } else {
            if (!iterator->second.isTable()) {
              const auto tablePath = pathToString(path);
              error("Table '" + tablePath + "' conflicts with existing non-table value");
            }

            const auto tablePath = pathToString(path);
            if (definedTables.find(tablePath) != definedTables.end()) {
              error("Table '" + tablePath + "' already defined");
            }
          }

          currentTable = &iterator->second.asTable();
          definedTables.insert(pathToString(path));
        }
      } else {
        if (iterator == table->end()) {
          iterator = table->emplace(key, Value(Value::Table{})).first;
          table = &iterator->second.asTable();
          continue;
        }

        if (iterator->second.isArray()) {
          auto& array = iterator->second.asArray();
          if (array.empty()) {
            array.emplace_back(Value(Value::Table{}));
          }
          table = &array.back().asTable();
          continue;
        }

        if (!iterator->second.isTable()) {
          error("Intermediate key '" + key + "' is not a table");
        }

        table = &iterator->second.asTable();
      }
    }

    currentPath = path;
  }

  void Parser::assign (const Vector<String>& keyPath, Value&& value, bool markClosed) {
    auto absolute = makeAbsolutePath(keyPath);
    ensureTableWritable(absolute);

    Value::Table* target = currentTable;
    Vector<String> prefix = currentPath;

    for (size_t index = 0; index < keyPath.size(); ++index) {
      const auto& key = keyPath[index];
      const bool isLast = index == keyPath.size() - 1;
      auto iterator = target->find(key);
      prefix.push_back(key);

      if (isLast) {
        if (iterator != target->end()) {
          error("Duplicate key '" + pathToString(absolute) + "'");
        }
        target->emplace(key, std::move(value));
      } else {
        bool created = false;
        if (iterator == target->end()) {
          iterator = target->emplace(key, Value(Value::Table{})).first;
          created = true;
        } else if (!iterator->second.isTable()) {
          error("Key '" + key + "' conflicts with existing value");
        }

        target = &iterator->second.asTable();
        if (created) {
          definedTables.insert(pathToString(prefix));
        }
      }
    }

    if (markClosed) {
      markTableClosed(absolute);
    }
  }

  String Parser::parseEscapeSequence (bool multiline) {
    if (eof()) {
      error("Unexpected end of string");
    }

    const char character = consume();

    switch (character) {
      case 'b': return String(1, '\b');
      case 't': return String(1, '\t');
      case 'n': return String(1, '\n');
      case 'f': return String(1, '\f');
      case 'r': return String(1, '\r');
      case '"': return String(1, '"');
      case '\\': return String(1, '\\');
      case 'u': return encodeCodePoint(parseHex(4));
      case 'U': return encodeCodePoint(parseHex(8));
      default:
        break;
    }

    error("Invalid escape sequence");
  }

  String Parser::encodeCodePoint (uint32_t codepoint) {
    String result;

    if (codepoint <= 0x7F) {
      result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
      result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
      result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
      error("Invalid unicode codepoint");
    }

    return result;
  }

  uint32_t Parser::parseHex (size_t count) {
    uint32_t value = 0;

    for (size_t i = 0; i < count; ++i) {
      if (eof()) {
        error("Unexpected end of unicode escape");
      }

      const char character = consume();
      uint32_t digit = 0;

      if (character >= '0' && character <= '9') {
        digit = character - '0';
      } else if (character >= 'a' && character <= 'f') {
        digit = 10 + (character - 'a');
      } else if (character >= 'A' && character <= 'F') {
        digit = 10 + (character - 'A');
      } else {
        error("Invalid unicode escape");
      }

      value = (value << 4) | digit;
    }

    return value;
  }

  void Parser::skipMultilineLineEnding () {
    if (eof()) {
      return;
    }

    if (peek() == '\r') {
      consume();
      if (!eof() && peek() == '\n') {
        consume();
      }
    } else if (peek() == '\n') {
      consume();
    } else {
      return;
    }

    while (!eof()) {
      const char character = peek();
      if (character == ' ' || character == '\t') {
        consume();
      } else {
        break;
      }
    }
  }

  Value parse (const String& source) {
    Parser parser(source);
    return parser.parse();
  }

  Value parseFile (const Path& path) {
    InputFileStream input(path);
    if (!input.is_open()) {
      throw runtime::Error("Cannot open TOML file: " + path.string());
    }

    StringStream buffer;
    buffer << input.rdbuf();
    input.close();
    return parse(buffer.str());
  }
}
