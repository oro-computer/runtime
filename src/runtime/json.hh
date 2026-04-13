#ifndef ORO_RUNTIME_RUNTIME_JSON_H
#define ORO_RUNTIME_RUNTIME_JSON_H

#include "platform.hh"
#include "crypto.hh"

namespace oro::runtime::JSON {
  using types::Atomic;
  using types::SharedPointer;
  template <typename K = types::String, typename V = types::String>
  using RuntimeMap = types::Map<K, V>;
  using RuntimePath = types::Path;
  using RuntimeString = types::String;
  template <typename T>
  using RuntimeVector = types::Vector<T>;

  // forward
  class Any;
  class Raw;
  class Null;
  class Object;
  class Array;
  class Boolean;
  class Number;
  class String;

  using ObjectEntries = RuntimeMap<RuntimeString, Any>;
  using ArrayEntries = RuntimeVector<Any>;

  enum class Type {
    Empty = -1,
    Any = 0,
    Null = 1,
    Object = 2,
    Array = 3,
    Boolean = 4,
    Number = 5,
    String = 6,
    Raw = 7,
    Error = 8
  };

  class Entity {
    public:
      using ID = uint64_t;

      ID id = crypto::rand64();

      virtual ~Entity() = 0;

      operator bool () const;

      const RuntimeString typeof () const;
      bool isError () const;
      bool isRaw () const;
      bool isArray () const;
      bool isBoolean () const;
      bool isNumber () const;
      bool isNull () const;
      bool isObject () const;
      bool isString () const;
      bool isEmpty () const;

      const ID getEntityID ();
      virtual Type getEntityType () const = 0;
      virtual bool getEntityBooleanValue () const = 0;
      virtual const RuntimeString str () const = 0;
  };

  class SharedEntityPointer {
    public:
      struct ControlBlock {
        std::atomic_size_t size;
        Entity* entity;

        ControlBlock (Entity*);
      };

      SharedEntityPointer (Entity* = nullptr);
      SharedEntityPointer (const SharedEntityPointer&);
      SharedEntityPointer (SharedEntityPointer&&);
      ~SharedEntityPointer ();

      SharedEntityPointer& operator = (const SharedEntityPointer&);
      SharedEntityPointer& operator = (SharedEntityPointer&&);
      Entity* operator -> () const;
      operator bool () const;

      void reset (Entity* = nullptr);
      size_t use_count () const;
      void swap (SharedEntityPointer&);

      Entity* get () const;
      template <typename T> T* as () const;

    protected:
      SharedPointer<ControlBlock> control = nullptr;
  };

  template <typename T = Null, typename... Args>
  SharedEntityPointer make_shared (Args... args) {
    static_assert(std::is_base_of<Entity, T>::value, "T must derive from Entity");
    return new T(args...);
  }

  template <typename D, Type t> class Value : public Entity {
    public:
      using DataType = D;
      Type type = t;
      DataType data;

      virtual const DataType value () const = 0;

      Type getEntityType () const override {
        return this->type;
      }

      bool getEntityBooleanValue () const override {
        return Entity::getEntityBooleanValue();
      }
  };

  class Error : public std::invalid_argument, public Value<RuntimeString, Type::Error> {
    public:
      static Type valueType;
      int code = 0;
      RuntimeString name;
      RuntimeString message;
      RuntimeString location;

      Error ();
      Error (const RuntimeString& message);
      Error (const Error&);
      Error (Error*);
      Error (
        const RuntimeString& name,
        const RuntimeString& message,
        int code = 0
      );
      Error (
        const RuntimeString& name,
        const RuntimeString& message,
        const RuntimeString& location
      );

      const RuntimeString value () const override;
      const char* what () const noexcept override;
      const RuntimeString str () const override;
  };

  class Null : public Value<std::nullptr_t, Type::Null> {
    public:
      static Type valueType;
      Null () = default;
      Null (std::nullptr_t);
      const std::nullptr_t value () const override;
      const RuntimeString str () const override;
  };

  class Any : public Value<SharedEntityPointer, Type::Any> {
    public:
      static Type valueType;

      Any (Type, const SharedEntityPointer&);
      Any (std::nullptr_t);
      Any (const Null);

      Any (bool);
      Any (int64_t);
      Any (uint64_t);
      Any (uint32_t);
      Any (int32_t);
      Any (double);
    #if ORO_RUNTIME_PLATFORM_APPLE
      Any (size_t);
      Any (ssize_t);
    #elif !ORO_RUNTIME_PLATFORM_WINDOWS
      Any (long long);
    #endif

      Any (Atomic<bool>&);
      Any (Atomic<int64_t>&);
      Any (Atomic<uint64_t>&);
      Any (Atomic<uint32_t>&);
      Any (Atomic<int32_t>&);
      Any (Atomic<double>&);
    #if ORO_RUNTIME_PLATFORM_APPLE
      Any (Atomic<size_t>&);
      Any (Atomic<ssize_t>&);
    #elif !ORO_RUNTIME_PLATFORM_WINDOWS
      Any (Atomic<long long>&);
    #endif

      Any (const char);
      Any (const char *);

      Any (const RuntimeString&);
      Any (const RuntimePath&);
      Any (const RuntimeMap<RuntimeString, RuntimeString>&);
      Any (const RuntimeMap<RuntimeString, std::nullptr_t>&);
      Any (const RuntimeMap<RuntimeString, const Null>&);
      Any (const RuntimeMap<RuntimeString, bool>&);
      Any (const RuntimeMap<RuntimeString, int64_t>&);
      Any (const RuntimeMap<RuntimeString, uint64_t>&);
      Any (const RuntimeMap<RuntimeString, uint32_t>&);
      Any (const RuntimeMap<RuntimeString, int32_t>&);
      Any (const RuntimeMap<RuntimeString, double>&);
    #if ORO_RUNTIME_PLATFORM_APPLE
      Any (const RuntimeMap<RuntimeString, size_t>&);
      Any (const RuntimeMap<RuntimeString, ssize_t>&);
    #elif !ORO_RUNTIME_PLATFORM_WINDOWS
      Any (const RuntimeMap<RuntimeString, long long>&);
    #endif

      Any (const Boolean&);
      Any (const Number&);
      Any (const String&);
      Any (const Object&);
      Any (const Array&);
      Any (const Raw&);
      Any (const Error&);
    #if ORO_RUNTIME_PLATFORM_APPLE
      Any (const NSError*);
    #elif ORO_RUNTIME_PLATFORM_LINUX
      Any (const GError*);
    #endif

      Any (const ArrayEntries&);
      Any (const ObjectEntries&);

      Any ();
      Any (Any&&);
      Any (const Any& any);
      ~Any ();

      Any& operator = (const Any&);
      Any& operator = (Any&&);

      Any& operator[] (const char*);
      Any& operator[] (const RuntimeString&);
      Any& operator[] (const unsigned int);

      bool operator == (const Any&) const;
      bool operator != (const Any&) const;

      template <typename T> T& as () const;
      Any& at (const RuntimeString&);
      Any& at (const unsigned int);
      const SharedEntityPointer value () const override;
      const RuntimeString str () const override;
  };

  class Raw : public Value<RuntimeString, Type::Raw> {
    public:
      static Type valueType;
      Raw () = default;
      Raw (const Raw&);
      Raw (Raw&&);
      Raw (const Raw*);
      Raw (const Any&);
      Raw (const RuntimeString&);
      Raw& operator = (const Raw&);
      Raw& operator = (Raw&&);
      const RuntimeString value () const override;
      const RuntimeString str () const override;
  };

  class Object : public Value<ObjectEntries, Type::Object> {
    public:
      static Type valueType;
      using Entries = ObjectEntries;
      using const_iterator = Entries::const_iterator;
      using iterator = Entries::iterator;
      Object () = default;
    #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
      Object (JSCValue*);
    #endif
      Object (const RuntimeMap<RuntimeString, RuntimeString>& entries);
      Object (const Object::Entries& entries);
      Object (const RuntimeMap<RuntimeString, std::nullptr_t>&);
      Object (const RuntimeMap<RuntimeString, const Null>&);
      Object (const RuntimeMap<RuntimeString, bool>&);
      Object (const RuntimeMap<RuntimeString, int64_t>&);
      Object (const RuntimeMap<RuntimeString, uint64_t>&);
      Object (const RuntimeMap<RuntimeString, uint32_t>&);
      Object (const RuntimeMap<RuntimeString, int32_t>&);
      Object (const RuntimeMap<RuntimeString, double>&);
    #if ORO_RUNTIME_PLATFORM_APPLE
      Object (const RuntimeMap<RuntimeString, size_t>&);
      Object (const RuntimeMap<RuntimeString, ssize_t>&);
    #elif !ORO_RUNTIME_PLATFORM_WINDOWS
      Object (const RuntimeMap<RuntimeString, long long>&);
    #endif
      Object (const Object&);
      Object (const Error&);

      Any& operator[] (const char*);
      Any& operator[] (const RuntimeString&);

      const RuntimeString str () const override;
      const Object::Entries value () const override;
      const Any& get (const RuntimeString&) const;
      const Any& get (const RuntimeString&);
      Any& at (const RuntimeString&);
      void set (const RuntimeString&, const Any&);
      bool has (const RuntimeString&) const;
      bool contains (const RuntimeString&) const;
      Entries::size_type size () const;
      const const_iterator begin () const noexcept;
      const const_iterator end () const noexcept;
      iterator begin () noexcept;
      iterator end () noexcept;
  };

  class Array : public Value<ArrayEntries, Type::Array> {
    public:
      static Type valueType;
      using Entries = ArrayEntries;
      using const_iterator = Entries::const_iterator;
      using iterator = Entries::iterator;
      Array () = default;
      Array (const Array&);
      Array (const Array::Entries&);
    #if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_PLATFORM_ANDROID
      Array (JSCValue*);
    #endif

      Any& operator[] (const unsigned int);
      const Any& operator[] (const unsigned int) const;

      const RuntimeString str () const override;
      const Array::Entries value () const override;
      bool has (const unsigned int) const;
      Entries::size_type size () const;
      const Any& get (const unsigned int) const;
      Any& at (const unsigned int);
      void set (const unsigned int, const Any&);
      void push (Any);
      const Any pop ();
      const const_iterator begin () const noexcept;
      const const_iterator end () const noexcept;
      iterator begin () noexcept;
      iterator end () noexcept;
  };

  class Boolean : public Value<bool, Type::Boolean> {
    public:
      static Type valueType;
      Boolean () = default;
      Boolean (const Boolean&);
      Boolean (bool);
      Boolean (int);
      Boolean (int64_t);
      Boolean (double);
      Boolean (void*);
      Boolean (const RuntimeString&);

      const bool value () const override;
      const RuntimeString str () const override;
  };

  class Number : public Value<double, Type::Number> {
    public:
      static Type valueType;
      Number () = default;
      Number (const Number&);
      Number (double);
      Number (char);
      Number (int32_t);
      Number (int64_t);
      Number (uint32_t);
      Number (uint64_t);
      Number (bool);
      Number (const RuntimeString&);

      const double value () const override;
      const RuntimeString str () const override;
  };

  class String : public Value<::oro::runtime::types::String, Type::String> {
    public:
      static Type valueType;
      String () = default;
      String (const String&);
      String (const ::oro::runtime::types::String&);
      String (const char);
      String (const char*);
      String (const Any&);
      String (const Number&);
      String (const Boolean&);
      String (const Error&);

      const ::oro::runtime::types::String str () const override;
      const ::oro::runtime::types::String value () const override;
      ::oro::runtime::types::String::size_type size () const;
  };

  extern const Null null;
  extern const Any nullAny;

  Any parse (const RuntimeString& source);
  Any parse (const char* source);

  // Serialize a JSON value to its canonical string form.
  inline RuntimeString stringify (const Any& any) {
    return any.str();
  }

  // Convenience overloads for concrete types.
  inline RuntimeString stringify (const Object& object) {
    return object.str();
  }

  inline RuntimeString stringify (const Array& array) {
    return array.str();
  }

  inline const auto typeof (const Any& any) {
    return any.typeof();
  }
}
#endif
