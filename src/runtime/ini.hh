#ifndef ORO_RUNTIME_RUNTIME_INI_H
#define ORO_RUNTIME_RUNTIME_INI_H

#include "platform.hh"

#include <cstddef>

namespace oro::runtime::INI {
  using runtime::Path;
  using runtime::String;
  using runtime::Vector;
  using runtime::Map;
  using runtime::Set;

  enum class ValueType {
    Scalar,
    List
  };

  class ParseError : public runtime::Error {
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
      using List = Vector<String>;

      Value ();
      explicit Value (const String& value);
      explicit Value (String&& value);
      explicit Value (const List& list);
      explicit Value (List&& list);

      ValueType type () const;
      bool isScalar () const;
      bool isList () const;

      const String& asString () const;
      const List& asList () const;

      void set (const String& value);
      void set (String&& value);
      void setList (const List& list);
      void setList (List&& list);
      void append (const String& value);
      void append (String&& value);

    private:
      ValueType valueType;
      String scalar;
      List list;
  };

  class Section {
    public:
      Section ();
      explicit Section (const String& name);

      const String& name () const;
      bool empty () const;
      bool has (const String& key) const;

      Value& set (const String& key, const String& value, bool append, bool forceList = false);
      Value& set (const String& key, String&& value, bool append, bool forceList = false);
      Value& value (const String& key);
      const Value& value (const String& key) const;

      const Map<String, Value>& entries () const;
      const Vector<String>& order () const;
      bool isExplicitList (const String& key) const;

    private:
      String sectionName;
      Map<String, Value> values;
      Vector<String> insertion;
      Set<String> explicitLists;
  };

  class Document {
    public:
      using SectionMap = Map<String, Section>;

      Document ();

      bool hasSection (const String& name) const;
      Section& section (const String& name);
      const Section& section (const String& name) const;
      Section& root ();
      const Section& root () const;

      const SectionMap& sections () const;
      const Vector<String>& sectionOrder () const;

      Map<String, String> flatten (const String& keyPathSeparator) const;

    private:
      SectionMap sectionMap;
      Vector<String> orderedSections;
  };

  Document parseDocument (const String& source);
  Document parseFile (const Path& path);

  String serialize (const Document& document);

  Map<String, String> parse (const String& source);
  Map<String, String> parse (const String& source, const String& keyPathSeparator);
  Map<String, String> parseFileFlat (const Path& path);
  Map<String, String> parseFileFlat (const Path& path, const String& keyPathSeparator);
  String serialize (const Map<String, String>& map);
}

#endif
