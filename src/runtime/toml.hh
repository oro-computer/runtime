#ifndef ORO_RUNTIME_TOML_H
#define ORO_RUNTIME_TOML_H

#include "platform.hh"

#include <cstdint>
#include <stdexcept>
#include <variant>

namespace oro::runtime::TOML {
  enum class Type {
    Empty,
    Boolean,
    Integer,
    Float,
    String,
    Array,
    Table,
    Date,
    Time,
    DateTime
  };

  struct Date {
    int year = 0;
    int month = 0;
    int day = 0;
  };

  struct Time {
    int hour = 0;
    int minute = 0;
    int second = 0;
    int nanosecond = 0;
  };

  struct DateTime {
    Date date;
    Time time;
    bool hasOffset = false;
    int offsetMinutes = 0;
  };

  class ParseError : public std::runtime_error {
    public:
      ParseError (const String& message, size_t line, size_t column);

      size_t line () const;
      size_t column () const;

    private:
      size_t errorLine;
      size_t errorColumn;
  };

  class Value {
    public:
      using Array = Vector<Value>;
      using Table = Map<String, Value>;
      using Variant = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        String,
        Array,
        Table,
        Date,
        Time,
        DateTime
      >;

      Value ();
      explicit Value (bool value);
      explicit Value (int64_t value);
      explicit Value (double value);
      explicit Value (const String& value);
      explicit Value (String&& value);
      explicit Value (const char* value);
      explicit Value (const Array& value);
      explicit Value (Array&& value);
      explicit Value (const Table& value);
      explicit Value (Table&& value);
      explicit Value (const Date& value);
      explicit Value (Date&& value);
      explicit Value (const Time& value);
      explicit Value (Time&& value);
      explicit Value (const DateTime& value);
      explicit Value (DateTime&& value);

      Type type () const;
      bool is (Type expected) const;
      bool isArray () const;
      bool isTable () const;

      bool asBool () const;
      int64_t asInteger () const;
      double asFloat () const;
      const String& asString () const;
      String& asString ();
      const Array& asArray () const;
      Array& asArray ();
      const Table& asTable () const;
      Table& asTable ();
      const Date& asDate () const;
      const Time& asTime () const;
      const DateTime& asDateTime () const;

      const Variant& variant () const;

      Value& operator[] (const String& key);
      const Value& operator[] (const String& key) const;

    private:
      Variant data;
      Type valueType;
  };

  Value parse (const String& source);
  Value parseFile (const Path& path);
}

#endif
