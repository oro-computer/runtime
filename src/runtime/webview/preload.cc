#include "../filesystem.hh"
#include "../webview.hh"
#include "../string.hh"
#include "../crypto.hh"
#include "../config.hh"
#include "../http.hh"
#include "../env.hh"
#include "../url.hh"

using oro::runtime::config::getUserConfig;
using oro::runtime::config::getDevHost;
using oro::runtime::config::getDevPort;

using oro::runtime::url::decodeURIComponent;
using oro::runtime::url::encodeURIComponent;
using oro::runtime::http::toHeaderCase;
using oro::runtime::crypto::rand64;
using oro::runtime::string::replace;
using oro::runtime::string::join;
using oro::runtime::string::tmpl;
using oro::runtime::string::trim;

namespace oro::runtime::webview {
  static constexpr auto DEFAULT_REFERRER_POLICY = "unsafe-url";
  static constexpr auto RUNTIME_PRELOAD_META_BEGIN_TAG = (
    R"HTML(<meta name="begin-runtime-preload">)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_META_END_TAG = (
    R"HTML(<meta name="end-runtime-preload">)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG = (
    R"HTML(<script type="text/javascript">)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_JAVASCRIPT_END_TAG = (
    R"HTML(</script>)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_MODULE_BEGIN_TAG = (
    R"HTML(<script type="module">)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_MODULE_END_TAG = (
    R"HTML(</script>)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_IMPORTMAP_BEGIN_TAG = (
    R"HTML(<script type="importmap">)HTML"
  );

  static constexpr auto RUNTIME_PRELOAD_IMPORTMAP_END_TAG = (
    R"HTML(</script>)HTML"
  );

  const Preload Preload::compile (const Options& options) {
    auto preload = Preload(options);
    preload.compile();
    return preload;
  }

  Preload::Preload (const Options& options)
    : options(options) {
    this->configure();
    if (this->options.userConfig.size() == 0) {
      this->options.userConfig = getUserConfig();
    }
  }

  void Preload::configure () {
    this->configure(this->options);
  }

  void Preload::configure (const Options& options) {
    this->options = options;
    this->headers = this->options.headers;
    this->metadata = this->options.metadata;

    if (this->options.features.useHTMLMarkup) {
      if (!this->metadata.contains("referrer")) {
        this->metadata["referrer"] = DEFAULT_REFERRER_POLICY;
      }

      if (!this->headers.has("referrer-policy")) {
        if (this->metadata["referrer"].size() > 0) {
          this->headers.set("referrer-policy", this->metadata["referrer"]);
        }
      }
    }

    auto resolveEnv = [](const char* preferred, const char* legacy) -> String {
      auto value = runtime::env::get(preferred);
      if (value.size() == 0) {
        value = runtime::env::get(legacy);
      }
      return value;
    };

    if (const auto v = resolveEnv("ORO_VM_DEBUG", "ORO_RUNTIME_VM_DEBUG"); v.size() > 0) {
      this->options.env["ORO_VM_DEBUG"] = v;
    }

    if (const auto v = resolveEnv("ORO_NPM_DEBUG", "ORO_RUNTIME_NPM_DEBUG"); v.size() > 0) {
      this->options.env["ORO_NPM_DEBUG"] = v;
    }

    if (const auto v = resolveEnv("ORO_SHARED_WORKER_DEBUG", "ORO_RUNTIME_SHARED_WORKER_DEBUG"); v.size() > 0) {
      this->options.env["ORO_SHARED_WORKER_DEBUG"] = v;
    }

    if (const auto v = resolveEnv("ORO_SERVICE_WORKER_DEBUG", "ORO_RUNTIME_SERVICE_WORKER_DEBUG"); v.size() > 0) {
      this->options.env["ORO_SERVICE_WORKER_DEBUG"] = v;
    }
  }

  Preload& Preload::append (const String& source) {
    this->buffer.push_back(source);
    return *this;
  }

  const String& Preload::compile () {
    Vector<String> buffers;

    auto args = JSON::Object {
      JSON::Object::Entries {
        {"argv", JSON::Array {}},
        {"client", JSON::Object {}},
        {"conduit", this->options.conduit},
        {"config", JSON::Object {}},
        {"debug", this->options.debug},
        {"headless", this->options.headless},
        {"env", JSON::Object {}},
        {"index", this->options.index}
      }
    };

    for (const auto& value : this->options.argv) {
      args["argv"].as<JSON::Array>().push(value);
    }

    // 1. compile metadata if `options.features.useHTMLMarkup == true`, otherwise skip
    if (this->options.features.useHTMLMarkup) {
      for (const auto &entry : this->metadata) {
        if (entry.second.size() == 0) {
          continue;
	      }

        buffers.push_back(tmpl(
          R"HTML(<meta name="{{name}}" content="{{content}}">)HTML",
          Map<String, String> {
            {"name", trim(entry.first)},
            {"content", trim(decodeURIComponent(entry.second))}
          }
        ));
      }
    }

    // 2. compile headers if `options.features.useHTMLMarkup == true`, otherwise skip
    if (this->options.features.useHTMLMarkup) {
      for (const auto &entry : this->headers) {
        buffers.push_back(tmpl(
          R"HTML(<meta http-equiv="{{header}}" content="{{value}}">)HTML",
          Map<String, String> {
            {"header", toHeaderCase(entry.name)},
            {"value", trim(decodeURIComponent(entry.value.str()))}
          }
        ));
      }
    }

    // 3. compile preload `<meta name="begin-runtime-preload">` "BEGIN" tag if `options.features.useHTMLMarkup == true`, otherwise skip
    if (this->options.features.useHTMLMarkup) {
      buffers.push_back(RUNTIME_PRELOAD_META_BEGIN_TAG);
    }

    if (this->options.features.useGlobalArgs) {
      // 4. compile preload `<script>` prefix if `options.features.useHTMLMarkup == true`, otherwise skip
      if (this->options.features.useHTMLMarkup) {
        buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG);
      }

      // 5. compile `globalThis.__args` object
      buffers.push_back(";(() => {");
      buffers.push_back(trim(tmpl(
        R"JAVASCRIPT(
          if (globalThis.__RUNTIME_INIT_NOW__) return
          if (!globalThis.__args) {
            Object.defineProperty(globalThis, '__args', {
              configurable: false,
              enumerable: false,
              writable: false,
              value: {}
            })
            Object.defineProperties(globalThis.__args, {
              argv: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {{argv}}
              },
              client: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {}
              },
              conduit: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {{conduit}}
              },
              config: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {}
              },
              debug: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {{debug}}
              },
              env: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {}
              },
              headless: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {{headless}}
              },
              index: {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {{index}}
              },
            })
          }
        )JAVASCRIPT",
        Map<String, String> {
          {"argv", args["argv"].str()},
          {"conduit", args["conduit"].str()},
          {"debug", args["debug"].str()},
          {"headless", args["headless"].str()},
          {"index", args["index"].str()},
        }
      )));

      buffers.push_back(R"JAVASCRIPT(
        if (typeof globalThis.__ORO_RUNTIME_CLIENT_ORIGIN__ !== 'function') {
          globalThis.__ORO_RUNTIME_CLIENT_ORIGIN__ = () => {
            const host = globalThis.__args?.client?.host
            const port = globalThis.__args?.client?.port
            return host && port ? host + ':' + port : ''
          }
        }

        if (typeof globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__ !== 'function') {
          globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__ = (origin) => {
            if (typeof origin !== 'string' || origin.length === 0) return false

            const bundleIdentifier = globalThis.__args?.config?.meta_bundle_identifier
            if (typeof bundleIdentifier === 'string' && bundleIdentifier.length > 0) {
              try {
                if (origin.includes(bundleIdentifier.toLowerCase())) {
                  return true
                }
              } catch {}
            }

            const clientOrigin = globalThis.__ORO_RUNTIME_CLIENT_ORIGIN__()
            if (clientOrigin && origin.includes(clientOrigin)) {
              return true
            }

            return false
          }
        }
      )JAVASCRIPT");

      // Capabilities probe for incremental streaming support (SSE/chunked)
      {
      #if ORO_RUNTIME_PLATFORM_WINDOWS
        const bool sseIncremental = false;
        const bool chunkedIncremental = false;
      #else
        const bool sseIncremental = true;
        const bool chunkedIncremental = true;
      #endif
        buffers.push_back(tmpl(
          R"JAVASCRIPT(
            if (!globalThis.__args.capabilities) {
              Object.defineProperty(globalThis.__args, 'capabilities', {
                configurable: false,
                enumerable: true,
                writable: false,
                value: {
                  streaming: {
                    sseIncremental: {{sse}},
                    chunkedIncremental: {{chunked}}
                  }
                }
              })
            }
          )JAVASCRIPT",
          Map<String, String> {
            {"sse", sseIncremental ? "true" : "false"},
            {"chunked", chunkedIncremental ? "true" : "false"}
          }
        ));
      }

      // 6. compile `globalThis.__args.client` values
      static std::regex platformPattern("^mac$", std::regex_constants::icase);
      static const auto platformHost = getDevHost();
      static const auto platformPort = getDevPort();
      buffers.push_back(trim(tmpl(
        R"JAVASCRIPT(
        if (!globalThis.__args.client?.id) {
          Object.defineProperties(globalThis.__args.client, {
            id: {
              configurable: false,
              enumerable: true,
              get: () => {
                return globalThis.window && globalThis.top !== globalThis.window
                  ? '{{id}}'
                  : globalThis.window && globalThis.top
                    ? '{{clientId}}'
                    : null
              }
            },
            type: {
              configurable: false,
              enumerable: true,
              writable: true,
              value: globalThis.window ? 'window' : 'worker'
            },
            platform: {
              configurable: false,
              enumerable: true,
              writable: true,
              value: '{{platform}}'
            },
            host: {
              configurable: false,
              enumerable: true,
              writable: true,
              value: '{{host}}' || null
            },
            port: {
              configurable: false,
              enumerable: true,
              writable: true,
              value: {{port}} || null
            },
            parent: {
              configurable: false,
              enumerable: true,
              get: () => {
                if (globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)) {
                  return globalThis.parent !== globalThis
	                  ? globalThis.parent?.__args?.client ?? null
	                  : null
                }

                return null
              }
            },
            top: {
              configurable: false,
              enumerable: true,
              get: () => {
                if (globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)) {
                  return globalThis.top
	                  ? globalThis.top.__args?.client ?? null
	                  : globalThis.__args.client
                }

                return null
              }
            },
            frameType: {
              configurable: false,
              enumerable: true,
              get: () => {
                return globalThis.window && globalThis.top !== globalThis.window
                  ? 'nested'
                  : globalThis.window && globalThis.top
                    ? 'top-level'
                    : 'none'
              }
            },
          })
        }
        )JAVASCRIPT",
        Map<String, String> {
          {"id", std::to_string(rand64())},
          {"clientId", std::to_string(this->options.client.id)},
          {"platform", std::regex_replace(platform.os, platformPattern, "darwin")},
          {"host", platformPort > 0 ? platformHost : ""},
          {"port", std::to_string(platformPort)}
        }
      )));

      // 7. compile `globalThis.__args.config` values
      buffers.push_back(R"JAVASCRIPT(const __RAW_CONFIG__ = {})JAVASCRIPT");

      for (const auto& entry : this->options.userConfig) {
        const auto key = trim(entry.first);
        const auto value = trim(entry.second);

        // skip empty key/value and comments
        if (key.size() == 0 || value.size() == 0 || key.rfind(";", 0) == 0 || key.rfind("#", 0) == 0) {
          continue;
        }

        buffers.push_back(tmpl(
          R"JAVASCRIPT(__RAW_CONFIG__['{{key}}'] = '{{value}}')JAVASCRIPT",
          Map<String, String> {{"key", key}, {"value", encodeURIComponent(value)}}
        ));
      }

      buffers.push_back(R"JAVASCRIPT(
        for (const key in __RAW_CONFIG__) {
          let value = __RAW_CONFIG__[key]

          try { value = decodeURIComponent(value) } catch {}

          if (value === 'true') {
            value = true
          } else if (value === 'false') {
            value = false
          } else if (value === 'null') {
            value = null
          } else if (value === 'NaN') {
            value = Number.NaN
          } else if (value.startsWith('0x')) {
            const parsed = parseInt(value.slice(2), 16)
            if (!Number.isNaN(parsed)) {
              value = parsed
            }
          } else {
            const parsed = parseFloat(value)
            if (!Number.isNaN(parsed)) {
              value = parsed
            }
          }

          try { value = JSON.parse(value) } catch {}

          globalThis.__args.config[key] = value
          if (key.startsWith('env_')) {
            globalThis.__args.env[key.slice(4)] = value
          }
        }
      )JAVASCRIPT");

      // 8. compile `globalThis.__args.env` values
      buffers.push_back(R"JAVASCRIPT(const __RAW_ENV__ = {})JAVASCRIPT");

      for (const auto& entry : this->options.env) {
        const auto key = trim(entry.first);
        const auto value = trim(entry.second);

        // skip empty keys and values
        if (key.size() == 0 || value.size() == 0) {
          continue;
        }

        buffers.push_back(tmpl(
          R"JAVASCRIPT(__RAW_ENV__['{{key}}'] = '{{value}}')JAVASCRIPT",
          Map<String, String> {{"key", key}, {"value", encodeURIComponent(value)}}
        ));
      }

      buffers.push_back(R"JAVASCRIPT(
        for (const key in __RAW_ENV__) {
          let value = __RAW_ENV__[key]
          try { value = decodeURIComponent(value) } catch {}
          try { value = JSON.parse(value) } catch {}
          globalThis.__args.env[key] = value
        }
      )JAVASCRIPT");

      // 9. compile test script import if `options.features.useTestScript == true`
      if (this->options.features.useTestScript && this->options.index == 0) {
        String pathname;
        for (const auto& value : this->options.argv) {
          const auto start = value.find("--test=");
          if (start != String::npos) {
            auto end = value.find("'", start);

            if (end == String::npos) {
              end = value.size();
            }

            const auto file = value.substr(start + 7, end - start - 7);
            if (file.size() > 0) {
              pathname = file;
              break;
            }
          }
        }

        if (pathname.size() == 0) {
          buffers.push_back(R"JAVASCRIPT(
            console.warn('Preload: A test script entry was requested, but was not given')
          )JAVASCRIPT");
        } else {
          buffers.push_back(tmpl(
            R"JAVASCRIPT(
              if (!globalThis.RUNTIME_TEST_FILENAME) {
                Object.defineProperty(globalThis, 'RUNTIME_TEST_FILENAME', {
                  configurable: false,
                  enumerable: false,
                  writable: false,
                  value: String(new URL('{{pathname}}', globalThis.location.href)
                })
              }
            )JAVASCRIPT",
            Map<String, String> {{"pathname", pathname}}
          ));
        }
      }

      // 10. compile listeners for `globalThis`
      buffers.push_back(R"JAVASCRIPT(
        if (
          globalThis.document &&
          !globalThis.RUNTIME_APPLICATION_URL_EVENT_BACKLOG &&
          globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)
        ) {
          Object.defineProperties(globalThis, {
            RUNTIME_APPLICATION_URL_EVENT_BACKLOG: {
              configurable: false,
              enumerable: false,
              writable: false,
              value: []
            }
          })

          globalThis.document.addEventListener('readystatechange', async (e) => {
            const ipc = await import('oro:ipc')
            ipc.send('platform.event', {
              value: 'readystatechange',
              state: globalThis.document.readyState
            })
          })

          globalThis.addEventListener('applicationurl', (event) => {
            if (globalThis.document.readyState !== 'complete') {
              globalThis.RUNTIME_APPLICATION_URL_EVENT_BACKLOG.push(event)
            }
          })

          globalThis.addEventListener('__runtime_init__', () => {
            const backlog = globalThis.RUNTIME_APPLICATION_URL_EVENT_BACKLOG
            if (Array.isArray(backlog)) {
              for (const event of backlog) {
                if (typeof ApplicationURLEvent === 'function') {
                  globalThis.dispatchEvent(new ApplicationURLEvent(event.type, event))
                }
              }

              backlog.splice(0, backlog.length)
            }
          }, { once: true })

          if (
            globalThis.__args.config.webview_watch === true &&
            globalThis.__args.config.webview_watch_reload !== false
          ) {
            globalThis.addEventListener('filedidchange', () => {
              globalThis.location.reload()
            })
          }
        }
      )JAVASCRIPT");

      // 11. freeze `globalThis.__args` values
      buffers.push_back(R"JAVASCRIPT(
        try { Object.freeze(globalThis.__args.client) } catch {}
        try { Object.freeze(globalThis.__args.config) } catch {}
        try { Object.freeze(globalThis.__args.argv) } catch {}
        try { Object.freeze(globalThis.__args.env) } catch {}
      )JAVASCRIPT");
      buffers.push_back("})();");

      if (this->options.features.useHTMLMarkup) {
        buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_END_TAG);
      }

      // 12. compile preload `<script>` prefix if `options.features.useHTMLMarkup == true`, otherwise skip
      if (this->options.features.useHTMLMarkup) {
        if (this->options.features.useESM) {
          buffers.push_back(RUNTIME_PRELOAD_MODULE_BEGIN_TAG);
        } else {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG);
        }
      }

      // 13. compile "internal init" import
      if (this->options.features.useHTMLMarkup && this->options.features.useESM) {
        buffers.push_back(tmpl(
          R"JAVASCRIPT(
            import 'oro:internal/init'
            {{userScript}}
          )JAVASCRIPT",
          Map<String, String> {{"userScript", this->options.userScript}}
        ));
      } else {
        buffers.push_back(";(() => {");
        buffers.push_back(tmpl(
          R"JAVASCRIPT(
            let userScriptExecuted = false
            async function userScriptCallback () {
              if (userScriptExecuted) return; else userScriptExecuted = true;
              {{userScript}}
            }

            if (globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)) {
              if (globalThis.document && globalThis.document.readyState !== 'complete') {
                globalThis.document.addEventListener('readystatechange', () => {
                  if(/interactive|complete/.test(globalThis.document.readyState)) {
                    import('oro:internal/init')
                      .then(userScriptCallback)
                      .catch(console.error)
                  }
                })
              } else {
                import('oro:internal/init')
                  .then(userScriptCallback)
                  .catch(console.error)
              }
            }
          )JAVASCRIPT",
          Map<String, String> {{"userScript", this->options.userScript}}
        ));
        buffers.push_back("})();");
      }

      // 14. compile preload `</script>` prefix if `options.features.useHTMLMarkup == true`, otherwise skip
      if (this->options.features.useHTMLMarkup) {
        if (this->options.features.useESM) {
          buffers.push_back(RUNTIME_PRELOAD_MODULE_END_TAG);
        } else {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_END_TAG);
        }
      }

      // 15. compile "global CommonJS" if `options.features.useGlobalCommonJS == true`, otherwise skip
      if (this->options.features.useGlobalCommonJS) {
        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG);
        }

        buffers.push_back(R"JAVASCRIPT(
          if (
            globalThis.document &&
            !globalThis.module &&
            globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)
          ) {
            ;(async function GlobalCommonJSScope () {
              const globals = await import('oro:internal/globals')
              await globals.get('RuntimeReadyPromise')

              const href = encodeURIComponent(globalThis.location.href)
              const source = `oro:module?ref=${href}`

              const { Module } = await import(source)
              const path = await import('oro:path')
              const require = Module.createRequire(globalThis.location.href)
              const __filename = Module.main.filename
              const __dirname = path.dirname(__filename)

              Object.defineProperties(globalThis, {
                module: {
                  configurable: true,
                  enumerable: false,
                  writable: false,
                  value: Module.main.scope
                },
                require: {
                  configurable: true,
                  enumerable: false,
                  writable: false,
                  value: require
                },
                __dirname: {
                  configurable: true,
                  enumerable: false,
                  writable: false,
                  value: __dirname
                },
                __filename: {
                  configurable: true,
                  enumerable: false,
                  writable: false,
                  value: __filename
                }
              })

              // reload global CommonJS scope on 'popstate' events
              globalThis.addEventListener('popstate', GlobalCommonJSScope)
            })();
          }
        )JAVASCRIPT");
        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_END_TAG);
        }
      }

      // 16. compile "global NodeJS" if `options.features.useGlobalNodeJS == true`, otherwise skip
      if (this->options.features.useGlobalNodeJS) {
        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG);
        }
        buffers.push_back(R"JAVASCRIPT(
          if (
            globalThis.document &&
            !globalThis.process &&
            globalThis.__ORO_RUNTIME_ORIGIN_MATCHES__(globalThis.origin)
          ) {
            ;(async function GlobalNodeJSScope () {
              const process = await import('oro:process')
              Object.defineProperties(globalThis, {
                process: {
                  configurable: false,
                  enumerable: false,
                  writable: false,
                  value: process.default
                },

                global: {
                  configurable: false,
                  enumerable: false,
                  writable: false,
                  value: globalThis
                }
              })
            })();
          }
        )JAVASCRIPT");
        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_END_TAG);
        }
      }

      // 17. compile `__RUNTIME_PRIMORDIAL_OVERRIDES__` -- assumes value is a valid JSON string
      if (this->options.RUNTIME_PRIMORDIAL_OVERRIDES.size() > 0) {
        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_BEGIN_TAG);
        }

        buffers.push_back(tmpl(
          R"JAVASCRIPT(
            if (!('__RUNTIME_PRIMORDIAL_OVERRIDES__' in globalThis) {
              Object.defineProperty(globalThis, '__RUNTIME_PRIMORDIAL_OVERRIDES__', {
                configurable: false,
                enumerable: false,
                writable: false,
                value: {{RUNTIME_PRIMORDIAL_OVERRIDES}}
              })
            }
          )JAVASCRIPT",
          Map<String, String> {{"RUNTIME_PRIMORDIAL_OVERRIDES", this->options.RUNTIME_PRIMORDIAL_OVERRIDES}}
        ));

        if (this->options.features.useHTMLMarkup) {
          buffers.push_back(RUNTIME_PRELOAD_JAVASCRIPT_END_TAG);
        }
      }
    }

    // 18. compile preload `<meta name="end-runtime-preload">` "END" tag if `options.features.useHTMLMarkup == true`, otherwise skip
    if (this->options.features.useHTMLMarkup) {
      buffers.push_back(RUNTIME_PRELOAD_META_END_TAG);
    }

    // 19. precompute capacity and clear existing compiled state
    size_t totalLen = 0;
    for (const auto& s : buffers) totalLen += (s.size() + 1);
    String userJoined;
    if (!this->buffer.empty()) {
      userJoined = join(this->buffer, '\n');
      totalLen += (userJoined.size() + 8); // rough overhead for wrappers
    }
    this->compiled.clear();
    this->compiled.reserve(totalLen + 32);

    // 20. compile core buffers
    for (const auto& buffer : buffers) {
      this->compiled += buffer;
      this->compiled += "\n";
    }
    // 21. user preload buffers
    if (!userJoined.empty()) {
      this->compiled += ";(() => {\n";
      this->compiled += userJoined;
      this->compiled += ")();\n";
    }
    if (this->options.features.useHTMLMarkup == false) {
      // 22. compile 'sourceURL' source map value if `options.features.useHTMLMarkup == false`
      this->compiled += "//# sourceURL=oro:<runtime>/preload.js";
    }
    return this->compiled;
  }

  const String& Preload::str () const {
    return this->compiled;
  }

  const String Preload::insertIntoHTML (
    const String& html,
    const InsertIntoHTMLOptions& options
  ) const {
    if (html.size() == 0) {
      return "";
    }

    auto protocolHandlerSchemes = options.protocolHandlerSchemes;
    auto preload = this->str();
    auto output = html;
    if (
      html.find(RUNTIME_PRELOAD_IMPORTMAP_BEGIN_TAG) == String::npos &&
      this->options.userConfig.contains("webview_importmap") &&
      this->options.userConfig.at("webview_importmap").size() > 0
    ) {
      auto resource = filesystem::Resource(Path(this->options.userConfig.at("webview_importmap")));

      if (resource.exists()) {
        const auto bytes = reinterpret_cast<const char*>(resource.read());

        if (bytes != nullptr) {
          preload = (
            tmpl(
              R"HTML(<script type="importmap">{{importmap}}</script>)HTML",
              Map<String, String> {{"importmap", String(bytes, resource.size())}}
           ) + preload
          );
        }
      }
    }

    protocolHandlerSchemes.push_back("node:");
    protocolHandlerSchemes.push_back("npm:");

    output = tmpl(output, Map<String, String> {
      {"protocol_handlers", join(protocolHandlerSchemes, " ")}
    });

    if (output.find("<meta name=\"runtime-preload-injection\" content=\"disabled\"") != String::npos) {
      preload = "";
    } else if (output.find("<meta content=\"disabled\" name=\"runtime-preload-injection\"") != String::npos) {
      preload = "";
    }

    const auto existingImportMapCursor = output.find(RUNTIME_PRELOAD_IMPORTMAP_BEGIN_TAG);
    bool preloadWasInjected = false;

    if (existingImportMapCursor != String::npos) {
      const auto closingScriptTag = output.find(
        RUNTIME_PRELOAD_JAVASCRIPT_END_TAG,
        existingImportMapCursor
      );

      if (closingScriptTag != String::npos) {
        output = (
          output.substr(0, closingScriptTag + 9) +
          preload +
          output.substr(closingScriptTag + 9)
        );

        preloadWasInjected = true;
      }
    }

    if (!preloadWasInjected) {
      if (output.find("<head>") != String::npos) {
        output = replace(output, "<head>", String("<head>" + preload));
      } else if (output.find("<body>") != String::npos) {
        output = replace(output, "<body>", String("<body>" + preload));
      } else if (output.find("<html>") != String::npos) {
        output = replace(output, "<html>", String("<html>" + preload));
      } else {
        output = preload + output;
      }
    }

    return output;
  }
}
