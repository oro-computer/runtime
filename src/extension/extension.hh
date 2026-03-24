#ifndef ORO_RUNTIME_EXTENSION_EXTENSION_H
#define ORO_RUNTIME_EXTENSION_EXTENSION_H

#include "../runtime.hh"

#if ORO_RUNTIME_PLATFORM_WINDOWS
#define ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME ".dll"
#else
#define ORO_RUNTIME_EXTENSION_FILENAME_EXTNAME ".so"
#endif

#undef ORO_RUNTIME_EXTENSION_H
#include "../../include/oro/extension.h"

namespace oro::extension {
  using namespace oro::runtime;
  class Extension {
    public:
      struct Context {
        enum class State {
          Error = -1,
          None = 0,
          Init = 1,
          Idle = 2,
          Route = 3
        };

        struct Error {
          int code = 0;
          String name = "";
          String message = "";
          String location = "";
        };

        struct Policy {
          String name;
          bool allowed = false;
          Policy (const String& name, bool allowed)
            : name(name), allowed(allowed)
          {}
        };

        struct Memory {
          std::vector<std::function<void()>> pool;
          Mutex mutex;

          ~Memory ();
          void release ();
          void push (std::function<void()> callback);
          template <typename T, typename C, typename... Args> T* alloc (
            C* ctx,
            Args... args
          ) {
            auto memory = new T(args...);
            memory->context = ctx;
            push([memory]() { delete memory; });
            return memory;
          }

          template <typename T, typename... Args> T* alloc (Args... args) {
            auto memory = new T(args...);
            push([memory]() { delete memory; });
            return memory;
          }

          template <typename T> T* alloc (size_t size) {
            auto memory = new T[size]{0};
            push([memory]() { delete [] memory; });
            return memory;
          }
        };

        using PolicyMap = Map<String, Policy>;

        const Extension* extension = nullptr;
        ipc::Router* router = nullptr;
        Context* context = nullptr;
        void *internal = nullptr;
        const void *data = nullptr;

        Memory memory;
        State state = State::None;
        Error error;
        std::atomic<unsigned int> retain_count = 0;
        PolicyMap policies;
        Map<String, String> config;

        Context () = default;
        Context (const Extension* extension);
        Context (const Context* context);
        Context (const Context& context);
        Context (ipc::Router* router);
        Context (const Context& context, ipc::Router* router);

        void retain ();
        bool release ();

        void setPolicy (const String& name, bool allowed);
        const Policy& getPolicy (const String& name) const;
        bool hasPolicy (const String& name) const;
        bool isAllowed (const String& name) const;
      };

      using Map = Map<String, SharedPointer<Extension>>;
      using Entry = SharedPointer<const Extension>;
      using Initializer = Function<bool(Context*, const void*)>;
      using Deinitializer = Function<bool(Context*, const void*)>;
      using RouterContexts = runtime::Map<ipc::Router*, Context*>;

      RouterContexts contexts;
      Context context;
      const void *data = nullptr;
      const oapi_extension_registration_t* registration = nullptr;

      // registration
      String name = "";
      String version = "";
      String description = "";
      unsigned long abi = 0;
      String path = "";
      String type = "";
      void *handle = nullptr;
      Initializer initializer = nullptr;
      Deinitializer deinitializer = nullptr;

      static String getExtensionsDirectory (const String& name);
      static const Extension::Map& all ();
      static const Entry get (const String& name);
      static Context* getContext (const String& name);
      static bool setHandle (const String& name, void* handle);
      static void create (const String& name, const Initializer initializer);
      static bool load (const String& name);
      static bool unload (Context* ctx, const String& name, bool shutdown);
      static bool isLoaded (const String& name);
      static bool isInitialized (const String& name);
      static String getExtensionType (const String& name);
      static String getExtensionPath (const String& name);

      static void setRouterContext (
        const String& name,
        ipc::Router* router,
        Context* context
      );

      static Context* getRouterContext (
        const String& name,
        ipc::Router* router
      );

      static void removeRouterContext (
        const String& name,
        ipc::Router* router
      );

      static bool initialize (
        Context* ctx,
        const String& name,
        const void* data = nullptr // owned by caller
      );

      Extension (const String& name, const Initializer initializer);
      Extension (Extension& extension);
  };
}

extern "C" {
  typedef oapi_extension_registration_t* (*oapi_extension_registration_entry)();

  struct oapi_context : public oro::extension::Extension::Context {
    oapi_context () = default;
    oapi_context (oapi_context* ctx)
      : oro::extension::Extension::Context(
          reinterpret_cast<oro::extension::Extension::Context*>(ctx)
        )
    {}
  };

  struct oapi_process_exec : public oro::runtime::process::ExecOutput {
    oapi_context_t* context = nullptr;
    oapi_process_exec () = default;
    oapi_process_exec (const oro::runtime::process::ExecOutput& result)
      : oro::runtime::process::ExecOutput(result)
    {}
    oapi_process_exec (oapi_context_t* ctx, const oro::runtime::process::ExecOutput& result)
      : context(ctx),
        oro::runtime::process::ExecOutput(result)
    {}
  };

  struct oapi_process_spawn : public oro::runtime::Process {
    public:
      oapi_context_t* context = nullptr;

      oapi_process_spawn (
        const char* command,
        const char* argv,
        oro::runtime::Vector<oro::runtime::String> env,
        const char* path,
        oapi_process_spawn_stderr_callback_t onstdout,
        oapi_process_spawn_stderr_callback_t onstderr,
        oapi_process_spawn_exit_callback_t onexit
      ) : oro::runtime::process::Process(
        command,
        argv,
        env,
        path,
        [this, onstdout] (auto output) {
          if (onstdout) {
            onstdout(this, output.c_str(), output.size());
          }
        },
        [this, onstderr] (auto output) {
          if (onstderr) {
            onstderr(this, output.c_str(), output.size());
          }
        },
        [this, onexit] (auto code) {
          if (onexit) {
            onexit(this, std::stoi(code));
          }
        }
      ) {}
  };

  struct oapi_ipc_router : public oro::runtime::ipc::Router {};
  struct oapi_ipc_message : public oro::runtime::ipc::Message {};

  struct oapi_ipc_result : public oro::runtime::ipc::Result {
    oapi_context_t* context = nullptr;
  };

  struct oapi_json_any : public oro::runtime::JSON::Any {
    oapi_context_t* context = nullptr;
  };

  struct oapi_json_null : public oro::runtime::JSON::Null {
    oapi_context_t* context = nullptr;
  };

  struct oapi_json_object : public oro::runtime::JSON::Object {
    oapi_context_t* context = nullptr;
  };

  struct oapi_json_array : public oro::runtime::JSON::Array {
    oapi_context_t* context = nullptr;
  };

  struct oapi_json_boolean : public oro::runtime::JSON::Boolean {
    oapi_context_t* context = nullptr;
    oapi_json_boolean () = default;
    oapi_json_boolean (bool boolean)
      : oro::runtime::JSON::Boolean(boolean)
    {}
    oapi_json_boolean (oapi_context_t* ctx, bool boolean)
      : context(ctx),
        oro::runtime::JSON::Boolean(boolean)
    {}
  };

  struct oapi_json_number : public oro::runtime::JSON::Number {
    oapi_context_t* context = nullptr;
    oapi_json_number () = default;
    oapi_json_number (int64_t number)
      : oro::runtime::JSON::Number(number)
    {}
    oapi_json_number (oapi_context_t* ctx, int64_t number)
      : context(ctx),
        oro::runtime::JSON::Number(number)
    {}
    oapi_json_number (double number)
      : oro::runtime::JSON::Number(number)
    {}
    oapi_json_number (oapi_context_t* ctx, double number)
      : context(ctx),
        oro::runtime::JSON::Number(number)
    {}
  };

  struct oapi_json_string : public oro::runtime::JSON::String {
    oapi_context_t* context = nullptr;
    oapi_json_string () = default;
    oapi_json_string (const char* string)
      : oro::runtime::JSON::String(string)
    {}
    oapi_json_string (oapi_context_t* ctx, const char* string)
      : context(ctx),
        oro::runtime::JSON::String(string)
    {}
  };

  struct oapi_json_raw : public oro::runtime::JSON::Raw {
    oapi_context_t* context = nullptr;
    oapi_json_raw (
      const char* source
    ) : oro::runtime::JSON::Raw(oro::runtime::String(source))
    {}

    oapi_json_raw (
      oapi_context_t* ctx,
      const char* source
    ) : context(ctx),
        oro::runtime::JSON::Raw(oro::runtime::String(source))
    {}
  };
};

#endif
