#include "cdp.hh"

#include "../../app.hh"
#include "../../bytes.hh"
#include "../../crypto.hh"
#include "../../debug.hh"
#include "../../env.hh"
#include "../../runtime.hh"
#include "../../string.hh"
#include "../../url.hh"
#include "../../version.hh"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
#include <webkit2/webkit2.h>
#include <cairo/cairo.h>
#endif

using oro::runtime::app::App;
using oro::runtime::crypto::SHA1;
using oro::runtime::string::trimRight;
using oro::runtime::string::toLowerCase;

  namespace oro::runtime::core::services {
    static constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    static constexpr uint64_t MAX_WS_MESSAGE_SIZE = 32ULL * 1024ULL * 1024ULL;
    static constexpr size_t MAX_HTTP_HEADER_SIZE = 64 * 1024;
    static constexpr int64_t MAX_SCREENSHOT_PIXELS = 20LL * 1024LL * 1024LL;
    static constexpr int64_t MAX_SCREENSHOT_DIMENSION = 16384;
    static constexpr size_t MAX_SCREENSHOT_PNG_BYTES = 18ULL * 1024ULL * 1024ULL;
    static constexpr size_t MAX_UNHANDLED_METHODS_LOGGED = 1024;
    static constexpr size_t MAX_NEW_DOCUMENT_SCRIPTS_PER_TARGET = 128;
    static constexpr size_t MAX_NEW_DOCUMENT_SCRIPT_BYTES = 1024ULL * 1024ULL;
    static constexpr size_t MAX_NEW_DOCUMENT_SCRIPT_TOTAL_BYTES_PER_TARGET = 4ULL * 1024ULL * 1024ULL;
    static constexpr size_t MAX_PAGE_SET_DOCUMENT_CONTENT_BYTES = 8ULL * 1024ULL * 1024ULL;
    static constexpr size_t MAX_BINDINGS_PER_TARGET = 256;
    static constexpr size_t MAX_NAVIGATION_HISTORY_ENTRIES = 128;
    static constexpr size_t MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES = 5ULL * 1024ULL * 1024ULL;
    static constexpr size_t MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS = ((MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES + 2ULL) / 3ULL) * 4ULL;
	    static constexpr size_t MAX_NETWORK_POSTDATA_RESULT_BYTES = 256ULL * 1024ULL;
	    static constexpr size_t MAX_NATIVE_NETWORK_PAYLOAD_ENTRIES = 256;
	    static constexpr size_t MAX_NATIVE_NETWORK_TOTAL_BODY_BYTES = 20ULL * 1024ULL * 1024ULL;
	    static constexpr size_t MAX_IO_STREAM_ENTRIES = 64;
	    static constexpr size_t MAX_IO_STREAM_TOTAL_BYTES = 20ULL * 1024ULL * 1024ULL;
	    static constexpr size_t MAX_IO_STREAM_READ_BYTES = 256ULL * 1024ULL;
	    static constexpr int MAX_DOM_SERIALIZE_DEPTH = 3;
	    static constexpr size_t MAX_DOM_QUERY_SELECTOR_ALL_RESULTS = 2048;
	    static constexpr size_t MAX_RUNTIME_PROPERTY_DESCRIPTORS = 2048;

  static inline bool screenshotDimensionsAllowed (int64_t w, int64_t h) {
    if (w <= 0 || h <= 0) {
      return false;
    }

    if (w > MAX_SCREENSHOT_DIMENSION || h > MAX_SCREENSHOT_DIMENSION) {
      return false;
    }

    if (w > (MAX_SCREENSHOT_PIXELS / h)) {
      return false;
    }

    return true;
  }

  static inline double monotonicSeconds () {
    return static_cast<double>(uv_hrtime()) / 1e9;
  }

  static inline double wallTimeSeconds () {
    return std::chrono::duration<double>(
      std::chrono::system_clock::now().time_since_epoch()
    ).count();
  }

  static inline bool parseBool (const JSON::Any& value, bool fallback = false) {
    if (value.isBoolean()) {
      return value.as<JSON::Boolean>().value();
    }

    if (value.isNumber()) {
      return value.as<JSON::Number>().value() != 0;
    }

    if (value.isString()) {
      const auto s = toLowerCase(value.as<JSON::String>().value());
      return s == "1" || s == "true" || s == "yes" || s == "on";
    }

    return fallback;
  }

  static inline bool isKnownCDPDomain (const String& domain) {
    static const std::unordered_set<std::string> domains = {
      "Accessibility",
      "Animation",
      "Audits",
      "Audits2",
      "BackgroundFetch",
      "BackgroundService",
      "Browser",
      "CacheStorage",
      "Cast",
      "Console",
      "Coverage",
      "CSS",
      "DOM",
      "DOMDebugger",
      "DOMSnapshot",
      "DOMStorage",
      "DeviceAccess",
      "DeviceOrientation",
      "Emulation",
      "Extensions",
      "Fetch",
      "HeapProfiler",
      "IO",
      "IndexedDB",
      "Input",
      "Inspector",
      "LayerTree",
      "Log",
      "Media",
      "Memory",
      "Network",
      "Overlay",
      "Page",
      "Performance",
      "Profiler",
      "Runtime",
      "Schema",
      "Security",
      "ServiceWorker",
      "Storage",
      "Target",
      "Tracing",
      "WebAudio",
      "WebAuthn"
    };
    return domains.contains(domain);
  }

  bool CDP::clientAttachedToTarget (const Client* client, const String& targetId) const {
    if (!client) {
      return false;
    }

    if (targetId == "browser" || targetId == this->browserId) {
      return true;
    }

    if (client->kind == Client::Kind::PageWS) {
      return client->boundTargetId == targetId;
    }

    if (client->kind == Client::Kind::BrowserWS) {
      for (const auto& kv : client->sessions) {
        if (kv.second.targetId == targetId) {
          return true;
        }
      }
    }

    return false;
  }

  Vector<CDP::AutoAttachOptions::TargetFilterRule> CDP::parseTargetFilter (const JSON::Any& filter) {
    Vector<AutoAttachOptions::TargetFilterRule> out;
    if (!filter.isArray()) {
      return out;
    }

    for (const auto& ruleAny : filter.as<JSON::Array>()) {
      if (!ruleAny.isObject()) {
        continue;
      }

      const auto& ruleObj = ruleAny.as<JSON::Object>();

      AutoAttachOptions::TargetFilterRule rule;
      rule.exclude = parseBool(ruleObj.get("exclude"), false);

      const auto& typeAny = ruleObj.get("type");
      if (typeAny.isString()) {
        rule.type = typeAny.as<JSON::String>().value();
      }

      out.push_back(rule);
    }

    return out;
  }

  bool CDP::targetMatchesFilter (const Vector<AutoAttachOptions::TargetFilterRule>& filter, const String& type) {
    if (filter.empty()) {
      return true;
    }

    bool hasIncludeRule = false;
    bool includeMatch = false;
    bool excludeMatch = false;

    for (const auto& rule : filter) {
      const bool isExclude = rule.exclude;
      if (!isExclude) {
        hasIncludeRule = true;
      }

      if (rule.type.has_value() && rule.type.value() != type) {
        continue;
      }

      if (isExclude) {
        excludeMatch = true;
      } else {
        includeMatch = true;
      }
    }

    if (excludeMatch) {
      return false;
    }

    if (hasIncludeRule) {
      return includeMatch;
    }

    // Exclusion-only filters default to including everything else (CDP-style).
    return true;
  }

  static inline String jsonStringOrEmpty (const JSON::Any& objAny, const char* key) {
    if (!objAny.isObject()) return "";
    const auto& v = objAny.as<JSON::Object>().get(key);
    if (!v.isString()) return "";
    return v.as<JSON::String>().value();
  }

  static inline int jsonIntOr (const JSON::Any& objAny, const char* key, int fallback) {
    if (!objAny.isObject()) return fallback;
    const auto& v = objAny.as<JSON::Object>().get(key);
    if (v.isNumber()) {
      const double d = v.as<JSON::Number>().value();
      if (!std::isfinite(d)) return fallback;
      if (d > static_cast<double>(std::numeric_limits<int>::max())) return fallback;
      if (d < static_cast<double>(std::numeric_limits<int>::min())) return fallback;
      return static_cast<int>(d);
    }
    if (v.isBoolean()) return v.as<JSON::Boolean>().value() ? 1 : 0;
    if (v.isString()) {
      try {
        const long long ll = std::stoll(v.as<JSON::String>().value());
        if (ll > std::numeric_limits<int>::max()) return fallback;
        if (ll < std::numeric_limits<int>::min()) return fallback;
        return static_cast<int>(ll);
      } catch (...) {}
    }
    return fallback;
  }

  static inline double jsonDoubleOr (const JSON::Any& objAny, const char* key, double fallback) {
    if (!objAny.isObject()) return fallback;
    const auto& v = objAny.as<JSON::Object>().get(key);
    if (v.isNumber()) return v.as<JSON::Number>().value();
    if (v.isBoolean()) return v.as<JSON::Boolean>().value() ? 1.0 : 0.0;
    if (v.isString()) {
      try {
        return std::stod(v.as<JSON::String>().value());
      } catch (...) {}
    }
    return fallback;
  }

  static inline bool globMatch (const String& pattern, const String& text) {
    // Supports '*' and '?' wildcards.
    size_t p = 0;
    size_t t = 0;
    size_t star = String::npos;
    size_t match = 0;

    while (t < text.size()) {
      if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
        ++p;
        ++t;
        continue;
      }

      if (p < pattern.size() && pattern[p] == '*') {
        star = p++;
        match = t;
        continue;
      }

      if (star != String::npos) {
        p = star + 1;
        t = ++match;
        continue;
      }

      return false;
    }

    while (p < pattern.size() && pattern[p] == '*') {
      ++p;
    }

    return p == pattern.size();
  }

  static inline std::optional<JSON::Any> parseJSONResultString (const JSON::Any& value) {
    if (!value.isString()) {
      return std::nullopt;
    }

    const auto text = value.as<JSON::String>().value();
    try {
      auto parsed = JSON::parse(text);

      // Some platforms return a JSON-encoded string for JS string results
      // (e.g. WebView2). In that case, parse the inner payload.
      if (parsed.isString()) {
        try {
          parsed = JSON::parse(parsed.as<JSON::String>().value());
        } catch (...) {}
      }

      return parsed;
    } catch (...) {
      return std::nullopt;
    }
  }

  static const char ORO_CDP_BOOTSTRAP[] = R"JS(
if (!globalThis.__oro_cdp) {
  const store = {
    objects: new Map(),
    objectGroups: new Map(),
    objectIdToGroup: new Map(),
    objectOrder: [],
    maxObjects: 32768,
    nodes: new Map(),
    nodeToId: new WeakMap(),
    nodeOrder: [],
    maxNodes: 32768,
    maxSerializeNodes: 4096,
    nextNodeId: 1,
    nextObjectId: 1
  };

  const getOrCreateObjectId = (value, objectGroup) => {
    const id = String(store.nextObjectId++);
    store.objects.set(id, value);
    store.objectOrder.push(id);

    if (objectGroup) {
      store.objectIdToGroup.set(id, objectGroup);
      let set = store.objectGroups.get(objectGroup);
      if (!set) {
        set = new Set();
        store.objectGroups.set(objectGroup, set);
      }
      set.add(id);
    }

    while (store.objectOrder.length > store.maxObjects) {
      const oldId = store.objectOrder.shift();
      if (!oldId) break;

      try {
        const group = store.objectIdToGroup.get(oldId);
        if (group) {
          const set = store.objectGroups.get(group);
          if (set) {
            set.delete(oldId);
            if (!set.size) store.objectGroups.delete(group);
          }
          store.objectIdToGroup.delete(oldId);
        }
      } catch (e) {}

      store.objects.delete(oldId);
    }

    return id;
  };

  const releaseObjectGroup = (objectGroup) => {
    const set = store.objectGroups.get(objectGroup);
    if (!set) return;

    for (const id of set) {
      store.objects.delete(id);
      store.objectIdToGroup.delete(id);
    }

    store.objectGroups.delete(objectGroup);
  };

  const parseUnserializableValue = (text) => {
    if (text === '-0') return -0;
    if (text === 'NaN') return NaN;
    if (text === 'Infinity') return Infinity;
    if (text === '-Infinity') return -Infinity;
    if (typeof text === 'string' && text.endsWith('n')) {
      try { return BigInt(text.slice(0, -1)); } catch (e) {}
    }
    return undefined;
  };

  const toRemoteObject = (value, opts = {}) => {
    const { returnByValue = false, objectGroup = '' } = opts || {};
    const t = typeof value;

    if (value === undefined) return { type: 'undefined' };
    if (value === null) return { type: 'object', subtype: 'null', value: null };

    if (t === 'boolean') return { type: 'boolean', value: Boolean(value) };

    if (t === 'number') {
      if (Object.is(value, -0)) return { type: 'number', unserializableValue: '-0' };
      if (Number.isNaN(value)) return { type: 'number', unserializableValue: 'NaN' };
      if (value === Infinity) return { type: 'number', unserializableValue: 'Infinity' };
      if (value === -Infinity) return { type: 'number', unserializableValue: '-Infinity' };
      return { type: 'number', value: Number(value) };
    }

    if (t === 'string') return { type: 'string', value: String(value) };

    if (t === 'bigint') {
      return { type: 'bigint', unserializableValue: String(value) + 'n' };
    }

    if (t === 'symbol') {
      return { type: 'symbol', description: String(value) };
    }

    if (t === 'function') {
      if (returnByValue) {
        return { type: 'function', description: String(value) };
      }
      const objectId = getOrCreateObjectId(value, objectGroup);
      return { type: 'function', objectId, description: String(value) };
    }

    if (t === 'object') {
      let subtype = undefined;
      if (Array.isArray(value)) subtype = 'array';
      else if (value instanceof Date) subtype = 'date';
      else if (value instanceof RegExp) subtype = 'regexp';
      else if (typeof Node !== 'undefined' && value instanceof Node) subtype = 'node';

      if (returnByValue) {
        try {
          const v = JSON.parse(JSON.stringify(value));
          return { type: 'object', subtype, value: v };
        } catch (e) {
          return { type: 'object', subtype, description: String(value) };
        }
      }

      const objectId = getOrCreateObjectId(value, objectGroup);
      return { type: 'object', subtype, objectId, description: String(value) };
    }

    return { type: 'undefined' };
  };

  const getOrCreateNodeId = (node) => {
    if (!node) return 0;
    const existing = store.nodeToId.get(node);
    if (existing && store.nodes.has(existing)) return existing;
    const id = store.nextNodeId++;
    store.nodeToId.set(node, id);
    store.nodes.set(id, node);
    store.nodeOrder.push(id);

    while (store.nodeOrder.length > store.maxNodes) {
      const oldId = store.nodeOrder.shift();
      if (!oldId) break;
      const oldNode = store.nodes.get(oldId);
      if (oldNode) {
        try { store.nodeToId.delete(oldNode); } catch (e) {}
      }
      store.nodes.delete(oldId);
    }

    return id;
  };

  const getNode = (nodeId) => {
    const id = Number(nodeId || 0);
    if (!id) return document;
    return store.nodes.get(id) || null;
  };

  const computeFrameIdForDocument = (doc) => {
    let rootId = '';
    try { rootId = String(globalThis.__oro_cdp_root_frame_id || ''); } catch (e) {}
    if (!rootId) return '';

    let curDoc = doc;
    const chain = [];

    try {
      while (curDoc) {
        let win = null;
        try { win = curDoc.defaultView || null; } catch (e) { win = null; }
        let frameEl = null;
        try { frameEl = win && win.frameElement ? win.frameElement : null; } catch (e) { frameEl = null; }
        if (!frameEl) break;
        chain.push(frameEl);
        try { curDoc = frameEl.ownerDocument || null; } catch (e) { curDoc = null; }
      }
    } catch (e) {}

    let id = rootId;
    for (let i = chain.length - 1; i >= 0; i--) {
      try { id += ':frame:' + String(getOrCreateNodeId(chain[i])); } catch (e) {}
    }

    return id;
  };

	  const computeFrameIdForNode = (node, nodeId) => {
	    let doc = null;
	    try {
	      doc = node && node.nodeType === 9 ? node : (node && node.ownerDocument) ? node.ownerDocument : null;
	    } catch (e) {
	      doc = null;
	    }

	    const base = computeFrameIdForDocument(doc || document);
	    if (!base) return '';

	    try {
	      if (node && node.nodeType === 9) {
	        return base;
	      }
	    } catch (e) {}

	    let tag = '';
	    try { tag = node && node.nodeType === 1 ? String(node.tagName || '').toLowerCase() : ''; } catch (e) { tag = ''; }

	    if (tag === 'iframe' || tag === 'frame') {
	      const nid = nodeId || getOrCreateNodeId(node);
	      return base + ':frame:' + String(nid);
	    }

	    if (tag === 'html') {
	      return base;
	    }

	    return '';
	  };

  const resolveNodeRef = (ref) => {
    try {
      if (!ref || typeof ref !== 'object') return null;

      const objectId = ref.objectId;
      if (objectId && store.objects.has(objectId)) {
        return store.objects.get(objectId) || null;
      }

      const id = Number(ref.nodeId || ref.backendNodeId || 0);
      if (!id) return document;
      return store.nodes.get(id) || null;
    } catch (e) {
      return null;
    }
  };

  const serializeNode = (node, depth, maxNodes) => {
    const limit = Number(maxNodes || store.maxSerializeNodes || 0);
    const state = {
      remaining: (Number.isFinite(limit) && limit > 0) ? limit : (store.maxSerializeNodes || 4096)
    };

    const inner = (node, depth) => {
      if (!node || state.remaining <= 0) return null;
      state.remaining--;

      const nodeId = getOrCreateNodeId(node);
      const out = {
        nodeId,
        backendNodeId: nodeId,
        nodeType: node.nodeType,
        nodeName: node.nodeName,
        localName: node.localName || String(node.nodeName || '').toLowerCase(),
        nodeValue: node.nodeValue || '',
        childNodeCount: node.childNodes ? node.childNodes.length : 0,
      };

      try {
        const frameId = computeFrameIdForNode(node, nodeId);
        if (frameId) out.frameId = frameId;
      } catch (e) {}

      if (node.nodeType === 9) {
        out.documentURL = location.href;
        out.baseURL = document.baseURI || location.href;
      } else if (node.nodeType === 1 && node.attributes) {
        const attrs = [];
        for (const attr of node.attributes) {
          attrs.push(attr.name, attr.value);
        }
        out.attributes = attrs;
      }

      const d = Number(depth || 0);
      if (d > 0 && node.childNodes && node.childNodes.length && state.remaining > 0) {
        const children = [];
        const list = node.childNodes;
        const len = (list && typeof list.length === 'number') ? list.length : 0;
        for (let i = 0; i < len && state.remaining > 0; i++) {
          const child = inner(list[i], d - 1);
          if (child) children.push(child);
        }
        if (children.length) out.children = children;
      }

      return out;
    };

    return inner(node, depth);
  };

  const getBoxModel = (node) => {
    const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);
    if (!el || !el.getBoundingClientRect) return null;
    const r = el.getBoundingClientRect();
    const quad = [r.left, r.top, r.right, r.top, r.right, r.bottom, r.left, r.bottom];
    return {
      model: {
        content: quad,
        padding: quad,
        border: quad,
        margin: quad,
        width: r.width,
        height: r.height
      }
    };
  };

  // Network.* event plumbing for automation tooling.
  // This is intentionally limited (not Chrome network instrumentation parity):
  // it instruments fetch/XHR activity and exposes bounded response bodies on-demand.
  const __oro_body_limit = 5 * 1024 * 1024; // 5 MiB
  const __oro_total_body_limit = 20 * 1024 * 1024; // 20 MiB
  const __oro_post_limit = 256 * 1024; // 256 KiB

  const __oro_get_ipc = (() => {
    let promise = null;
    return () => {
      if (promise) return promise;
      try {
        promise = import('oro:ipc').then((m) => m && (m.default || m)).catch(() => null);
      } catch (e) {
        promise = Promise.resolve(null);
      }
      return promise;
    };
  })();

  const __oro_ipc_send = (name, data) => {
    try {
      __oro_get_ipc().then((ipc) => {
        if (ipc && typeof ipc.send === 'function') {
          ipc.send(name, data);
        }
      });
    } catch (e) {}
  };

  const __oro_base64_encode = (bytes) => {
    try {
      if (typeof btoa !== 'function') return '';
      let binary = '';
      const chunk = 0x8000;
      for (let i = 0; i < bytes.length; i += chunk) {
        binary += String.fromCharCode.apply(null, Array.from(bytes.subarray(i, i + chunk)));
      }
      return btoa(binary);
    } catch (e) {
      return '';
    }
  };

  const __oro_base64_decode = (text) => {
    try {
      if (typeof atob !== 'function') return new Uint8Array(0);
      const bin = atob(String(text || ''));
      const out = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; ++i) {
        out[i] = bin.charCodeAt(i) & 0xff;
      }
      return out;
    } catch (e) {
      return new Uint8Array(0);
    }
  };

  const __oro_read_response_bytes = async (response, limit) => {
    if (!response || !limit || limit <= 0) return null;

    try {
      const lenHeader = response.headers && typeof response.headers.get === 'function'
        ? response.headers.get('content-length')
        : null;
      if (lenHeader) {
        const n = Number(lenHeader);
        if (Number.isFinite(n) && n > limit) {
          return null;
        }
      }
    } catch (e) {}

    try {
      const stream = response.body;
      if (stream && typeof stream.getReader === 'function') {
        const reader = stream.getReader();
        const chunks = [];
        let total = 0;
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          if (!value) continue;
          const len = value.byteLength || 0;
          total += len;
          if (total > limit) {
            try { await reader.cancel(); } catch (e) {}
            return null;
          }
          chunks.push(value);
        }

        const out = new Uint8Array(total);
        let offset = 0;
        for (const c of chunks) {
          out.set(c, offset);
          offset += c.byteLength || 0;
        }
        return out;
      }
    } catch (e) {}

    try {
      const buf = await response.arrayBuffer();
      if (buf && buf.byteLength <= limit) {
        return new Uint8Array(buf);
      }
    } catch (e) {}

    return null;
  };

  const net = {
    extraHeaders: {},
    requests: new Map(),
    order: [],
    nextRequestId: 1,
    requestIdPrefix: (() => {
      try {
        const raw = globalThis.__args && (globalThis.__args.index ?? globalThis.__args.windowIndex);
        const idx = Number(raw);
        if (Number.isFinite(idx) && idx >= 0) return String(idx) + ':js:';
      } catch (e) {}
      return '0:js:';
    })(),
    maxEntries: 256,
    totalBodyBytes: 0,
    maxBodyBytes: __oro_total_body_limit,
	    fetchEnabled: false,
	    fetchPatterns: [],
	    fetchWaiters: new Map(),
	    blockedURLPatterns: [],

    setExtraHeaders: (headers) => {
      try {
        net.extraHeaders = headers && typeof headers === 'object' ? headers : {};
      } catch (e) {
        net.extraHeaders = {};
      }
    },

    _remember: (requestId, info) => {
      net.requests.set(requestId, info);
      net.order.push(requestId);
      while (net.order.length > net.maxEntries) {
        const old = net.order.shift();
        if (old) {
          try {
            const entry = net.requests.get(old);
            const bytes = entry && entry.bodyBytes ? Number(entry.bodyBytes) : 0;
            if (bytes > 0) {
              net.totalBodyBytes = Math.max(0, net.totalBodyBytes - bytes);
            }
          } catch (e) {}
          net.requests.delete(old);
        }
      }
    },

    _emit: (method, params) => {
      try {
        __oro_ipc_send('cdp.networkEvent', {
          event: JSON.stringify({ method, params: params && typeof params === 'object' ? params : {} })
        });
      } catch (e) {}
    },

    setFetchPatterns: (patterns) => {
      try {
        net.fetchEnabled = true;
        net.fetchPatterns = Array.isArray(patterns) ? patterns : [];
      } catch (e) {
        net.fetchEnabled = true;
        net.fetchPatterns = [];
      }
    },

	    clearFetchPatterns: () => {
	      net.fetchEnabled = false;
	      net.fetchPatterns = [];
	    },

	    setBlockedURLs: (urls) => {
	      try {
	        net.blockedURLPatterns = Array.isArray(urls) ? urls : [];
	      } catch (e) {
	        net.blockedURLPatterns = [];
	      }
	    },

	    clearBlockedURLs: () => {
	      net.blockedURLPatterns = [];
	    },

	    _matchesUrlPattern: (pattern, url) => {
	      try {
	        if (!pattern) return true;
	        const p = String(pattern);
        if (!p) return true;
        const parts = p.split('*').map((s) => {
          try {
            return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
          } catch (e) {
            return '';
          }
        });
        const esc = parts.join('.*');
        const re = new RegExp('^' + esc + '$');
        return re.test(String(url || ''));
      } catch (e) {
        return false;
	      }
	    },

	    _shouldBlockURL: (url) => {
	      const patterns = Array.isArray(net.blockedURLPatterns) ? net.blockedURLPatterns : [];
	      if (!patterns.length) return false;
	      for (const p of patterns) {
	        try {
	          if (!p) continue;
	          if (net._matchesUrlPattern(p, url)) return true;
	        } catch (e) {}
	      }
	      return false;
	    },

	    _shouldPauseFetch: (url, resourceType) => {
	      if (!net.fetchEnabled) return false;
      const patterns = Array.isArray(net.fetchPatterns) ? net.fetchPatterns : [];
      if (!patterns.length) return true;
      for (const p of patterns) {
        try {
          if (!p || typeof p !== 'object') continue;
          if (p.resourceType && String(p.resourceType) !== String(resourceType || '')) continue;
          if (p.urlPattern && !net._matchesUrlPattern(p.urlPattern, url)) continue;
          return true;
        } catch (e) {}
      }
      return false;
    },

    _pauseFetch: (requestId, request, resourceType) => {
      const rid = String(requestId || '');
      return new Promise((resolve) => {
        try { net.fetchWaiters.set(rid, resolve); } catch (e) {}
        try {
          net._emit('Fetch.requestPaused', {
            requestId: rid,
            networkId: rid,
            request: request && typeof request === 'object' ? request : {},
            resourceType: String(resourceType || 'Fetch'),
            requestStage: 'Request'
          });
        } catch (e) {}
      });
    },

    _resolveFetchDecision: (requestId, decision) => {
      try {
        const rid = String(requestId || '');
        const cb = net.fetchWaiters.get(rid);
        if (cb) {
          net.fetchWaiters.delete(rid);
          cb(decision && typeof decision === 'object' ? decision : { action: 'continue' });
        }
      } catch (e) {}
    },

    getResponseBody: (requestId) => {
      const key = String(requestId || '');
      const entry = net.requests.get(key);
      if (!entry || entry.responseBody === undefined) return null;
      return JSON.stringify({ body: String(entry.responseBody || ''), base64Encoded: Boolean(entry.base64Encoded) });
    },

    getRequestPostData: (requestId) => {
      const key = String(requestId || '');
      const entry = net.requests.get(key);
      if (!entry || entry.postData === undefined) return null;
      return JSON.stringify({ postData: String(entry.postData || '') });
    }
  };

	  // Patch fetch/XHR once. This intentionally ignores subresource
	  // loads (images/script tags) on platforms without resource instrumentation.
	  try {
	    const originalFetch = globalThis.fetch;
	    if (typeof originalFetch === 'function' && !originalFetch.__oro_cdp_wrapped) {
		      const wrappedFetch = async function(input, init) {
		        const requestId = String(net.requestIdPrefix) + String(net.nextRequestId++);
	        let url = '';
	        let method = 'GET';
	        let headers = {};

	        try {
	          if (typeof Request !== 'undefined' && input instanceof Request) {
	            url = String(input.url || '');
	            method = String((init && init.method) || input.method || 'GET');

	            try {
	              const hs = (init && init.headers !== undefined)
	                ? new Headers(init.headers)
	                : (input.headers || null);
	              if (hs && typeof hs.forEach === 'function') {
	                hs.forEach((v, k) => { headers[String(k).toLowerCase()] = String(v) })
	              }
	            } catch (e) {}
	          } else {
	            const req = new Request(input, init);
	            url = String(req.url || input || '');
	            method = String(req.method || (init && init.method) || 'GET');
	            try {
	              if (req && req.headers && typeof req.headers.forEach === 'function') {
	                req.headers.forEach((v, k) => { headers[String(k).toLowerCase()] = String(v) })
	              }
	            } catch (e) {}
	          }
	        } catch (e) {
	          try {
	            url = String(input || '');
	            method = String((init && init.method) || 'GET');
	          } catch (e2) {}
	        }

	        let postData = undefined;
	        try {
	          const body = init && init.body;
	          if (typeof body === 'string') postData = body;
	          else if (typeof URLSearchParams !== 'undefined' && body instanceof URLSearchParams) postData = body.toString();
	        } catch (e) {}

	        try {
	          if (typeof postData === 'string' && postData.length > __oro_post_limit) {
	            postData = undefined;
	          }
	        } catch (e) {}

		        // Reflect current extra headers in our request view.
		        try {
		          const extra = net.extraHeaders || {};
		          for (const k of Object.keys(extra)) {
		            headers[String(k).toLowerCase()] = String(extra[k]);
		          }
		        } catch (e) {}

	          // Network.setBlockedURLs (applies before interception).
	          try {
	            if (net._shouldBlockURL(url)) {
	              net._remember(requestId, { postData, responseBody: undefined, base64Encoded: false, bodyBytes: 0 });
	              net._emit('Network.requestWillBeSent', {
	                requestId,
	                type: 'Fetch',
	                request: { url, method, headers, postData }
	              });
	              net._emit('Network.loadingFailed', { requestId, type: 'Fetch', errorText: 'BlockedByClient', canceled: false });
	              throw new Error('BlockedByClient');
	            }
	          } catch (e) {
	            if (e && String((e && (e.message || e)) || '') === 'BlockedByClient') throw e;
	          }

	          let decision = null;
	          let inputOverride = null;
	          try {
	            if (net._shouldPauseFetch(url, 'Fetch')) {
	              decision = await net._pauseFetch(requestId, { url, method, headers, postData }, 'Fetch');
            }
          } catch (e) {
            decision = null;
          }

          try {
            const action = decision && typeof decision === 'object'
              ? String(decision.action || 'continue')
              : 'continue';
            if (action === 'continue' && decision && typeof decision === 'object') {
              if (typeof decision.url === 'string' && decision.url) {
                url = String(decision.url);
                inputOverride = url;
              }
              if (typeof decision.method === 'string' && decision.method) {
                method = String(decision.method);
              }
              if (decision.headers && typeof decision.headers === 'object') {
                headers = {};
                for (const k of Object.keys(decision.headers)) {
                  headers[String(k).toLowerCase()] = String(decision.headers[k]);
                }
              }
              if (typeof decision.postData === 'string' && decision.postData.length <= __oro_post_limit) {
                postData = String(decision.postData);
              }
            }
          } catch (e) {}

	        // Re-apply extra headers after any header overrides.
	        try {
	          const extra = net.extraHeaders || {};
	          for (const k of Object.keys(extra)) {
	            headers[String(k).toLowerCase()] = String(extra[k]);
	          }
	        } catch (e) {}

	        net._remember(requestId, { postData, responseBody: undefined, base64Encoded: false, bodyBytes: 0 });
	        net._emit('Network.requestWillBeSent', {
	          requestId,
	          type: 'Fetch',
	          request: { url, method, headers, postData }
	        });

          // Handle Fetch.fulfillRequest / Fetch.failRequest for fetch() interception.
          try {
            if (decision && typeof decision === 'object') {
              const action = String(decision.action || 'continue');
              if (action === 'fail') {
                const errorText = String(decision.errorReason || 'Failed');
                net._emit('Network.loadingFailed', { requestId, type: 'Fetch', errorText, canceled: false });
                throw new Error(errorText);
              }

              if (action === 'fulfill') {
                const status = Number(decision.responseCode || 200);
                const responseHeaders = {};
                try {
                  const h = decision.responseHeaders;
                  if (h && typeof h === 'object') {
                    for (const k of Object.keys(h)) {
                      responseHeaders[String(k).toLowerCase()] = String(h[k]);
                    }
                  }
                } catch (e) {}

                const mimeType = String(responseHeaders['content-type'] || '');
                let bodyBytes = new Uint8Array(0);
                try {
                  const body = String(decision.body || '');
                  if (body) {
                    bodyBytes = __oro_base64_decode(body);
                    if (
                      bodyBytes &&
                      bodyBytes.byteLength <= __oro_body_limit &&
                      (net.totalBodyBytes + bodyBytes.byteLength) <= net.maxBodyBytes
                    ) {
                      const entry = net.requests.get(requestId);
                      if (entry) {
                        const prev = entry.bodyBytes ? Number(entry.bodyBytes) : 0;
                        if (prev > 0) net.totalBodyBytes = Math.max(0, net.totalBodyBytes - prev);
                        entry.responseBody = body;
                        entry.base64Encoded = true;
                        entry.bodyBytes = Number(bodyBytes.byteLength || 0);
                        net.totalBodyBytes += entry.bodyBytes;
                      }
                    }
                  }
                } catch (e) {}

                net._emit('Network.responseReceived', {
                  requestId,
                  type: 'Fetch',
                  response: {
                    url: String(url || ''),
                    status: Number(status || 0),
                    statusText: '',
                    headers: responseHeaders,
                    mimeType
                  }
                });

                net._emit('Network.loadingFinished', {
                  requestId,
                  type: 'Fetch',
                  encodedDataLength: Number(bodyBytes && bodyBytes.byteLength || 0)
                });

                try {
                  const h = new Headers();
                  for (const k of Object.keys(responseHeaders)) {
                    try { h.set(k, String(responseHeaders[k])); } catch (e) {}
                  }
                  const body = bodyBytes && bodyBytes.byteLength ? bodyBytes : null;
                  return new Response(body, { status, headers: h });
                } catch (e) {
                  const body = bodyBytes && bodyBytes.byteLength ? bodyBytes : null;
                  return new Response(body, { status });
                }
              }
            }
          } catch (e) {
            // If we threw from interception handling, preserve the error.
            throw e;
          }

	        let response = null;
	        try {
	          let outInit = init;
            try {
              const baseInit = (init && typeof init === 'object') ? init : {};
              const h = new Headers();
              for (const k of Object.keys(headers)) {
                try { h.set(k, String(headers[k])); } catch (e) {}
              }
              outInit = Object.assign({}, baseInit, { headers: h, method: String(method || 'GET') });
              if (typeof postData === 'string') {
                outInit.body = postData;
              }
            } catch (e) {}

	          response = await originalFetch.call(this, inputOverride !== null ? inputOverride : input, outInit);
	        } catch (e) {
	          net._emit('Network.loadingFailed', { requestId, type: 'Fetch', errorText: String(e && e.message || e), canceled: false });
	          throw e;
	        }

        const responseHeaders = {};
        try {
          if (response && response.headers && typeof response.headers.forEach === 'function') {
            response.headers.forEach((v, k) => { responseHeaders[String(k).toLowerCase()] = String(v) });
          }
        } catch (e) {}

        const mimeType = String(responseHeaders['content-type'] || '');
        net._emit('Network.responseReceived', {
          requestId,
          type: 'Fetch',
          response: {
            url: String(response.url || url),
            status: Number(response.status || 0),
            statusText: String(response.statusText || ''),
            headers: responseHeaders,
            mimeType
          }
        });

        // Capture response body (bounded).
        try {
          if (net.totalBodyBytes < net.maxBodyBytes) {
            const clone = response.clone();
            const bytes = await __oro_read_response_bytes(clone, __oro_body_limit);
            if (bytes && bytes.byteLength <= __oro_body_limit && (net.totalBodyBytes + bytes.byteLength) <= net.maxBodyBytes) {
            let base64Encoded = false;
            let bodyText = '';
            const looksText = /^text\//i.test(mimeType) || /\b(json|javascript|xml|html)\b/i.test(mimeType);
            if (looksText && typeof TextDecoder !== 'undefined') {
              try {
                bodyText = new TextDecoder('utf-8').decode(bytes);
              } catch (e) {
                base64Encoded = true;
                bodyText = __oro_base64_encode(bytes);
              }
            } else {
              base64Encoded = true;
              bodyText = __oro_base64_encode(bytes);
            }

            const entry = net.requests.get(requestId);
            if (entry) {
              const prev = entry.bodyBytes ? Number(entry.bodyBytes) : 0;
              if (prev > 0) net.totalBodyBytes = Math.max(0, net.totalBodyBytes - prev);
              entry.responseBody = bodyText;
              entry.base64Encoded = base64Encoded;
              entry.bodyBytes = Number(bytes.byteLength || 0);
              net.totalBodyBytes += entry.bodyBytes;
            }
          }
          }
        } catch (e) {}

        net._emit('Network.loadingFinished', { requestId, type: 'Fetch', encodedDataLength: 0 });
        return response;
      };

      wrappedFetch.__oro_cdp_wrapped = true;
      globalThis.fetch = wrappedFetch;
    }
  } catch (e) {}

  try {
	    const XHR = globalThis.XMLHttpRequest;
	    if (typeof XHR === 'function' && XHR.prototype && !XHR.prototype.__oro_cdp_wrapped) {
	      const origOpen = XHR.prototype.open;
	      const origSend = XHR.prototype.send;
	      const origSetRequestHeader = XHR.prototype.setRequestHeader;
	      XHR.prototype.__oro_cdp_wrapped = true;

	      XHR.prototype.open = function(method, url) {
	        try {
	          this.__oro_cdp_method = String(method || 'GET');
	          this.__oro_cdp_url = String(url || '');
	          this.__oro_cdp_headers = {};
	        } catch (e) {}
	        return origOpen.apply(this, arguments);
	      };

	      if (typeof origSetRequestHeader === 'function') {
	        XHR.prototype.setRequestHeader = function(name, value) {
	          try {
	            if (!this.__oro_cdp_headers || typeof this.__oro_cdp_headers !== 'object') {
	              this.__oro_cdp_headers = {};
	            }
	            const k = String(name || '').toLowerCase();
	            if (k) this.__oro_cdp_headers[k] = String(value);
	          } catch (e) {}
	          return origSetRequestHeader.apply(this, arguments);
	        };
	      }

			      XHR.prototype.send = function(body) {
			        const requestId = String(net.requestIdPrefix) + String(net.nextRequestId++);
		        let url = '';
		        let method = 'GET';
		        try {
	          url = String(this.__oro_cdp_url || '');
	          method = String(this.__oro_cdp_method || 'GET');
	        } catch (e) {}

	        let postData = undefined;
	        try {
	          if (typeof body === 'string') postData = body;
	          else if (typeof URLSearchParams !== 'undefined' && body instanceof URLSearchParams) postData = body.toString();
	        } catch (e) {}

	        try {
	          if (typeof postData === 'string' && postData.length > __oro_post_limit) {
	            postData = undefined;
	          }
		        } catch (e) {}

		        const requestHeaders = {};
		        try {
		          const base = this.__oro_cdp_headers;
		          if (base && typeof base === 'object') {
		            for (const k of Object.keys(base)) {
		              requestHeaders[String(k).toLowerCase()] = String(base[k]);
		            }
		          }
		        } catch (e) {}

		        try {
		          const extra = net.extraHeaders || {};
		          for (const k of Object.keys(extra)) {
		            requestHeaders[String(k).toLowerCase()] = String(extra[k]);
		          }
		        } catch (e) {}

	        net._remember(requestId, { postData, responseBody: undefined, base64Encoded: false, bodyBytes: 0 });
	        net._emit('Network.requestWillBeSent', {
	          requestId,
	          type: 'XHR',
	          request: { url, method, headers: requestHeaders, postData }
	        });

	        try {
	          const extra = net.extraHeaders || {};
	          for (const k of Object.keys(extra)) {
	            try { this.setRequestHeader(k, String(extra[k])); } catch (e) {}
	          }
        } catch (e) {}

        const onDone = () => {
          try {
            const status = Number(this.status || 0);
            const statusText = String(this.statusText || '');
            const responseHeaders = {};
            try {
              const raw = String(this.getAllResponseHeaders ? this.getAllResponseHeaders() : '');
              raw.split(/\r?\n/).forEach((line) => {
                const idx = line.indexOf(':');
                if (idx > 0) {
                  const k = line.slice(0, idx).trim().toLowerCase();
                  const v = line.slice(idx + 1).trim();
                  if (k) responseHeaders[k] = v;
                }
              });
            } catch (e) {}

            const mimeType = String(responseHeaders['content-type'] || '');
            net._emit('Network.responseReceived', {
              requestId,
              type: 'XHR',
              response: { url, status, statusText, headers: responseHeaders, mimeType }
            });

            try {
              const entry = net.requests.get(requestId);
              if (entry) {
                if (this.responseType === '' || this.responseType === 'text') {
                  const text = String(this.responseText || '');
                  if (text.length <= __oro_body_limit && (net.totalBodyBytes + text.length) <= net.maxBodyBytes) {
                    const prev = entry.bodyBytes ? Number(entry.bodyBytes) : 0;
                    if (prev > 0) net.totalBodyBytes = Math.max(0, net.totalBodyBytes - prev);
                    entry.responseBody = text;
                    entry.base64Encoded = false;
                    entry.bodyBytes = Number(text.length || 0);
                    net.totalBodyBytes += entry.bodyBytes;
                  }
                } else if (this.responseType === 'arraybuffer' && this.response) {
                  const bytes = new Uint8Array(this.response);
                  if (bytes.byteLength <= __oro_body_limit && (net.totalBodyBytes + bytes.byteLength) <= net.maxBodyBytes) {
                    const prev = entry.bodyBytes ? Number(entry.bodyBytes) : 0;
                    if (prev > 0) net.totalBodyBytes = Math.max(0, net.totalBodyBytes - prev);
                    entry.responseBody = __oro_base64_encode(bytes);
                    entry.base64Encoded = true;
                    entry.bodyBytes = Number(bytes.byteLength || 0);
                    net.totalBodyBytes += entry.bodyBytes;
                  }
                }
              }
            } catch (e) {}
          } catch (e) {}

          net._emit('Network.loadingFinished', { requestId, type: 'XHR', encodedDataLength: 0 });
          cleanup();
        };

        const onFail = (errText) => {
          net._emit('Network.loadingFailed', { requestId, type: 'XHR', errorText: String(errText || 'Failed'), canceled: false });
          cleanup();
        };

        const onError = () => onFail('Error');
        const onAbort = () => onFail('Aborted');
        const onTimeout = () => onFail('Timeout');

        const cleanup = () => {
          try {
            this.removeEventListener('load', onDone);
            this.removeEventListener('error', onError);
            this.removeEventListener('abort', onAbort);
            this.removeEventListener('timeout', onTimeout);
          } catch (e) {}
        };

	        try {
	          this.addEventListener('load', onDone);
	          this.addEventListener('error', onError);
	          this.addEventListener('abort', onAbort);
	          this.addEventListener('timeout', onTimeout);
	        } catch (e) {}

	        // Network.setBlockedURLs
	        try {
	          if (net._shouldBlockURL(url)) {
	            onFail('BlockedByClient');
	            try { this.abort(); } catch (e) {}
	            return;
	          }
	        } catch (e) {}

	        return origSend.apply(this, arguments);
	      };
	    }
	  } catch (e) {}

  // Runtime/Page events for automation tooling.
  // These are not Chrome-parity implementations; they exist to keep common
  // Puppeteer/Playwright flows working and observable.
  try {
    if (!globalThis.__oro_cdp_event_wrapped) {
      globalThis.__oro_cdp_event_wrapped = true;

      let __oro_next_exception_id = 1;

      const __oro_max_console_string = 64 * 1024;
      const __oro_trunc = (input) => {
        try {
          const s = String(input || '');
          if (s.length > __oro_max_console_string) {
            return s.slice(0, __oro_max_console_string) + '…';
          }
          return s;
        } catch (e) {
          return '';
        }
      };

      const __oro_map_arg = (value) => {
        try {
          if (value === undefined || value === null) {
            return toRemoteObject(value, { returnByValue: true });
          }

          if (typeof Error !== 'undefined' && value instanceof Error) {
            const desc = __oro_trunc(value && (value.stack || value.message || value) || 'Error');
            return { type: 'object', subtype: 'error', description: desc };
          }
        } catch (e) {}

        try {
          const t = typeof value;

          // Primitives (keep as values, but cap long strings).
          if (t === 'string') return { type: 'string', value: __oro_trunc(value) };
          if (t === 'number' || t === 'boolean' || t === 'bigint' || t === 'symbol') {
            return toRemoteObject(value, { returnByValue: true });
          }

          // Avoid JSON.stringify for arbitrary objects in event args to prevent
          // heavy/cyclic serialization and giant frames.
          if (t === 'function') {
            return { type: 'function', description: __oro_trunc(String(value)) };
          }

          if (t === 'object') {
            let subtype = undefined;
            try {
              if (Array.isArray(value)) subtype = 'array';
              else if (value instanceof Date) subtype = 'date';
              else if (value instanceof RegExp) subtype = 'regexp';
              else if (typeof Node !== 'undefined' && value instanceof Node) subtype = 'node';
            } catch (e) {}

            return { type: 'object', subtype, description: __oro_trunc(String(value)) };
          }
        } catch (e) {}

        return { type: 'undefined' };
      };

      // console.* -> Runtime.consoleAPICalled
      try {
        const c = globalThis.console;
        const wrapConsole = (name, type) => {
          try {
            if (!c || typeof c[name] !== 'function') return;
            const orig = c[name];
            if (orig.__oro_cdp_wrapped) return;

            const wrapped = function() {
              try {
                const args = Array.prototype.slice.call(arguments).map(__oro_map_arg);
                net._emit('Runtime.consoleAPICalled', {
                  type: String(type || name || 'log'),
                  args,
                  executionContextId: 0,
                  timestamp: 0,
                  stackTrace: { callFrames: [] }
                });
              } catch (e) {}

              return orig.apply(this, arguments);
            };

            wrapped.__oro_cdp_wrapped = true;
            c[name] = wrapped;
          } catch (e) {}
        };

        wrapConsole('log', 'log');
        wrapConsole('debug', 'debug');
        wrapConsole('info', 'info');
        wrapConsole('warn', 'warning');
        wrapConsole('error', 'error');
      } catch (e) {}

      // window.onerror / unhandledrejection -> Runtime.exceptionThrown
      try {
        if (typeof globalThis.addEventListener === 'function') {
          globalThis.addEventListener('error', (e) => {
            try {
              const message = String((e && e.message) || 'Error');
              const url = String((e && (e.filename || e.url)) || (globalThis.location && globalThis.location.href) || '');
              const lineNumber = Number((e && e.lineno) || 0);
              const columnNumber = Number((e && e.colno) || 0);
              const err = e && e.error;
              const exception = err ? __oro_map_arg(err) : { type: 'string', value: message };

              net._emit('Runtime.exceptionThrown', {
                timestamp: 0,
                exceptionDetails: {
                  exceptionId: __oro_next_exception_id++,
                  text: message,
                  url,
                  lineNumber,
                  columnNumber,
                  exception,
                  executionContextId: 0,
                  stackTrace: { callFrames: [] }
                }
              });
            } catch (e2) {}
          }, true);

          globalThis.addEventListener('unhandledrejection', (e) => {
            try {
              const reason = e && e.reason;
              const message = String((reason && (reason.message || reason)) || 'Unhandled promise rejection');
              const exception = reason ? __oro_map_arg(reason) : { type: 'string', value: message };

              net._emit('Runtime.exceptionThrown', {
                timestamp: 0,
                exceptionDetails: {
                  exceptionId: __oro_next_exception_id++,
                  text: message,
                  url: String((globalThis.location && globalThis.location.href) || ''),
                  lineNumber: 0,
                  columnNumber: 0,
                  exception,
                  executionContextId: 0,
                  stackTrace: { callFrames: [] }
                }
              });
            } catch (e2) {}
          }, true);
        }
      } catch (e) {}

      // alert/confirm/prompt -> Page.javascriptDialogOpening
      try {
        const wrapDialog = (name, type) => {
          try {
            const orig = globalThis[name];
            if (typeof orig !== 'function' || orig.__oro_cdp_wrapped) return;
            const wrapped = function(message, defaultPrompt) {
              try {
                net._emit('Page.javascriptDialogOpening', {
                  url: String((globalThis.location && globalThis.location.href) || ''),
                  message: String(message || ''),
                  type: String(type || name || 'alert'),
                  hasBrowserHandler: true,
                  defaultPrompt: type === 'prompt' ? String(defaultPrompt || '') : ''
                });
              } catch (e) {}
              return orig.apply(this, arguments);
            };
            wrapped.__oro_cdp_wrapped = true;
            globalThis[name] = wrapped;
          } catch (e) {}
        };

        wrapDialog('alert', 'alert');
        wrapDialog('confirm', 'confirm');
        wrapDialog('prompt', 'prompt');
      } catch (e) {}
    }
  } catch (e) {}

  globalThis.__oro_cdp = {
    store,
    toRemoteObject,
    getOrCreateObjectId,
    parseUnserializableValue,
    releaseObjectGroup,
    getOrCreateNodeId,
    getNode,
    resolveNodeRef,
    serializeNode,
    getBoxModel,
    net
  };
}
)JS";

  CDP::CDP (const Options& options)
    : core::Service(options) {
      this->defaultBrowserContextId = String("default-") + this->browserId;
    }

  CDP::~CDP () noexcept {
    this->stop();
  }

  bool CDP::isListening () const {
    return this->listening.load();
  }

  CDP::Status CDP::status () const {
    return this->computeStatus();
  }

  CDP::Status CDP::computeStatus () const {
    Status s;
    s.listening = this->listening.load();
    s.hostname = this->hostname;
    s.port = this->port.load();
    s.browserId = this->browserId;
    if (s.listening && s.port > 0) {
      s.wsEndpoint = String("ws://") + s.hostname + ":" + std::to_string(s.port) + "/devtools/browser/" + s.browserId;
      s.httpEndpoint = String("http://") + s.hostname + ":" + std::to_string(s.port);
    }
    return s;
  }

  bool CDP::start () {
    // Start from config if enabled.
    const auto& userConfig = static_cast<runtime::Runtime&>(this->context).userConfig;
    if (!userConfig.contains("cdp_remote_debugging_port")) {
      return true;
    }

    ListenOptions options;
    options.hostname = userConfig.contains("cdp_remote_debugging_host")
      ? userConfig.at("cdp_remote_debugging_host")
      : "127.0.0.1";

    try {
      options.port = std::stoi(userConfig.at("cdp_remote_debugging_port"));
    } catch (...) {
      options.port = 0;
    }

    // On platforms where the loop runs on a dedicated thread/queue, all libuv
    // handle operations must happen on the loop thread to avoid races/crashes.
  #if ORO_RUNTIME_PLATFORM_APPLE
    const bool dispatched = this->loop.dispatch([=, this]() {
      if (!this->ensureListening(options)) {
        std::fprintf(stderr, "DevTools failed to start\n");
        std::fflush(stderr);
      }
    });
    (void)dispatched;
    return true;
  #else
    if (this->loop.options.dedicatedThread) {
      const bool dispatched = this->loop.dispatch([=, this]() {
        if (!this->ensureListening(options)) {
          std::fprintf(stderr, "DevTools failed to start\n");
          std::fflush(stderr);
        }
      });
      (void)dispatched;
      return true;
    }

    if (!this->ensureListening(options)) {
      std::fprintf(stderr, "DevTools failed to start\n");
      std::fflush(stderr);
    }
    return true;
  #endif
  }

  bool CDP::stop () {
    if (!this->enabled) {
      return true;
    }

    // Always schedule shutdown on the loop.
    this->stopListeningOnLoop();
    return true;
  }

  void CDP::listen (const String& seq, const ListenOptions& options, const Callback cb) {
    this->loop.dispatch([=, this]() {
      const bool ok = this->ensureListening(options);
      if (!ok) {
        const auto json = JSON::Object::Entries {
          {"source", "cdp.listen"},
          {"err", JSON::Object::Entries {
            {"message", "Failed to start CDP server"}
          }}
        };
        cb(seq, json, QueuedResponse{});
        return;
      }

      const auto s = this->computeStatus();
      const auto json = JSON::Object::Entries {
        {"source", "cdp.listen"},
        {"data", JSON::Object::Entries {
          {"listening", JSON::Boolean(s.listening)},
          {"hostname", s.hostname},
          {"port", JSON::Number(s.port)},
          {"browserId", s.browserId},
          {"wsEndpoint", s.wsEndpoint},
          {"httpEndpoint", s.httpEndpoint}
        }}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  void CDP::close (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]() {
      this->stopListeningOnLoop();
      const auto json = JSON::Object::Entries {
        {"source", "cdp.close"},
        {"data", JSON::Object::Entries {}}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  void CDP::getStatus (const String& seq, const Callback cb) {
    this->loop.dispatch([=, this]() {
      const auto s = this->computeStatus();
      const auto json = JSON::Object::Entries {
        {"source", "cdp.status"},
        {"data", JSON::Object::Entries {
          {"listening", JSON::Boolean(s.listening)},
          {"hostname", s.hostname},
          {"port", JSON::Number(s.port)},
          {"browserId", s.browserId},
          {"wsEndpoint", s.wsEndpoint},
          {"httpEndpoint", s.httpEndpoint}
        }}
      };
      cb(seq, json, QueuedResponse{});
    });
  }

  void CDP::onWindowCreated (int windowIndex) {
    if (!this->isListening()) {
      return;
    }

    this->loop.dispatch([=, this]() {
      const auto target = this->getOrCreateTargetForWindow(windowIndex);
      if (target.targetId.empty()) {
        return;
      }

      auto app = App::sharedApplication();
      if (!app) {
        return;
      }

      auto window = app->runtime.windowManager.getWindow(windowIndex);
      if (!window) {
        return;
      }

      auto targetInfo = JSON::Object::Entries {
        {"targetId", target.targetId},
        {"type", target.type},
        {"title", ""},
        {"url", window->bridge->navigator.location.str()},
        {"attached", false}
      };
      if (!target.browserContextId.empty()) {
        targetInfo.insert_or_assign("browserContextId", target.browserContextId);
      }

      const int contextId = [&]() {
        Lock lock(this->mutex);
        if (this->windowExecutionContexts.contains(windowIndex)) {
          return this->windowExecutionContexts.at(windowIndex);
        }
        return 0;
      }();

      Lock lock(this->mutex);
      for (auto& entry : this->clients) {
        auto client = entry.second;
        if (!client || client->closing || client->closed) {
          continue;
        }

        if (client->discoverTargets && targetMatchesFilter(client->discoverFilter, target.type)) {
          this->sendCDPEvent(client, "Target.targetCreated", JSON::Object::Entries { {"targetInfo", targetInfo} });
        }

        if (client->kind != Client::Kind::BrowserWS) {
          continue;
        }

        AutoAttachOptions rootAutoAttach;
        if (client->autoAttachBySession.contains("")) {
          rootAutoAttach = client->autoAttachBySession.at("");
        }

        if (!rootAutoAttach.autoAttach) {
          continue;
        }

        if (!targetMatchesFilter(rootAutoAttach.filter, target.type)) {
          continue;
        }

        const auto sid = uuid::v7();
        client->sessions.emplace(std::string(sid), SessionInfo {
          .sessionId = sid,
          .targetId = target.targetId,
          .flatten = rootAutoAttach.flatten,
          .parentSessionId = ""
        });

        auto attachedInfo = JSON::Object::Entries {
          {"targetId", target.targetId},
          {"type", target.type},
          {"title", ""},
          {"url", window->bridge->navigator.location.str()},
          {"attached", true}
        };
        if (!target.browserContextId.empty()) {
          attachedInfo.insert_or_assign("browserContextId", target.browserContextId);
        }

        this->sendCDPEvent(client, "Target.attachedToTarget", JSON::Object::Entries {
          {"sessionId", sid},
          {"targetInfo", attachedInfo},
          {"waitingForDebugger", rootAutoAttach.waitForDebuggerOnStart}
        });

        if (contextId > 0) {
          this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
            {"context", JSON::Object::Entries {
              {"id", JSON::Number(contextId)},
              {"origin", window->bridge->navigator.location.origin},
              {"name", ""},
              {"auxData", JSON::Object::Entries {
                {"isDefault", JSON::Boolean(true)},
                {"type", "default"},
                {"frameId", target.targetId}
              }}
            }}
          }, sid);
        }

        this->sendCDPEvent(client, "Page.frameNavigated", JSON::Object::Entries {
          {"frame", JSON::Object::Entries {
            {"id", target.targetId},
            {"loaderId", target.targetId},
            {"url", window->bridge->navigator.location.str()},
            {"name", ""},
            {"mimeType", "text/html"},
            {"securityOrigin", window->bridge->navigator.location.origin}
          }}
        }, sid);
      }
    });
  }

  void CDP::onWindowDestroyed (int windowIndex) {
    if (!this->isListening()) {
      return;
    }

    this->loop.dispatch([=, this]() {
      const auto maybeTarget = [&]() -> std::optional<std::string> {
        Lock lock(this->mutex);
        if (!this->windowTargets.contains(windowIndex)) {
          return std::nullopt;
        }
        return this->windowTargets.at(windowIndex);
      }();

      if (!maybeTarget.has_value()) {
        return;
      }

      const auto targetId = String(maybeTarget.value());

      // Detach and notify clients.
      {
        Lock lock(this->mutex);
        for (auto& entry : this->clients) {
          auto client = entry.second;
          if (!client || client->closing || client->closed) {
            continue;
          }

          if (client->discoverTargets) {
            if (targetMatchesFilter(client->discoverFilter, "page")) {
              this->sendCDPEvent(client, "Target.targetDestroyed", JSON::Object::Entries { {"targetId", targetId} });
            }
          }

          if (client->kind == Client::Kind::PageWS && client->boundTargetId == targetId) {
            client->closing = true;
            uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
            uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
              auto client = static_cast<Client*>(uv_handle_get_data(h));
              uv_handle_set_data(h, nullptr);
              if (client && client->cdp) {
                Lock lock(client->cdp->mutex);
                client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
              }
              if (client) {
                client->closed = true;
                delete client->handle;
                client->handle = nullptr;
                delete client;
              }
            });
            continue;
          }

          if (client->kind == Client::Kind::BrowserWS) {
            std::vector<std::string> toDetach;
            toDetach.reserve(client->sessions.size());
            for (const auto& kv : client->sessions) {
              if (kv.second.targetId == targetId) {
                toDetach.push_back(kv.first);
              }
            }

            for (const auto& sid : toDetach) {
              String parentSessionId;
              if (client->sessions.contains(sid)) {
                parentSessionId = client->sessions.at(sid).parentSessionId;
              }

              this->sendCDPEvent(
                client,
                "Target.detachedFromTarget",
                JSON::Object::Entries {
                  {"sessionId", sid},
                  {"targetId", targetId}
                },
                parentSessionId
              );

              client->sessions.erase(sid);
              client->autoAttachBySession.erase(sid);
            }
          }
        }

        // Remove per-target state
        this->targetNewDocumentScripts.erase(std::string(targetId));
        this->targetBindings.erase(std::string(targetId));

        // Remove mappings for this window.
        this->windowReadyState.erase(windowIndex);
        this->windowExecutionContexts.erase(windowIndex);
        this->windowAnimationPlaybackRate.erase(windowIndex);
        this->windowNavigationHistory.erase(windowIndex);
        this->windowNavigationHistoryIndex.erase(windowIndex);
	        this->windowPendingNavigationRequestId.erase(windowIndex);
	        this->windowLastNavigationRequestId.erase(windowIndex);
	        this->windowExtraHTTPHeaders.erase(windowIndex);
	        this->windowBlockedURLPatterns.erase(windowIndex);
	        this->windowNetworkRequestInterceptionEnabled.erase(windowIndex);
	        this->windowBrowserContextId.erase(windowIndex);
	        this->windowFetchEnabled.erase(windowIndex);
	        this->windowFetchPatterns.erase(windowIndex);
        this->windowIsolatedWorldExecutionContexts.erase(windowIndex);
        for (auto it = this->pendingFetchRequests.begin(); it != this->pendingFetchRequests.end();) {
          if (it->second.windowIndex == windowIndex) {
            it = this->pendingFetchRequests.erase(it);
          } else {
            ++it;
          }
        }
        this->targetWindows.erase(std::string(targetId));
        this->windowTargets.erase(windowIndex);
      }
    });
  }

  void CDP::handleWindowNavigatedOnLoop (int windowIndex, const String& url) {
    const auto target = this->getOrCreateTargetForWindow(windowIndex);
    if (target.targetId.empty()) {
      return;
    }

    auto app = App::sharedApplication();
    if (!app) {
      return;
    }

    auto window = app->runtime.windowManager.getWindow(windowIndex);
    if (!window) {
      return;
    }

    // Navigation history (bounded).
    {
      Lock lock(this->mutex);
      auto& history = this->windowNavigationHistory[windowIndex];

      int currentIndex = -1;
      if (this->windowNavigationHistoryIndex.contains(windowIndex)) {
        currentIndex = this->windowNavigationHistoryIndex.at(windowIndex);
      }

      if (currentIndex >= static_cast<int>(history.size())) {
        currentIndex = history.empty() ? -1 : static_cast<int>(history.size()) - 1;
      }

      // If we were in the middle of history, truncate the forward entries.
      if (currentIndex >= 0 && currentIndex + 1 < static_cast<int>(history.size())) {
        history.erase(history.begin() + currentIndex + 1, history.end());
      }

      if (history.empty() || currentIndex < 0 || history.at(currentIndex).url != url) {
        const int entryId = this->nextNavigationEntryId.fetch_add(1);
        history.push_back(NavigationEntry {
          .id = entryId,
          .url = url,
          .title = ""
        });
        currentIndex = static_cast<int>(history.size()) - 1;
      }

      if (history.size() > MAX_NAVIGATION_HISTORY_ENTRIES) {
        const size_t overflow = history.size() - MAX_NAVIGATION_HISTORY_ENTRIES;
        history.erase(history.begin(), history.begin() + overflow);
        if (currentIndex >= 0) {
          currentIndex -= static_cast<int>(overflow);
          if (currentIndex < 0) currentIndex = 0;
        }
      }

      this->windowNavigationHistoryIndex[windowIndex] = currentIndex;
    }

    // Notify discovery clients that target info changed.
    {
      Lock lock(this->mutex);
      for (auto& entry : this->clients) {
        auto client = entry.second;
        if (!client || client->closing || client->closed) {
          continue;
        }

        if (client->discoverTargets && targetMatchesFilter(client->discoverFilter, "page")) {
          auto targetInfo = JSON::Object::Entries {
            {"targetId", target.targetId},
            {"type", target.type},
            {"title", ""},
            {"url", url},
            {"attached", this->clientAttachedToTarget(client, target.targetId)}
          };
          if (!target.browserContextId.empty()) {
            targetInfo.insert_or_assign("browserContextId", target.browserContextId);
          }
          this->sendCDPEvent(client, "Target.targetInfoChanged", JSON::Object::Entries { {"targetInfo", targetInfo} });
        }
      }
    }

    const auto frameId = target.targetId;
    broadcastToAttachedTarget(target.targetId, [this, url, window, frameId](auto client, const auto& sessionId) {
      this->sendCDPEvent(client, "Page.frameNavigated", JSON::Object::Entries {
        {"frame", JSON::Object::Entries {
          {"id", frameId},
          {"loaderId", frameId},
          {"url", url},
          {"name", ""},
          {"mimeType", "text/html"},
          {"securityOrigin", window->bridge->navigator.location.origin}
        }}
      }, sessionId);
    });
  }

  void CDP::onWindowNavigated (int windowIndex, const String& url) {
    if (!this->isListening()) {
      return;
    }

    const auto urlCopy = url;

    this->loop.dispatch([=, this]() {
      this->handleWindowNavigatedOnLoop(windowIndex, urlCopy);
    });
  }

  void CDP::handleWindowBeforeRuntimeInitOnLoop (int windowIndex) {
    const auto target = this->getOrCreateTargetForWindow(windowIndex);
    if (target.targetId.empty()) {
      return;
    }

    auto app = App::sharedApplication();
    if (!app) {
      return;
    }

    auto window = app->runtime.windowManager.getWindow(windowIndex);
    if (!window) {
      return;
    }

    // Ensure our helper/instrumentation is installed early.
    window->eval(ORO_CDP_BOOTSTRAP);

    std::vector<ScriptToEvaluateOnNewDocument> scripts;
    std::vector<std::string> bindings;
    std::vector<std::pair<String, int>> isolatedWorldContexts;

    const auto frameId = target.targetId;
    const auto rootFrameIdLiteral = JSON::String(frameId).str();
    window->eval(
      "(function(){"
      " try { globalThis.__oro_cdp_root_frame_id = " + rootFrameIdLiteral + "; } catch (e) {}"
      "})();"
    );
    const auto frameIdKey = std::string(frameId);

    const int contextId = [&]() {
      Lock lock(this->mutex);
      const int next = this->nextExecutionContextId.fetch_add(1);
      this->windowExecutionContexts[windowIndex] = next;

      // New document means new isolated world contexts too.
      this->windowIsolatedWorldExecutionContexts[windowIndex].clear();

      const auto key = frameIdKey;
      if (this->targetNewDocumentScripts.contains(key)) {
        scripts = this->targetNewDocumentScripts.at(key);
      }

      if (this->targetBindings.contains(key)) {
        for (const auto& b : this->targetBindings.at(key)) {
          bindings.push_back(b);
        }
      }

      std::unordered_set<std::string> seenWorldNames;
      auto& worldMap = this->windowIsolatedWorldExecutionContexts[windowIndex][key];
      for (const auto& s : scripts) {
        if (s.worldName.empty()) {
          continue;
        }

        const auto worldName = std::string(s.worldName);
        if (!seenWorldNames.insert(worldName).second) {
          continue;
        }

        const int worldContextId = this->nextExecutionContextId.fetch_add(1);
        worldMap[worldName] = worldContextId;
        isolatedWorldContexts.push_back(std::make_pair(s.worldName, worldContextId));
      }

      return next;
    }();

    const auto origin = window->bridge->navigator.location.origin;
    const double ts = monotonicSeconds();
    broadcastToAttachedTarget(target.targetId, [this, contextId, origin, frameId, isolatedWorldContexts, ts](auto client, const auto& sessionId) {
      this->sendCDPEvent(client, "Runtime.executionContextsCleared", JSON::Object::Entries {}, sessionId);
      this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
        {"context", JSON::Object::Entries {
          {"id", JSON::Number(contextId)},
          {"origin", origin},
          {"name", ""},
          {"auxData", JSON::Object::Entries {
            {"isDefault", JSON::Boolean(true)},
            {"type", "default"},
            {"frameId", frameId}
          }}
        }}
      }, sessionId);

      for (const auto& entry : isolatedWorldContexts) {
        this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
          {"context", JSON::Object::Entries {
            {"id", JSON::Number(entry.second)},
            {"origin", origin},
            {"name", entry.first},
            {"auxData", JSON::Object::Entries {
              {"isDefault", JSON::Boolean(false)},
              {"type", "isolated"},
              {"frameId", frameId}
            }}
          }}
        }, sessionId);
      }

      this->sendCDPEvent(client, "Page.lifecycleEvent", JSON::Object::Entries {
        {"frameId", frameId},
        {"loaderId", frameId},
        {"name", "init"},
        {"timestamp", ts}
      }, sessionId);
    });

    for (const auto& name : bindings) {
      const auto nameLiteral = JSON::String(name).str();
      window->eval(
        "(function(){"
        " try {"
        "  const name = " + nameLiteral + ";"
        "  if (Object.prototype.hasOwnProperty.call(globalThis, name)) return;"
        "  Object.defineProperty(globalThis, name, {"
        "    configurable: true,"
        "    enumerable: false,"
        "    writable: false,"
        "    value: function(payload) {"
        "      try {"
        "        let p = '';"
        "        if (payload === undefined) p = '';"
        "        else if (typeof payload === 'string') p = payload;"
        "        else {"
        "          try { p = JSON.stringify(payload); } catch (e2) { p = String(payload); }"
        "        }"
        "        if (typeof p === 'string' && p.length > 1024 * 1024) p = p.slice(0, 1024 * 1024);"
        "        let executionContextId = 0;"
        "        try { executionContextId = Number(globalThis.__oro_cdp_active_execution_context_id || 0); } catch (e3) {}"
        "        if (!executionContextId || !Number.isFinite(executionContextId) || executionContextId <= 0) executionContextId = " + std::to_string(contextId) + ";"
        "        import('oro:ipc').then((m) => {"
        "          const ipc = m && (m.default || m);"
        "          if (ipc && ipc.send) ipc.send('cdp.bindingCalled', { name, payload: p, executionContextId });"
        "        });"
        "      } catch (e) {}"
        "    }"
        "  });"
        " } catch (e) {}"
        "})();"
      );
    }

    for (const auto& s : scripts) {
      window->eval(s.source);
    }
  }

  void CDP::onWindowBeforeRuntimeInit (int windowIndex) {
    if (!this->isListening()) {
      return;
    }

    this->loop.dispatch([=, this]() {
      this->handleWindowBeforeRuntimeInitOnLoop(windowIndex);
    });
  }

  void CDP::onBindingCalled (int windowIndex, const String& name, const String& payload, int executionContextId) {
    if (!this->isListening()) {
      return;
    }

    const auto nameCopy = name;
    const auto payloadCopy = payload;
    const int executionContextIdCopy = executionContextId;

    this->loop.dispatch([=, this]() {
      const auto target = this->getOrCreateTargetForWindow(windowIndex);
      if (target.targetId.empty()) {
        return;
      }

      const int contextId = [&]() {
        Lock lock(this->mutex);
        const auto isKnownContextId = [&](int candidate) -> bool {
          if (candidate <= 0) {
            return false;
          }

          if (
            this->windowExecutionContexts.contains(windowIndex) &&
            this->windowExecutionContexts.at(windowIndex) == candidate
          ) {
            return true;
          }

          if (this->windowIsolatedWorldExecutionContexts.contains(windowIndex)) {
            const auto& frameMap = this->windowIsolatedWorldExecutionContexts.at(windowIndex);
            for (const auto& frameEntry : frameMap) {
              for (const auto& worldEntry : frameEntry.second) {
                if (worldEntry.second == candidate) {
                  return true;
                }
              }
            }
          }

          return false;
        };

        if (isKnownContextId(executionContextIdCopy)) {
          return executionContextIdCopy;
        }

        if (this->windowExecutionContexts.contains(windowIndex)) {
          return this->windowExecutionContexts.at(windowIndex);
        }

        return 0;
      }();

      broadcastToAttachedTarget(target.targetId, [this, nameCopy, payloadCopy, contextId](auto client, const auto& sessionId) {
        this->sendCDPEvent(client, "Runtime.bindingCalled", JSON::Object::Entries {
          {"name", nameCopy},
          {"payload", payloadCopy},
          {"executionContextId", JSON::Number(contextId)}
        }, sessionId);
      });
    });
  }

  void CDP::onNetworkEvent (int windowIndex, const JSON::Any& event) {
    if (!this->isListening()) {
      return;
    }

    const auto eventCopy = event;
    const bool fromPage = eventCopy.isString();

    this->loop.dispatch([=, this]() mutable {
      JSON::Any normalized = eventCopy;
      if (normalized.isString()) {
        const auto parsedOpt = parseJSONResultString(normalized);
        if (parsedOpt.has_value()) {
          normalized = parsedOpt.value();
        }
      }

      if (!normalized.isObject()) {
        return;
      }

      const auto method = jsonStringOrEmpty(normalized, "method");
      if (method.empty()) {
        return;
      }

      const auto target = this->getOrCreateTargetForWindow(windowIndex);
      if (target.targetId.empty()) {
        return;
      }

      JSON::Any params = normalized.as<JSON::Object>().get("params");
      if (!params.isObject()) {
        params = JSON::Object::Entries {};
      }

      // Support Fetch.* interception for in-page fetch() instrumentation by
      // bridging Fetch.requestPaused -> Fetch.continue*/fulfill*/fail* decisions
      // back into the page via eval.
      if (fromPage && method == "Fetch.requestPaused") {
        const auto requestId = jsonStringOrEmpty(params, "requestId");
        if (!requestId.empty()) {
          const bool registered = this->registerFetchRequest(windowIndex, requestId, [=, this](const JSON::Any& decision) {
            auto app = App::sharedApplication();
            if (!app) {
              return;
            }

            auto window = app->runtime.windowManager.getWindow(windowIndex);
            if (!window) {
              return;
            }

            const auto ridJson = JSON::Any(JSON::String(requestId)).str();
            const auto decisionJson = decision.str();
            const auto script = std::string(
              "(function(){"
            ) + ORO_CDP_BOOTSTRAP + std::string(
              " try {"
              "  const __oro = globalThis.__oro_cdp;"
              "  if (__oro && __oro.net && typeof __oro.net._resolveFetchDecision === 'function') {"
              "    __oro.net._resolveFetchDecision(" + ridJson + ", " + decisionJson + ");"
              "  }"
              " } catch (e) {}"
              "})()"
            );
            window->eval(script);
          });
          (void)registered;
        }
      }

      const double monotonicNow = monotonicSeconds();
      const double wallTimeNow = wallTimeSeconds();

	      // Fill a few required/commonly-consumed fields when missing so automation
	      // clients (Puppeteer/Playwright) can make progress.
	      if (method.rfind("Network.", 0) == 0) {
	        auto entries = params.as<JSON::Object>().value();

	        if (!entries.contains("timestamp") || !entries.at("timestamp").isNumber() || entries.at("timestamp").as<JSON::Number>().value() <= 0) {
	          entries.insert_or_assign("timestamp", JSON::Number(monotonicNow));
	        }
	        if (!entries.contains("loaderId")) {
	          entries.insert_or_assign("loaderId", target.targetId);
	        }
	        if (!entries.contains("frameId")) {
	          entries.insert_or_assign("frameId", target.targetId);
	        }

	        if (method == "Network.requestWillBeSent") {
	          if (!entries.contains("redirectHasExtraInfo")) {
	            entries.insert_or_assign("redirectHasExtraInfo", JSON::Boolean(false));
	          }
	          if (!entries.contains("documentURL")) {
	            auto app = App::sharedApplication();
	            if (app) {
	              if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
	                entries.insert_or_assign("documentURL", window->bridge->navigator.location.str());
	              }
	            }
	          }
	          if (!entries.contains("initiator")) {
	            entries.insert_or_assign("initiator", JSON::Object::Entries { {"type", "other"} });
	          }
	          if (!entries.contains("hasUserGesture")) {
	            entries.insert_or_assign("hasUserGesture", JSON::Boolean(false));
	          }
	          if (!entries.contains("wallTime") || !entries.at("wallTime").isNumber() || entries.at("wallTime").as<JSON::Number>().value() <= 0) {
	            entries.insert_or_assign("wallTime", JSON::Number(wallTimeNow));
	          }
	        } else if (method == "Network.responseReceived") {
	          if (!entries.contains("hasExtraInfo")) {
	            entries.insert_or_assign("hasExtraInfo", JSON::Boolean(false));
	          }

	          JSON::Any responseAny = entries.contains("response") ? entries.at("response") : JSON::Any();
	          auto response = responseAny.isObject()
	            ? responseAny.as<JSON::Object>().value()
	            : JSON::Object::Entries {};

	          if (!response.contains("url")) {
	            response.insert_or_assign("url", "");
	          }
	          if (!response.contains("status") || !response.at("status").isNumber()) {
	            response.insert_or_assign("status", JSON::Number(0));
	          }
	          if (!response.contains("statusText")) {
	            response.insert_or_assign("statusText", "");
	          }
	          if (!response.contains("headers") || !response.at("headers").isObject()) {
	            response.insert_or_assign("headers", JSON::Object::Entries {});
	          }
	          if (!response.contains("mimeType")) {
	            response.insert_or_assign("mimeType", "");
	          }
	          if (!response.contains("charset")) {
	            response.insert_or_assign("charset", "");
	          }
	          if (!response.contains("connectionReused")) {
	            response.insert_or_assign("connectionReused", JSON::Boolean(false));
	          }
	          if (!response.contains("connectionId") || !response.at("connectionId").isNumber()) {
	            response.insert_or_assign("connectionId", JSON::Number(0));
	          }
	          if (!response.contains("encodedDataLength") || !response.at("encodedDataLength").isNumber()) {
	            response.insert_or_assign("encodedDataLength", JSON::Number(0));
	          }
	          if (!response.contains("securityState")) {
	            response.insert_or_assign("securityState", "unknown");
	          }

	          entries.insert_or_assign("response", response);
	        } else if (method == "Network.loadingFinished") {
	          if (!entries.contains("encodedDataLength") || !entries.at("encodedDataLength").isNumber()) {
	            entries.insert_or_assign("encodedDataLength", JSON::Number(0));
	          }
	        } else if (method == "Network.loadingFailed") {
	          if (!entries.contains("type")) {
	            entries.insert_or_assign("type", "Other");
	          }
	        }

	        params = entries;
	      } else if (method == "Fetch.requestPaused") {
        auto entries = params.as<JSON::Object>().value();

        if (!entries.contains("frameId")) {
          entries.insert_or_assign("frameId", target.targetId);
        }

        if (!entries.contains("requestStage")) {
          entries.insert_or_assign("requestStage", "Request");
        }

        params = entries;
      } else if (method == "Runtime.consoleAPICalled") {
        auto entries = params.as<JSON::Object>().value();

        if (!entries.contains("timestamp") || !entries.at("timestamp").isNumber() || entries.at("timestamp").as<JSON::Number>().value() <= 0) {
          entries.insert_or_assign("timestamp", JSON::Number(monotonicNow));
        }

        if (!entries.contains("executionContextId") || !entries.at("executionContextId").isNumber() || entries.at("executionContextId").as<JSON::Number>().value() <= 0) {
          const int contextId = [&]() {
            Lock lock(this->mutex);
            if (this->windowExecutionContexts.contains(windowIndex)) {
              return this->windowExecutionContexts.at(windowIndex);
            }
            return 0;
          }();
          if (contextId > 0) {
            entries.insert_or_assign("executionContextId", JSON::Number(contextId));
          }
        }

        params = entries;
      } else if (method == "Runtime.exceptionThrown") {
        auto entries = params.as<JSON::Object>().value();

        if (!entries.contains("timestamp") || !entries.at("timestamp").isNumber() || entries.at("timestamp").as<JSON::Number>().value() <= 0) {
          entries.insert_or_assign("timestamp", JSON::Number(monotonicNow));
        }

        if (entries.contains("exceptionDetails") && entries.at("exceptionDetails").isObject()) {
          auto details = entries.at("exceptionDetails").as<JSON::Object>().value();
          if (!details.contains("executionContextId") || !details.at("executionContextId").isNumber() || details.at("executionContextId").as<JSON::Number>().value() <= 0) {
            const int contextId = [&]() {
              Lock lock(this->mutex);
              if (this->windowExecutionContexts.contains(windowIndex)) {
                return this->windowExecutionContexts.at(windowIndex);
              }
              return 0;
            }();
            if (contextId > 0) {
              details.insert_or_assign("executionContextId", JSON::Number(contextId));
            }
          }
          entries.insert_or_assign("exceptionDetails", details);
        }

	        params = entries;
	      }

	      const bool emitNetworkRequestIntercepted = [&]() {
	        Lock lock(this->mutex);
	        return (
	          this->windowNetworkRequestInterceptionEnabled.contains(windowIndex) &&
	          this->windowNetworkRequestInterceptionEnabled.at(windowIndex)
	        );
	      }();

	      broadcastToAttachedTarget(target.targetId, [this, method, params, emitNetworkRequestIntercepted](auto client, const auto& sessionId) {
	        this->sendCDPEvent(client, method, params, sessionId);

	        if (emitNetworkRequestIntercepted && method == "Fetch.requestPaused") {
	          const auto requestStage = jsonStringOrEmpty(params, "requestStage");
	          if (requestStage.empty() || requestStage == "Request") {
		            const auto interceptionId = jsonStringOrEmpty(params, "requestId");
		            const auto frameId = jsonStringOrEmpty(params, "frameId");
		            const auto resourceType = jsonStringOrEmpty(params, "resourceType");

		            const auto requestAny = params.isObject() ? params.as<JSON::Object>().get("request") : JSON::Any();
		            const auto request = requestAny.isObject() ? requestAny : JSON::Any(JSON::Object::Entries {});

		            this->sendCDPEvent(client, "Network.requestIntercepted", JSON::Object::Entries {
		              {"interceptionId", interceptionId},
		              {"request", request},
		              {"frameId", frameId},
	              {"resourceType", resourceType.empty() ? String("Other") : resourceType},
	              {"isNavigationRequest", JSON::Boolean(resourceType == "Document")},
	              {"requestId", interceptionId}
	            }, sessionId);
	          }
	        }

	        if (!client || !client->logEnabled) {
	          return;
	        }

        // Mirror console/exception events into Log.entryAdded when enabled.
        if (method == "Runtime.consoleAPICalled") {
          String level = "info";
          String text = "";
          double ts = monotonicSeconds();

          if (params.isObject()) {
            const auto& obj = params.as<JSON::Object>();
            const auto type = jsonStringOrEmpty(params, "type");
            if (type == "error") level = "error";
            else if (type == "warning") level = "warning";

            const auto tsAny = obj.get("timestamp");
            if (tsAny.isNumber() && tsAny.as<JSON::Number>().value() > 0) {
              ts = tsAny.as<JSON::Number>().value();
            }

            const auto stringifyRemoteObject = [](const JSON::Any& ro) -> String {
              if (!ro.isObject()) return "";
              const auto& o = ro.as<JSON::Object>();

              if (o.has("value")) {
                const auto& v = o.get("value");
                if (v.isString()) return v.as<JSON::String>().value();
                if (v.isNumber()) return std::to_string(v.as<JSON::Number>().value());
                if (v.isBoolean()) return v.as<JSON::Boolean>().value() ? "true" : "false";
              }

              const auto desc = o.get("description");
              if (desc.isString()) return desc.as<JSON::String>().value();

              const auto t = o.get("type");
              if (t.isString()) return t.as<JSON::String>().value();

              return "";
            };

            const auto argsAny = obj.get("args");
            if (argsAny.isArray()) {
              bool first = true;
              for (const auto& a : argsAny.as<JSON::Array>()) {
                const auto s = stringifyRemoteObject(a);
                if (s.empty()) continue;
                if (!first) {
                  text += " ";
                }
                text += s;
                first = false;
                if (text.size() > 64 * 1024) {
                  text.resize(64 * 1024);
                  break;
                }
              }
            }
          }

          this->sendCDPEvent(client, "Log.entryAdded", JSON::Object::Entries {
            {"entry", JSON::Object::Entries {
              {"source", "console-api"},
              {"level", level},
              {"text", text},
              {"timestamp", JSON::Number(ts)}
            }}
          }, sessionId);
          return;
        }

        if (method == "Runtime.exceptionThrown") {
          String text = "Error";
          String url = "";
          int lineNumber = 0;
          double ts = monotonicSeconds();

          if (params.isObject()) {
            const auto& obj = params.as<JSON::Object>();
            const auto tsAny = obj.get("timestamp");
            if (tsAny.isNumber() && tsAny.as<JSON::Number>().value() > 0) {
              ts = tsAny.as<JSON::Number>().value();
            }

            const auto detailsAny = obj.get("exceptionDetails");
            if (detailsAny.isObject()) {
              const auto& details = detailsAny.as<JSON::Object>();
              const auto t = details.get("text");
              if (t.isString() && !t.as<JSON::String>().value().empty()) {
                text = t.as<JSON::String>().value();
              }

              const auto u = details.get("url");
              if (u.isString()) {
                url = u.as<JSON::String>().value();
              }

              lineNumber = static_cast<int>(jsonIntOr(detailsAny, "lineNumber", 0));
            }
          }

          this->sendCDPEvent(client, "Log.entryAdded", JSON::Object::Entries {
            {"entry", JSON::Object::Entries {
              {"source", "javascript"},
              {"level", "error"},
              {"text", text},
              {"timestamp", JSON::Number(ts)},
              {"url", url},
              {"lineNumber", JSON::Number(static_cast<double>(lineNumber))}
            }}
          }, sessionId);
          return;
        }
      });
    });
  }

  void CDP::onWindowReadyStateChanged (int windowIndex, const String& state) {
    if (!this->isListening()) {
      return;
    }

    const auto stateCopy = state;
    this->loop.dispatch([=, this]() {
      const bool shouldInit = [&]() {
        Lock lock(this->mutex);
        std::string prev;
        if (this->windowReadyState.contains(windowIndex)) {
          prev = this->windowReadyState.at(windowIndex);
        }

        bool init = false;
        if (stateCopy == "loading") {
          init = prev != "loading";
        } else if (prev.empty() && (stateCopy == "interactive" || stateCopy == "complete")) {
          // Some initial documents may not emit an explicit "loading" transition.
          init = true;
        }

        this->windowReadyState[windowIndex] = std::string(stateCopy);
        return init;
      }();

      if (shouldInit) {
        this->handleWindowBeforeRuntimeInitOnLoop(windowIndex);
      }

      const auto target = this->getOrCreateTargetForWindow(windowIndex);
      if (target.targetId.empty()) {
        return;
      }

      if (stateCopy == "loading") {
        broadcastToAttachedTarget(target.targetId, [this, target](auto client, const auto& sessionId) {
          this->sendCDPEvent(client, "Page.frameStartedLoading", JSON::Object::Entries { {"frameId", target.targetId} }, sessionId);
        });
      } else if (stateCopy == "interactive") {
        // Treat "interactive" as navigation commit and emit
        // Page.frameNavigated + Target.targetInfoChanged so automation clients
        // see the new URL.
        if (auto app = App::sharedApplication()) {
          if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
            this->handleWindowNavigatedOnLoop(windowIndex, window->bridge->navigator.location.str());
          }
        }

        const double ts = monotonicSeconds();
        broadcastToAttachedTarget(target.targetId, [this, target, ts](auto client, const auto& sessionId) {
          this->sendCDPEvent(client, "Page.lifecycleEvent", JSON::Object::Entries {
            {"frameId", target.targetId},
            {"loaderId", target.targetId},
            {"name", "DOMContentLoaded"},
            {"timestamp", ts}
          }, sessionId);
        });
      } else if (stateCopy == "complete") {
        const double ts = monotonicSeconds();
        broadcastToAttachedTarget(target.targetId, [this, ts](auto client, const auto& sessionId) {
          this->sendCDPEvent(client, "Page.loadEventFired", JSON::Object::Entries { {"timestamp", ts} }, sessionId);
        });
        broadcastToAttachedTarget(target.targetId, [this, target](auto client, const auto& sessionId) {
          this->sendCDPEvent(client, "Page.frameStoppedLoading", JSON::Object::Entries { {"frameId", target.targetId} }, sessionId);
        });
        broadcastToAttachedTarget(target.targetId, [this, target, ts](auto client, const auto& sessionId) {
          this->sendCDPEvent(client, "Page.lifecycleEvent", JSON::Object::Entries {
            {"frameId", target.targetId},
            {"loaderId", target.targetId},
            {"name", "load"},
            {"timestamp", ts}
          }, sessionId);
        });

        // Close out the synthetic navigation request so network
        // idle-based waits don't immediately short-circuit.
        std::optional<std::string> pendingRequestId;
        {
          Lock lock(this->mutex);
          if (this->windowPendingNavigationRequestId.contains(windowIndex)) {
            pendingRequestId = this->windowPendingNavigationRequestId.at(windowIndex);
            this->windowPendingNavigationRequestId.erase(windowIndex);
            this->windowLastNavigationRequestId[windowIndex] = pendingRequestId.value();
          }
        }

        if (pendingRequestId.has_value()) {
          auto app = App::sharedApplication();
          String urlNow = "";
          String originNow = "";
          if (app) {
            if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
              urlNow = window->bridge->navigator.location.str();
              originNow = window->bridge->navigator.location.origin;
            }
          }

          const auto requestId = String(pendingRequestId.value());
          broadcastToAttachedTarget(target.targetId, [this, target, requestId, urlNow, originNow, ts](auto client, const auto& sessionId) {
            this->sendCDPEvent(client, "Network.responseReceived", JSON::Object::Entries {
              {"requestId", requestId},
              {"loaderId", target.targetId},
              {"timestamp", ts},
              {"type", "Document"},
              {"response", JSON::Object::Entries {
                {"url", urlNow},
                {"status", 200},
                {"statusText", "OK"},
                {"headers", JSON::Object::Entries {}},
                {"mimeType", "text/html"},
                {"connectionReused", false},
                {"connectionId", 0},
                {"fromDiskCache", false},
                {"fromServiceWorker", false},
                {"encodedDataLength", 0},
                {"securityState", "unknown"},
                {"securityOrigin", originNow}
              }}
            }, sessionId);

            this->sendCDPEvent(client, "Network.loadingFinished", JSON::Object::Entries {
              {"requestId", requestId},
              {"timestamp", ts},
              {"encodedDataLength", 0}
            }, sessionId);
          });
        }
      }
    });
  }

  void CDP::storeNativeNetworkResponseBody (
    const String& requestId,
    const String& body,
    bool base64Encoded,
    size_t bodyBytes
  ) {
    if (requestId.empty()) {
      return;
    }

    const auto key = std::string(requestId);
    {
      Lock lock(this->mutex);

      if (this->nativeNetworkPayloads.contains(key)) {
        const auto& prev = this->nativeNetworkPayloads.at(key);
        if (prev.bodyBytes > 0 && this->nativeNetworkTotalBodyBytes >= prev.bodyBytes) {
          this->nativeNetworkTotalBodyBytes -= prev.bodyBytes;
        } else if (prev.bodyBytes > 0) {
          this->nativeNetworkTotalBodyBytes = 0;
        }
      } else {
        this->nativeNetworkPayloadOrder.push_back(key);
      }

      auto& entry = this->nativeNetworkPayloads[key];
      entry.body = body;
      entry.base64Encoded = base64Encoded;
      entry.bodyBytes = bodyBytes;
      this->nativeNetworkTotalBodyBytes += bodyBytes;

      while (
        this->nativeNetworkPayloadOrder.size() > MAX_NATIVE_NETWORK_PAYLOAD_ENTRIES ||
        this->nativeNetworkTotalBodyBytes > MAX_NATIVE_NETWORK_TOTAL_BODY_BYTES
      ) {
        const auto oldest = this->nativeNetworkPayloadOrder.front();
        this->nativeNetworkPayloadOrder.pop_front();
        if (this->nativeNetworkPayloads.contains(oldest)) {
          const auto& oldEntry = this->nativeNetworkPayloads.at(oldest);
          if (oldEntry.bodyBytes > 0 && this->nativeNetworkTotalBodyBytes >= oldEntry.bodyBytes) {
            this->nativeNetworkTotalBodyBytes -= oldEntry.bodyBytes;
          } else if (oldEntry.bodyBytes > 0) {
            this->nativeNetworkTotalBodyBytes = 0;
          }
          this->nativeNetworkPayloads.erase(oldest);
        }
      }
    }
  }

  void CDP::storeNativeNetworkRequestPostData (const String& requestId, const String& postData) {
    if (requestId.empty()) {
      return;
    }

    const auto key = std::string(requestId);
    {
      Lock lock(this->mutex);
      if (!this->nativeNetworkPayloads.contains(key)) {
        this->nativeNetworkPayloadOrder.push_back(key);
      }

      auto& entry = this->nativeNetworkPayloads[key];
      entry.postData = postData;

      while (this->nativeNetworkPayloadOrder.size() > MAX_NATIVE_NETWORK_PAYLOAD_ENTRIES) {
        const auto oldest = this->nativeNetworkPayloadOrder.front();
        this->nativeNetworkPayloadOrder.pop_front();
        if (this->nativeNetworkPayloads.contains(oldest)) {
          const auto& oldEntry = this->nativeNetworkPayloads.at(oldest);
          if (oldEntry.bodyBytes > 0 && this->nativeNetworkTotalBodyBytes >= oldEntry.bodyBytes) {
            this->nativeNetworkTotalBodyBytes -= oldEntry.bodyBytes;
          } else if (oldEntry.bodyBytes > 0) {
            this->nativeNetworkTotalBodyBytes = 0;
          }
          this->nativeNetworkPayloads.erase(oldest);
        }
      }
    }
  }

	  void CDP::applyExtraHTTPHeadersForWindow (int windowIndex, http::Headers* headers) const {
	    if (!headers) {
	      return;
	    }

    Lock lock(this->mutex);
    if (!this->windowExtraHTTPHeaders.contains(windowIndex)) {
      return;
    }

    const auto& extra = this->windowExtraHTTPHeaders.at(windowIndex);
	    for (const auto& h : extra) {
	      headers->set(h.name, h.value.str());
	    }
	  }

	  bool CDP::shouldBlockURLForWindow (int windowIndex, const String& url) const {
	    if (url.empty()) {
	      return false;
	    }

	    Lock lock(this->mutex);
	    if (!this->windowBlockedURLPatterns.contains(windowIndex)) {
	      return false;
	    }

	    const auto& patterns = this->windowBlockedURLPatterns.at(windowIndex);
	    if (patterns.empty()) {
	      return false;
	    }

	    for (const auto& p : patterns) {
	      if (p.empty()) {
	        continue;
	      }
	      if (globMatch(p, url)) {
	        return true;
	      }
	    }

	    return false;
	  }

	  bool CDP::isFetchEnabledForWindow (int windowIndex) const {
	    Lock lock(this->mutex);
	    return this->windowFetchEnabled.contains(windowIndex) && this->windowFetchEnabled.at(windowIndex);
	  }

  bool CDP::shouldPauseFetchRequest (int windowIndex, const String& url, const String& resourceType) const {
    Lock lock(this->mutex);

    if (!this->windowFetchEnabled.contains(windowIndex) || !this->windowFetchEnabled.at(windowIndex)) {
      return false;
    }

    if (!this->windowFetchPatterns.contains(windowIndex)) {
      return true;
    }

    const auto& patterns = this->windowFetchPatterns.at(windowIndex);
    if (patterns.empty()) {
      return true;
    }

    for (const auto& p : patterns) {
      if (p.resourceType.has_value() && !resourceType.empty() && p.resourceType.value() != resourceType) {
        continue;
      }

      if (p.urlPattern.has_value() && !p.urlPattern.value().empty()) {
        if (!globMatch(p.urlPattern.value(), url)) {
          continue;
        }
      }

      return true;
    }

    return false;
  }

  bool CDP::registerFetchRequest (
    int windowIndex,
    const String& requestId,
    const Function<void(const JSON::Any& decision)>& resolve
  ) {
    if (requestId.empty() || !resolve) {
      return false;
    }

    Lock lock(this->mutex);
    if (!this->windowFetchEnabled.contains(windowIndex) || !this->windowFetchEnabled.at(windowIndex)) {
      return false;
    }

    const auto key = std::string(requestId);
    if (this->pendingFetchRequests.contains(key)) {
      return false;
    }

    this->pendingFetchRequests.emplace(key, PendingFetchRequest {
      .windowIndex = windowIndex,
      .resolve = resolve
    });

    return true;
  }

  void CDP::onWindowDOMContentLoaded (int windowIndex) {
    if (!this->isListening()) {
      return;
    }

    this->loop.dispatch([=, this]() {
      const auto target = this->getOrCreateTargetForWindow(windowIndex);
      if (target.targetId.empty()) {
        return;
      }
      const double ts = monotonicSeconds();
      broadcastToAttachedTarget(target.targetId, [this, ts](auto client, const auto& sessionId) {
        this->sendCDPEvent(client, "Page.domContentEventFired", JSON::Object::Entries { {"timestamp", ts} }, sessionId);
      });
    });
  }

  bool CDP::ensureListening (const ListenOptions& options) {
    if (this->isListening()) {
      return true;
    }

    bool expected = false;
    if (!this->starting.compare_exchange_strong(expected, true)) {
      return true;
    }

    auto& runtime = static_cast<runtime::Runtime&>(this->context);
    const bool hadConfig = runtime.userConfig.contains("cdp_remote_debugging_port");

    this->hostname = options.hostname.size() > 0 ? options.hostname : "127.0.0.1";
    if (this->hostname == "localhost") {
      this->hostname = "127.0.0.1";
    }

    auto serverHandle = reinterpret_cast<uv_handle_t*>(&this->serverSocket);
    if (uv_is_closing(serverHandle)) {
      this->starting.store(false);
      return false;
    }

    const int port = (options.port >= 0 && options.port <= 65535) ? options.port : 0;
    const int addrResult = uv_ip4_addr(this->hostname.c_str(), port, &this->addr);
    if (addrResult != 0) {
      this->starting.store(false);
      return false;
    }

    const int initResult = uv_tcp_init(this->loop.get(), &this->serverSocket);
    if (initResult != 0) {
      this->starting.store(false);
      return false;
    }

    uv_handle_set_data(serverHandle, this);

    const int bindResult = uv_tcp_bind(
      &this->serverSocket,
      reinterpret_cast<const struct sockaddr*>(&this->addr),
      0
    );

    if (bindResult != 0) {
      this->starting.store(false);
      if (!uv_is_closing(serverHandle)) {
        uv_close(serverHandle, [](uv_handle_t* h) {
          uv_handle_set_data(h, nullptr);
        });
      }
      return false;
    }

    struct sockaddr_in sockname;
    int namelen = sizeof(sockname);
    const int socknameResult = uv_tcp_getsockname(
      &this->serverSocket,
      reinterpret_cast<struct sockaddr*>(&sockname),
      &namelen
    );

    if (socknameResult != 0) {
      this->port = 0;
      this->starting.store(false);
      if (!uv_is_closing(serverHandle)) {
        uv_close(serverHandle, [](uv_handle_t* h) {
          uv_handle_set_data(h, nullptr);
        });
      }
      return false;
    }

    this->port = ntohs(sockname.sin_port);

    const int listenResult = uv_listen(
      reinterpret_cast<uv_stream_t*>(&this->serverSocket),
      128,
      [](uv_stream_t* server, int status) {
        auto cdp = static_cast<CDP*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(server)));
        if (cdp != nullptr) {
          cdp->onConnection(server, status);
        }
      }
    );

    if (listenResult != 0) {
      this->port = 0;
      this->starting.store(false);
      if (!uv_is_closing(serverHandle)) {
        uv_close(serverHandle, [](uv_handle_t* h) {
          uv_handle_set_data(h, nullptr);
        });
      }
      return false;
    }

    this->listening.store(true);
    this->starting.store(false);

    if (!hadConfig) {
      this->injectedUserConfig.store(true);
      runtime.userConfig["cdp_remote_debugging_host"] = this->hostname;
      runtime.userConfig["cdp_remote_debugging_port"] = std::to_string(this->port.load());
    } else {
      // If a config requested a random port (0), reflect the actual bound port.
      if (runtime.userConfig.at("cdp_remote_debugging_port") == "0") {
        runtime.userConfig["cdp_remote_debugging_port"] = std::to_string(this->port.load());
      }

      if (!runtime.userConfig.contains("cdp_remote_debugging_host")) {
        runtime.userConfig["cdp_remote_debugging_host"] = this->hostname;
      }
    }

#if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
    // If CDP is enabled after the first window/web context was
    // created (e.g. via CLI flag or JS API), toggle WebKit automation on any
    // already-initialized contexts so external tools can attach.
    if (auto app = App::sharedApplication()) {
      app->dispatch([this, app]() {
        for (int i = 0; i < app->runtime.windowManager.windows.size(); ++i) {
          auto window = app->runtime.windowManager.getWindow(i);
          if (window && window->bridge && window->bridge->webContext) {
            webkit_web_context_set_automation_allowed(window->bridge->webContext, TRUE);
          }
        }
      });
    }
#endif

    // Ensure our helper/instrumentation has been installed for any existing
    // windows. This matters when CDP is enabled after a page has already
    // loaded: automation clients expect methods like DOM.describeNode to
    // include node.frameId for iframe/documentElement nodes.
    if (auto app = App::sharedApplication()) {
      for (int i = 0; i < app->runtime.windowManager.windows.size(); ++i) {
        this->handleWindowBeforeRuntimeInitOnLoop(i);
      }
    }

    // Chrome-like diagnostic line for tooling convenience.
    const auto s = this->computeStatus();
    if (s.listening && s.port > 0) {
      if (this->hostname.rfind("127.", 0) != 0) {
        std::fprintf(
          stderr,
          "WARNING: DevTools is listening on %s (non-loopback). This is insecure.\n",
          this->hostname.c_str()
        );
      }

      std::fprintf(stderr, "DevTools listening on %s\n", s.wsEndpoint.c_str());
      std::fflush(stderr);
    }

    return true;
  }

  void CDP::stopListeningOnLoop () {
    if (!this->isListening()) {
      return;
    }

    this->loop.dispatch([this]() {
      if (!this->isListening()) {
        return;
      }

      this->listening.store(false);
      this->port.store(0);

      if (this->injectedUserConfig.load()) {
        auto& runtime = static_cast<runtime::Runtime&>(this->context);
        runtime.userConfig.erase("cdp_remote_debugging_port");
        runtime.userConfig.erase("cdp_remote_debugging_host");
        this->injectedUserConfig.store(false);
      }

      // Close all clients
      {
        Lock lock(this->mutex);
        for (auto& entry : this->clients) {
          auto client = entry.second;
          if (client && !client->closing && !client->closed) {
            client->closing = true;
            uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
            uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
              auto client = static_cast<Client*>(uv_handle_get_data(h));
              uv_handle_set_data(h, nullptr);
              if (client) {
                client->closed = true;
                delete client->handle;
                client->handle = nullptr;
                delete client;
              }
            });
          }
        }
        this->clients.clear();
        this->loggedUnhandledMethods.clear();
        this->loggedUnhandledMethodsSaturated = false;
      }

      // Close server socket
      auto handle = reinterpret_cast<uv_handle_t*>(&this->serverSocket);
      if (!uv_is_closing(handle)) {
        uv_close(handle, [](uv_handle_t* h) {
          uv_handle_set_data(h, nullptr);
        });
      }
    });
  }

  void CDP::onConnection (uv_stream_t* server, int status) {
    if (status < 0) {
      return;
    }

    auto client = new Client();
    client->cdp = this;
    client->id = oro::runtime::crypto::rand64();
    client->handle = new uv_tcp_t();

    const int initResult = uv_tcp_init(this->loop.get(), client->handle);
    if (initResult != 0) {
      delete client->handle;
      client->handle = nullptr;
      delete client;
      return;
    }
    uv_handle_set_data(reinterpret_cast<uv_handle_t*>(client->handle), client);

    const int accepted = uv_accept(server, reinterpret_cast<uv_stream_t*>(client->handle));
    if (accepted != 0) {
      client->closing = true;
      uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
        auto client = static_cast<Client*>(uv_handle_get_data(h));
        if (client) {
          uv_handle_set_data(h, nullptr);
          delete client->handle;
          client->handle = nullptr;
          delete client;
          return;
        }
        uv_handle_set_data(h, nullptr);
      });
      return;
    }

    {
      Lock lock(this->mutex);
      this->clients.emplace(reinterpret_cast<uv_stream_t*>(client->handle), client);
    }

    uv_read_start(
      reinterpret_cast<uv_stream_t*>(client->handle),
      [](uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
        auto client = static_cast<Client*>(uv_handle_get_data(handle));
        if (!client || !buf) {
          return;
        }
        client->cdp->onAlloc(handle, suggested, buf);
      },
      [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
        auto client = static_cast<Client*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(stream)));
        if (!client) {
          if (buf && buf->base) delete [] buf->base;
          return;
        }
        client->cdp->onRead(stream, nread, buf);
      }
    );
  }

  void CDP::onAlloc (uv_handle_t*, size_t suggested, uv_buf_t* buf) {
    if (!buf) {
      return;
    }

    buf->base = suggested > 0 ? new char[suggested] : nullptr;
    buf->len = suggested;
  }

  void CDP::onRead (uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto client = static_cast<Client*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(stream)));

    // libuv can report nread == 0 when no data is available; that's not an EOF.
    if (nread < 0) {
      if (buf && buf->base) {
        delete [] buf->base;
      }

      if (client && !client->closing && !client->closed) {
        client->closing = true;
        uv_read_stop(stream);
        uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
          auto client = static_cast<Client*>(uv_handle_get_data(h));
          uv_handle_set_data(h, nullptr);
          if (client && client->cdp) {
            {
              Lock lock(client->cdp->mutex);
              client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
            }
          }
          if (client) {
            client->closed = true;
            delete client->handle;
            client->handle = nullptr;
            delete client;
          }
        });
      }

      return;
    }

    if (nread == 0) {
      if (buf && buf->base) {
        delete [] buf->base;
      }
      return;
    }

    if (client && !client->closing && !client->closed && buf && buf->base) {
      client->readBuffer.append(buf->base, nread);
      if (!client->handshakeDone) {
        this->handleHTTP(client);
      } else {
        this->processWebSocketFrames(client);
      }
    }

    if (buf && buf->base) {
      delete [] buf->base;
    }
  }

  void CDP::handleHTTP (Client* client) {
    const auto end = client->readBuffer.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (client->readBuffer.size() > MAX_HTTP_HEADER_SIZE) {
        uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
        writeHTTPAndClose(client, http::Response(431));
      }
      return;
    }

    const auto headerEnd = end + 4;
    if (headerEnd > MAX_HTTP_HEADER_SIZE) {
      uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
      writeHTTPAndClose(client, http::Response(431));
      return;
    }
    const auto requestText = client->readBuffer.substr(0, headerEnd);
    http::Request request(requestText, "http");

    if (!request.valid()) {
      writeHTTPAndClose(client, http::Response(400));
      return;
    }

    // WebSocket upgrade?
    const auto upgrade = request.headers.get("upgrade").value.str();
    const bool isWebSocketUpgrade = [&]() {
      if (upgrade.empty()) return false;
      const auto lower = toLowerCase(upgrade);
      // Some clients send comma-separated tokens. Treat "websocket" as a token.
      for (const auto& t : oro::runtime::string::split(lower, ',')) {
        if (oro::runtime::string::trim(t) == "websocket") {
          return true;
        }
      }
      return false;
    }();

    if (isWebSocketUpgrade) {
      const bool ok = this->handleWebSocketHandshake(client, request);
      // consume handshake bytes regardless
      client->readBuffer.erase(0, headerEnd);
      if (ok) {
        client->handshakeDone = true;
        // process any buffered frames immediately
        this->processWebSocketFrames(client);
      }
      return;
    }

    // We don't support HTTP keep-alive or pipelining for these endpoints. Stop
    // reading and consume the parsed request bytes; we'll close after writing.
    uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
    client->readBuffer.erase(0, headerEnd);

    // Simple /json endpoints.
    auto path = request.url.pathname;
    while (path.size() > 1 && path.back() == '/') {
      path.pop_back();
    }

    if (path == "/json" || path == "/json/list") {
      JSON::Array::Entries list;
      for (const auto& t : this->snapshotTargets()) {
        if (t.type != "page") continue;
        const auto ws = String("ws://") + this->hostname + ":" + std::to_string(this->port.load()) + "/devtools/page/" + t.targetId;
        list.push_back(JSON::Object::Entries {
          {"description", ""},
          {"id", t.targetId},
          {"title", t.title},
          {"type", t.type},
          {"url", t.url},
          {"webSocketDebuggerUrl", ws}
        });
      }

      const auto body = JSON::Any(list).str();
      auto response = http::Response(200);
      response.setHeader("content-type", "application/json; charset=utf-8");
      response.setHeader("content-length", static_cast<uint64_t>(body.size()));
      response.body = body;
      writeHTTPAndClose(client, response);
      return;
    }

    if (path == "/json/protocol") {
      // Minimal protocol schema. Most automation clients ship their
      // own protocol definitions, but some tools probe this endpoint.
      const auto body = JSON::Any(JSON::Object::Entries {
        {"version", JSON::Object::Entries {{"major", "1"}, {"minor", "3"}}},
        {"domains", JSON::Array::Entries {}}
      }).str();
      auto response = http::Response(200);
      response.setHeader("content-type", "application/json; charset=utf-8");
      response.setHeader("content-length", static_cast<uint64_t>(body.size()));
      response.body = body;
      writeHTTPAndClose(client, response);
      return;
    }

    if (path == "/json/version") {
      const auto st = this->computeStatus();
      const auto ua = String("OroRuntime/") + runtime::version::VERSION_STRING;
      const auto body = JSON::Any(JSON::Object::Entries {
        {"Browser", ua},
        {"Protocol-Version", "1.3"},
        {"User-Agent", ua},
        {"V8-Version", "0"},
        {"WebKit-Version", "0"},
        {"webSocketDebuggerUrl", st.wsEndpoint}
      }).str();
      auto response = http::Response(200);
      response.setHeader("content-type", "application/json; charset=utf-8");
      response.setHeader("content-length", static_cast<uint64_t>(body.size()));
      response.body = body;
      writeHTTPAndClose(client, response);
      return;
    }

    if (path == "/json/new") {
      // Chrome supports `/json/new?<url>` (and some tools use `/json/new?url=`).
      String newUrl = "about:blank";
      if (!request.url.query.empty()) {
        if (request.url.searchParams.contains("url")) {
          newUrl = request.url.searchParams.get("url").str();
        } else {
          newUrl = oro::runtime::url::decodeURIComponent(request.url.query);
        }
        if (newUrl.size() == 0) {
          newUrl = "about:blank";
        }
      }

      auto app = App::sharedApplication();
      if (!app) {
        writeHTTPAndClose(client, http::Response(500));
        return;
      }

      const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
      const auto clientId = client->id;
      app->dispatch([=, this]() {
        const int windowIndex = app->runtime.windowManager.getRandomWindowIndex(false);
        if (windowIndex < 0) {
          this->loop.dispatch([=, this]() {
            Client* client = nullptr;
            {
              Lock lock(this->mutex);
              if (this->clients.contains(stream)) {
                auto candidate = this->clients.at(stream);
                if (candidate && candidate->id == clientId) {
                  client = candidate;
                }
              }
            }

            if (!client || client->closing || client->closed) return;
            writeHTTPAndClose(client, http::Response(503));
          });
          return;
        }

        window::Window::Options options;
        options.index = windowIndex;
        options.headless = app->runtime.userConfig["build_headless"] == "true";
        options.debug = true;

        auto createdWindow = app->runtime.windowManager.createWindow(options);
        if (!createdWindow) {
          this->loop.dispatch([=, this]() {
            Client* client = nullptr;
            {
              Lock lock(this->mutex);
              if (this->clients.contains(stream)) {
                auto candidate = this->clients.at(stream);
                if (candidate && candidate->id == clientId) {
                  client = candidate;
                }
              }
            }

            if (!client || client->closing || client->closed) return;
            writeHTTPAndClose(client, http::Response(500));
          });
          return;
        }

        createdWindow->navigate(newUrl);

      #if !ORO_RUNTIME_PLATFORM_ANDROID
        createdWindow->show();
      #endif

        this->loop.dispatch([=, this]() {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) return;

          const auto target = this->getOrCreateTargetForWindow(windowIndex);
          if (target.targetId.empty()) {
            writeHTTPAndClose(client, http::Response(500));
            return;
          }

          const auto body = JSON::Any(JSON::Object::Entries {
            {"description", ""},
            {"id", target.targetId},
            {"title", ""},
            {"type", "page"},
            {"url", createdWindow->bridge->navigator.location.str()},
            {"webSocketDebuggerUrl", String("ws://") + this->hostname + ":" + std::to_string(this->port.load()) + "/devtools/page/" + target.targetId}
          }).str();
          auto response = http::Response(200);
          response.setHeader("content-type", "application/json; charset=utf-8");
          response.setHeader("content-length", static_cast<uint64_t>(body.size()));
          response.body = body;
          writeHTTPAndClose(client, response);
        });
      });

      return;
    }

    const auto activatePrefix = std::string("/json/activate/");
    if (path.rfind(activatePrefix, 0) == 0) {
      const auto targetId = path.substr(activatePrefix.size());
      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();

      if (!maybeWindow.has_value() || !app) {
        writeHTTPAndClose(client, http::Response(404));
        return;
      }

      const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
      const auto clientId = client->id;
      app->dispatch([=, this]() {
        auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
        if (window) {
          window->focus();
        }

        this->loop.dispatch([=, this]() {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) return;
          writeHTTPAndClose(client, http::Response(200));
        });
      });

      return;
    }

    const auto closePrefix = std::string("/json/close/");
    if (path.rfind(closePrefix, 0) == 0) {
      const auto targetId = path.substr(closePrefix.size());
      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();

      if (!maybeWindow.has_value() || !app) {
        writeHTTPAndClose(client, http::Response(404));
        return;
      }

      const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
      const auto clientId = client->id;
      app->dispatch([=, this]() {
        app->runtime.windowManager.destroyWindow(maybeWindow.value());
        this->loop.dispatch([=, this]() {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) return;
          writeHTTPAndClose(client, http::Response(200));
        });
      });

      return;
    }

    writeHTTPAndClose(client, http::Response(404));
  }

  bool CDP::handleWebSocketHandshake (Client* client, const http::Request& request) {
    const auto webSocketKey = request.headers.get("sec-websocket-key").value.str();
    if (webSocketKey.empty()) {
      writeHTTPAndClose(client, http::Response(400));
      return false;
    }

    auto path = request.url.pathname;
    while (path.size() > 1 && path.back() == '/') {
      path.pop_back();
    }
    const auto prefixBrowser = std::string("/devtools/browser/");
    const auto prefixPage = std::string("/devtools/page/");

    if (path == "/devtools/browser") {
      client->kind = Client::Kind::BrowserWS;
    } else if (path.rfind(prefixBrowser, 0) == 0) {
      auto id = path.substr(prefixBrowser.size());
      while (!id.empty() && id.back() == '/') {
        id.pop_back();
      }
      // Be lenient: accept any browser id path segment. Some tooling caches or
      // truncates the id, and we only have a single browser target anyway.
      client->kind = Client::Kind::BrowserWS;
    } else if (path.rfind(prefixPage, 0) == 0) {
      auto tid = path.substr(prefixPage.size());
      while (!tid.empty() && tid.back() == '/') {
        tid.pop_back();
      }
      if (tid.empty()) {
        writeHTTPAndClose(client, http::Response(404));
        return false;
      }

      {
        Lock lock(this->mutex);
        if (!this->targetWindows.contains(tid)) {
          writeHTTPAndClose(client, http::Response(404));
          return false;
        }
      }

      client->kind = Client::Kind::PageWS;
      client->boundTargetId = tid;
    } else {
      writeHTTPAndClose(client, http::Response(404));
      return false;
    }

    const auto accept = bytes::base64::encode(SHA1(webSocketKey + WS_GUID).finalize());
    auto response = http::Response(101);
    response.setHeader("upgrade", "websocket");
    response.setHeader("connection", "upgrade");
    response.setHeader("sec-websocket-accept", accept);

    // Some clients (including browser automation tooling) request a subprotocol.
    // Select the first one to keep the handshake RFC-compliant.
    const auto requestedProtocols = request.headers.get("sec-websocket-protocol").value.str();
    if (!requestedProtocols.empty()) {
      for (const auto& t : oro::runtime::string::split(requestedProtocols, ',')) {
        const auto p = oro::runtime::string::trim(t);
        if (!p.empty()) {
          response.setHeader("sec-websocket-protocol", p);
          break;
        }
      }
    }
    this->writeHTTP(client, response, false);

    return true;
  }

  void CDP::writeHTTP (Client* client, const http::Response& response, bool closeAfter) {
    const auto text = response.str() + response.body.str();
    WriteRequest* wr = new WriteRequest();
    wr->data.assign(text.begin(), text.end());
    wr->callback = nullptr;
    wr->closeAfter = closeAfter;

    uv_buf_t b = uv_buf_init(wr->data.data(), wr->data.size());
    wr->req.data = wr;

    const int writeResult = uv_write(
      &wr->req,
      reinterpret_cast<uv_stream_t*>(client->handle),
      &b,
      1,
      [](uv_write_t* req, int status) {
        auto wr = static_cast<WriteRequest*>(req->data);
        auto client = static_cast<Client*>(uv_handle_get_data(reinterpret_cast<uv_handle_t*>(req->handle)));

        if (wr->callback) {
          wr->callback(status);
        }

        const bool closeAfter = wr->closeAfter;
        delete wr;

        if (closeAfter && client && !client->closing && !client->closed) {
          client->closing = true;
          uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
          uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
            auto client = static_cast<Client*>(uv_handle_get_data(h));
            uv_handle_set_data(h, nullptr);
            if (client && client->cdp) {
              Lock lock(client->cdp->mutex);
              client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
            }
            if (client) {
              client->closed = true;
              delete client->handle;
              client->handle = nullptr;
              delete client;
            }
          });
        }
      }
    );

    if (writeResult != 0) {
      delete wr;

      if (client && !client->closing && !client->closed) {
        client->closing = true;
        uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
        uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
          auto client = static_cast<Client*>(uv_handle_get_data(h));
          uv_handle_set_data(h, nullptr);
          if (client && client->cdp) {
            Lock lock(client->cdp->mutex);
            client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
          }
          if (client) {
            client->closed = true;
            delete client->handle;
            client->handle = nullptr;
            delete client;
          }
        });
      }
    }
  }

  void CDP::writeHTTPAndClose (Client* client, const http::Response& response) {
    this->writeHTTP(client, response, true);
  }

  void CDP::processWebSocketFrames (Client* client) {
    // Minimal incremental parser for masked client frames.
    while (true) {
      if (client->readBuffer.size() < 2) {
        return;
      }

      const unsigned char* data = reinterpret_cast<const unsigned char*>(client->readBuffer.data());
      const bool fin = (data[0] & 0x80) != 0;
      const int opcode = data[0] & 0x0F;
      const bool masked = (data[1] & 0x80) != 0;
      uint64_t payloadLen = data[1] & 0x7F;
      size_t pos = 2;

      if (payloadLen == 126) {
        if (client->readBuffer.size() < 4) return;
        payloadLen = (data[2] << 8) | data[3];
        pos = 4;
      } else if (payloadLen == 127) {
        if (client->readBuffer.size() < 10) return;
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
          payloadLen = (payloadLen << 8) | data[2 + i];
        }
        pos = 10;
      }

      if (payloadLen > MAX_WS_MESSAGE_SIZE) {
        // Avoid unbounded allocations / OOM.
        if (!client->closing && !client->closed) {
          client->closing = true;
          uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
          uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
            auto client = static_cast<Client*>(uv_handle_get_data(h));
            uv_handle_set_data(h, nullptr);
            if (client && client->cdp) {
              Lock lock(client->cdp->mutex);
              client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
            }
            if (client) {
              client->closed = true;
              delete client->handle;
              client->handle = nullptr;
              delete client;
            }
          });
        }
        return;
      }

      if (!masked) {
        // RFC requires client -> server frames to be masked. Close.
        if (!client->closing && !client->closed) {
          client->closing = true;
          uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
          uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
            auto client = static_cast<Client*>(uv_handle_get_data(h));
            uv_handle_set_data(h, nullptr);
            if (client && client->cdp) {
              Lock lock(client->cdp->mutex);
              client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
            }
            if (client) {
              client->closed = true;
              delete client->handle;
              client->handle = nullptr;
              delete client;
            }
          });
        }
        return;
      }

      if (client->readBuffer.size() < pos + 4 + payloadLen) {
        return;
      }

      unsigned char mask[4];
      std::memcpy(mask, data + pos, 4);
      pos += 4;

      std::string payload;
      payload.resize(payloadLen);
      for (uint64_t i = 0; i < payloadLen; ++i) {
        payload[i] = static_cast<char>(data[pos + i] ^ mask[i % 4]);
      }

      client->readBuffer.erase(0, pos + payloadLen);

      if (opcode == 0x08) {
        // close
        client->closing = true;
        uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
        uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
          auto client = static_cast<Client*>(uv_handle_get_data(h));
          uv_handle_set_data(h, nullptr);
          if (client && client->cdp) {
            Lock lock(client->cdp->mutex);
            client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
          }
          if (client) {
            client->closed = true;
            delete client->handle;
            client->handle = nullptr;
            delete client;
          }
        });
        return;
      }

      if (opcode == 0x09) {
        // ping -> pong
        this->sendWebSocketFrame(client, 0x0A, payload.data(), payload.size());
        continue;
      }

      if (opcode == 0x0) {
        // continuation
        if (client->wsMessageBuffer.size() + payload.size() > MAX_WS_MESSAGE_SIZE) {
          if (!client->closing && !client->closed) {
            client->closing = true;
            uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
            uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
              auto client = static_cast<Client*>(uv_handle_get_data(h));
              uv_handle_set_data(h, nullptr);
              if (client && client->cdp) {
                Lock lock(client->cdp->mutex);
                client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
              }
              if (client) {
                client->closed = true;
                delete client->handle;
                client->handle = nullptr;
                delete client;
              }
            });
          }
          return;
        }
        client->wsMessageBuffer.append(payload);
        if (fin) {
          this->handleWebSocketMessage(client, client->wsMessageBuffer);
          client->wsMessageBuffer.clear();
        }
        continue;
      }

      if (opcode == 0x01) {
        // text
        if (fin) {
          this->handleWebSocketMessage(client, payload);
        } else {
          client->wsMessageBuffer = payload;
        }
        continue;
      }

      // ignore other opcodes for now
    }
  }

  void CDP::handleWebSocketMessage (Client* client, const std::string& text) {
    JSON::Any msg;
    try {
      msg = JSON::parse(text);
    } catch (...) {
      return;
    }

    this->handleCDPCommand(client, msg);
  }

  void CDP::sendWebSocketFrame (Client* client, int opcode, const char* data, size_t length, const Function<void(int)>& cb) {
    if (client == nullptr || client->closing || client->closed) {
      return;
    }

    std::vector<char> frame;
    const uint64_t len = static_cast<uint64_t>(length);

    // Guard server-sent frames as well to avoid giant allocations/OOM.
    if (len > MAX_WS_MESSAGE_SIZE) {
      client->closing = true;
      uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
      uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
        auto client = static_cast<Client*>(uv_handle_get_data(h));
        uv_handle_set_data(h, nullptr);
        if (client && client->cdp) {
          Lock lock(client->cdp->mutex);
          client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
        }
        if (client) {
          client->closed = true;
          delete client->handle;
          client->handle = nullptr;
          delete client;
        }
      });
      return;
    }

    if (len <= 125) {
      frame.resize(2 + len);
      frame[1] = static_cast<char>(len);
    } else if (len <= 65535) {
      frame.resize(4 + len);
      frame[1] = 126;
      frame[2] = static_cast<char>((len >> 8) & 0xFF);
      frame[3] = static_cast<char>(len & 0xFF);
    } else {
      frame.resize(10 + len);
      frame[1] = 127;
      for (int i = 0; i < 8; i++) {
        frame[9 - i] = static_cast<char>((len >> (i * 8)) & 0xFF);
      }
    }

    frame[0] = static_cast<char>(0x80 | (opcode & 0x0F)); // FIN + opcode

    if (data != nullptr && len > 0) {
      std::memcpy(frame.data() + (frame.size() - len), data, len);
    }

    WriteRequest* wr = new WriteRequest();
    wr->data = std::move(frame);
    wr->callback = cb;
    uv_buf_t b = uv_buf_init(wr->data.data(), wr->data.size());
    wr->req.data = wr;

    const int writeResult = uv_write(
      &wr->req,
      reinterpret_cast<uv_stream_t*>(client->handle),
      &b,
      1,
      [](uv_write_t* req, int status) {
        auto wr = static_cast<WriteRequest*>(req->data);
        if (wr->callback) {
          wr->callback(status);
        }
        delete wr;
      }
    );

    if (writeResult != 0) {
      if (wr->callback) {
        wr->callback(writeResult);
      }
      delete wr;

      if (!client->closing && !client->closed) {
        client->closing = true;
        uv_read_stop(reinterpret_cast<uv_stream_t*>(client->handle));
        uv_close(reinterpret_cast<uv_handle_t*>(client->handle), [](uv_handle_t* h) {
          auto client = static_cast<Client*>(uv_handle_get_data(h));
          uv_handle_set_data(h, nullptr);
          if (client && client->cdp) {
            Lock lock(client->cdp->mutex);
            client->cdp->clients.erase(reinterpret_cast<uv_stream_t*>(h));
          }
          if (client) {
            client->closed = true;
            delete client->handle;
            client->handle = nullptr;
            delete client;
          }
        });
      }
    }
  }

  void CDP::sendWebSocketText (Client* client, const std::string& text, const Function<void(int)>& cb) {
    this->sendWebSocketFrame(client, 0x01, text.data(), text.size(), cb);
  }

  void CDP::sendWebSocketJSON (Client* client, const JSON::Any& message) {
    this->sendWebSocketText(client, message.str());
  }

  void CDP::sendCDPResult (Client* client, int id, const JSON::Any& result, const String& sessionId) {
    const auto inner = JSON::Any(JSON::Object::Entries {
      {"id", JSON::Number(id)},
      {"result", result}
    });

    if (!sessionId.empty() && client && client->kind == Client::Kind::BrowserWS) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key) && !client->sessions.at(key).flatten) {
        const auto outer = JSON::Any(JSON::Object::Entries {
          {"method", "Target.receivedMessageFromTarget"},
          {"params", JSON::Object::Entries {
            {"sessionId", sessionId},
            {"message", inner.str()}
          }}
        });
        this->sendWebSocketJSON(client, outer);
        return;
      }
    }

    if (!sessionId.empty()) {
      auto msg = inner;
      msg.as<JSON::Object>().set("sessionId", sessionId);
      this->sendWebSocketJSON(client, msg);
      return;
    }

    this->sendWebSocketJSON(client, inner);
  }

  void CDP::sendCDPError (Client* client, int id, int code, const std::string& message, const String& sessionId) {
    const auto inner = JSON::Any(JSON::Object::Entries {
      {"id", JSON::Number(id)},
      {"error", JSON::Object::Entries {
        {"code", JSON::Number(code)},
        {"message", message}
      }}
    });

    if (!sessionId.empty() && client && client->kind == Client::Kind::BrowserWS) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key) && !client->sessions.at(key).flatten) {
        const auto outer = JSON::Any(JSON::Object::Entries {
          {"method", "Target.receivedMessageFromTarget"},
          {"params", JSON::Object::Entries {
            {"sessionId", sessionId},
            {"message", inner.str()}
          }}
        });
        this->sendWebSocketJSON(client, outer);
        return;
      }
    }

    if (!sessionId.empty()) {
      auto msg = inner;
      msg.as<JSON::Object>().set("sessionId", sessionId);
      this->sendWebSocketJSON(client, msg);
      return;
    }

    this->sendWebSocketJSON(client, inner);
  }

  void CDP::sendCDPEvent (Client* client, const std::string& method, const JSON::Any& params, const String& sessionId) {
    const auto inner = JSON::Any(JSON::Object::Entries {
      {"method", method},
      {"params", params}
    });

    if (!sessionId.empty() && client && client->kind == Client::Kind::BrowserWS) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key) && !client->sessions.at(key).flatten) {
        const auto outer = JSON::Any(JSON::Object::Entries {
          {"method", "Target.receivedMessageFromTarget"},
          {"params", JSON::Object::Entries {
            {"sessionId", sessionId},
            {"message", inner.str()}
          }}
        });
        this->sendWebSocketJSON(client, outer);
        return;
      }
    }

    if (!sessionId.empty()) {
      auto msg = inner;
      msg.as<JSON::Object>().set("sessionId", sessionId);
      this->sendWebSocketJSON(client, msg);
      return;
    }

    this->sendWebSocketJSON(client, inner);
  }

  std::vector<CDP::TargetInfo> CDP::snapshotTargets () {
    std::vector<TargetInfo> targets;

    // Browser target (synthetic)
    targets.push_back(TargetInfo {
      .targetId = this->browserId,
      .windowIndex = -1,
      .type = "browser",
      .title = "",
      .url = "",
      .attached = true,
      .browserContextId = ""
    });

    auto app = App::sharedApplication();
    if (!app) {
      return targets;
    }

    for (int i = 0; i < app->runtime.windowManager.windows.size(); ++i) {
      auto window = app->runtime.windowManager.getWindow(i);
      if (!window) {
        continue;
      }

      auto t = this->getOrCreateTargetForWindow(i);
      t.title = "";
      t.url = window->bridge->navigator.location.str();
      targets.push_back(t);
    }

    return targets;
  }

  CDP::TargetInfo CDP::getOrCreateTargetForWindow (int windowIndex) {
    Lock lock(this->mutex);
    TargetInfo info;
    info.windowIndex = windowIndex;
    info.type = "page";

    // For Playwright/Puppeteer compatibility, always surface a browserContextId.
    if (this->windowBrowserContextId.contains(windowIndex)) {
      info.browserContextId = this->windowBrowserContextId.at(windowIndex);
    } else {
      info.browserContextId = this->defaultBrowserContextId;
    }

    if (this->windowTargets.contains(windowIndex)) {
      info.targetId = this->windowTargets.at(windowIndex);
    } else {
      const auto id = uuid::v7();
      this->windowTargets.emplace(windowIndex, id);
      this->targetWindows.emplace(id, windowIndex);
      info.targetId = id;
    }

    if (!this->windowExecutionContexts.contains(windowIndex)) {
      this->windowExecutionContexts.emplace(windowIndex, this->nextExecutionContextId.fetch_add(1));
    }

    return info;
  }

  std::optional<int> CDP::resolveWindowIndexForTarget (const String& targetId) {
    Lock lock(this->mutex);
    const auto key = std::string(targetId);
    if (this->targetWindows.contains(key)) {
      return this->targetWindows.at(key);
    }
    return std::nullopt;
  }

  void CDP::handleCDPCommand (Client* client, const JSON::Any& msg) {
    if (!msg.isObject()) {
      return;
    }

    const int id = jsonIntOr(msg, "id", -1);
    const auto method = jsonStringOrEmpty(msg, "method");
    const String sessionId = jsonStringOrEmpty(msg, "sessionId");

    const auto params = [&]() -> JSON::Any {
      const auto& paramsAny = msg.as<JSON::Object>().get("params");
      if (paramsAny.isObject()) {
        return paramsAny;
      }
      return JSON::Object::Entries {};
    }();

    if (id < 0 || method.empty()) {
      return;
    }

    if (method == "Browser.getVersion") {
      this->handleBrowserGetVersion(client, id, sessionId);
      return;
    }

    if (method == "Browser.getBrowserCommandLine") {
      // Stub: Oro does not expose the full underlying process command
      // line via CDP.
      this->sendCDPResult(client, id, JSON::Object::Entries { {"arguments", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "Browser.getWindowForTarget") {
      this->handleBrowserGetWindowForTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Browser.getWindowBounds") {
      this->handleBrowserGetWindowBounds(client, id, params, sessionId);
      return;
    }

    if (method == "Browser.setContentsSize") {
      this->handleBrowserSetContentsSize(client, id, params, sessionId);
      return;
    }

    if (method == "Browser.setWindowBounds") {
      this->handleBrowserSetWindowBounds(client, id, params, sessionId);
      return;
    }

    if (method == "Browser.grantPermissions" || method == "Browser.resetPermissions") {
      // Stub: permissions are managed by the runtime/platform.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Browser.close") {
      // Close the default window (typically exits the app on desktop).
      auto app = App::sharedApplication();
      if (app) {
        app->dispatch([app]() {
          app->runtime.windowManager.destroyWindow(0);
        });
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Security.setIgnoreCertificateErrors") {
      // Stub: the runtime TLS and WebKit networking stacks are not
      // currently wired up to CDP security settings.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Browser.setDownloadBehavior") {
      // Stub: downloads are handled by the runtime and may vary per
      // platform.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Browser.cancelDownload") {
      // Stub: downloads are handled by the runtime/platform. This is a
      // compatibility no-op for automation tooling.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Animation.getPlaybackRate" || method == "Animation.setPlaybackRate") {
      // Compatibility stubs for automation tooling. Oro does not currently
      // apply playback rate to the underlying engine, but we store and return
      // the value so tooling doesn't break.
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (targetId.empty()) {
        this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
        return;
      }

      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      if (!maybeWindow.has_value()) {
        this->sendCDPError(client, id, -32000, "Target not found", sessionId);
        return;
      }

      const int windowIndex = maybeWindow.value();

      if (method == "Animation.setPlaybackRate") {
        double playbackRate = jsonDoubleOr(params, "playbackRate", 1.0);
        if (!std::isfinite(playbackRate) || playbackRate <= 0) {
          playbackRate = 1.0;
        }

        {
          Lock lock(this->mutex);
          this->windowAnimationPlaybackRate[windowIndex] = playbackRate;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      double playbackRate = 1.0;
      {
        Lock lock(this->mutex);
        if (this->windowAnimationPlaybackRate.contains(windowIndex)) {
          playbackRate = this->windowAnimationPlaybackRate.at(windowIndex);
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries { {"playbackRate", JSON::Number(playbackRate)} }, sessionId);
      return;
    }

    if (method == "Target.setDiscoverTargets") {
      this->handleTargetSetDiscoverTargets(client, id, params, sessionId);
      return;
    }

    if (method == "Target.getTargets") {
      this->handleTargetGetTargets(client, id, sessionId);
      return;
    }

    if (method == "Target.setAutoAttach") {
      this->handleTargetSetAutoAttach(client, id, params, sessionId);
      return;
    }

    if (method == "Target.attachToBrowserTarget") {
      if (client->kind != Client::Kind::BrowserWS) {
        this->sendCDPError(client, id, -32000, "Not supported", sessionId);
        return;
      }

      const auto sid = uuid::v7();
      client->sessions.emplace(std::string(sid), SessionInfo {
        .sessionId = sid,
        .targetId = this->browserId,
        .flatten = true,
        .parentSessionId = sessionId
      });

      this->sendCDPResult(client, id, JSON::Object::Entries { {"sessionId", sid} }, sessionId);
      return;
    }

    if (method == "Target.attachToTarget") {
      this->handleTargetAttachToTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.detachFromTarget") {
      this->handleTargetDetachFromTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.sendMessageToTarget") {
      this->handleTargetSendMessageToTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.getTargetInfo") {
      this->handleTargetGetTargetInfo(client, id, params, sessionId);
      return;
    }

    if (method == "Target.createTarget") {
      this->handleTargetCreateTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.closeTarget") {
      this->handleTargetCloseTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.activateTarget") {
      this->handleTargetActivateTarget(client, id, params, sessionId);
      return;
    }

    if (method == "Target.openDevTools") {
      // Stub: Oro does not ship a DevTools frontend.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Target.getBrowserContexts") {
      JSON::Array::Entries ids;
      {
        Lock lock(this->mutex);
        for (const auto& cid : this->browserContextIds) {
          ids.push_back(String(cid));
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries { {"browserContextIds", ids} }, sessionId);
      return;
    }

    if (method == "Target.createBrowserContext") {
      const auto contextId = uuid::v7();
      {
        Lock lock(this->mutex);
        this->browserContextIds.insert(std::string(contextId));
      }

      this->sendCDPResult(client, id, JSON::Object::Entries { {"browserContextId", contextId} }, sessionId);
      return;
    }

    if (method == "Target.disposeBrowserContext") {
      const auto contextId = jsonStringOrEmpty(params, "browserContextId");
      if (contextId.empty()) {
        this->sendCDPError(client, id, -32602, "Missing browserContextId", sessionId);
        return;
      }

      std::vector<int> windowsToClose;
      bool found = false;
      {
        Lock lock(this->mutex);
        const auto key = std::string(contextId);
        found = this->browserContextIds.contains(key);
        if (!found) {
          // fallthrough
        } else {
          this->browserContextIds.erase(key);

          for (const auto& kv : this->windowBrowserContextId) {
            if (kv.second == key) {
              windowsToClose.push_back(kv.first);
            }
          }
          for (const int windowIndex : windowsToClose) {
            this->windowBrowserContextId.erase(windowIndex);
          }
        }
      }

      if (!found) {
        this->sendCDPError(client, id, -32000, "Browser context not found", sessionId);
        return;
      }

      if (!windowsToClose.empty()) {
        auto app = App::sharedApplication();
        if (app) {
          app->dispatch([=]() {
            for (const int windowIndex : windowsToClose) {
              app->runtime.windowManager.destroyWindow(windowIndex);
            }
          });
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Page.enable") {
      // Report current navigation state for the attached target.
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (!targetId.empty()) {
        const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
        if (maybeWindow.has_value()) {
          auto app = App::sharedApplication();
          if (app) {
            auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
            if (window) {
              this->sendCDPEvent(client, "Page.frameNavigated", JSON::Object::Entries {
                {"frame", JSON::Object::Entries {
                  {"id", targetId},
                  {"loaderId", targetId},
                  {"url", window->bridge->navigator.location.str()},
                  {"name", ""},
                  {"mimeType", "text/html"},
                  {"securityOrigin", window->bridge->navigator.location.origin}
                }}
              }, sessionId);
            }
          }
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Runtime.enable") {
      // Report current execution context for the attached target.
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (!targetId.empty()) {
        const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
        if (maybeWindow.has_value()) {
          auto app = App::sharedApplication();
          if (app) {
            auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
            if (window) {
              const int windowIndex = maybeWindow.value();
              const auto frameId = targetId;
              const auto origin = window->bridge->navigator.location.origin;

              int contextId = 0;
              std::vector<std::pair<String, int>> isolatedWorldContexts;

              {
                Lock lock(this->mutex);
                if (this->windowExecutionContexts.contains(windowIndex)) {
                  contextId = this->windowExecutionContexts.at(windowIndex);
                } else {
                  contextId = this->nextExecutionContextId.fetch_add(1);
                  this->windowExecutionContexts[windowIndex] = contextId;
                }

                if (this->windowIsolatedWorldExecutionContexts.contains(windowIndex)) {
                  const auto& frameMap = this->windowIsolatedWorldExecutionContexts.at(windowIndex);
                  const auto frameKey = std::string(frameId);
                  if (frameMap.contains(frameKey)) {
                    for (const auto& kv : frameMap.at(frameKey)) {
                      isolatedWorldContexts.push_back(std::make_pair(String(kv.first), kv.second));
                    }
                  }
                }
              }

              if (contextId > 0) {
                this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
                  {"context", JSON::Object::Entries {
                    {"id", JSON::Number(contextId)},
                    {"origin", origin},
                    {"name", ""},
                    {"auxData", JSON::Object::Entries {
                      {"isDefault", JSON::Boolean(true)},
                      {"type", "default"},
                      {"frameId", frameId}
                    }}
                  }}
                }, sessionId);
              }

              for (const auto& entry : isolatedWorldContexts) {
                this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
                  {"context", JSON::Object::Entries {
                    {"id", JSON::Number(entry.second)},
                    {"origin", origin},
                    {"name", entry.first},
                    {"auxData", JSON::Object::Entries {
                      {"isDefault", JSON::Boolean(false)},
                      {"type", "isolated"},
                      {"frameId", frameId}
                    }}
                  }}
                }, sessionId);
              }
            }
          }
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Log.enable") {
      client->logEnabled = true;
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Log.disable") {
      client->logEnabled = false;
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Log.clear") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Runtime.runIfWaitingForDebugger") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Runtime.getIsolateId") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"id", this->browserId} }, sessionId);
      return;
    }

    if (method == "Runtime.discardConsoleEntries") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

	    if (method == "Network.getResponseBody") {
	      this->handleNetworkGetResponseBody(client, id, params, sessionId);
	      return;
	    }

	    if (method == "Fetch.getResponseBody") {
	      // Same shape as Network.getResponseBody, keyed by requestId.
	      this->handleNetworkGetResponseBody(client, id, params, sessionId);
	      return;
	    }

	    if (method == "Network.getRequestPostData") {
	      this->handleNetworkGetRequestPostData(client, id, params, sessionId);
	      return;
	    }

	    if (method == "Network.getResponseBodyForInterception") {
	      const auto interceptionId = jsonStringOrEmpty(params, "interceptionId");
	      if (interceptionId.empty()) {
	        this->sendCDPError(client, id, -32602, "Missing interceptionId", sessionId);
	        return;
	      }
	      this->handleNetworkGetResponseBody(client, id, JSON::Object::Entries { {"requestId", interceptionId} }, sessionId);
	      return;
	    }

	    if (method == "Fetch.takeResponseBodyAsStream") {
	      this->handleTakeResponseBodyAsStream(client, id, params, sessionId, "requestId");
	      return;
	    }

	    if (method == "Network.takeResponseBodyForInterceptionAsStream") {
	      this->handleTakeResponseBodyAsStream(client, id, params, sessionId, "interceptionId");
	      return;
	    }

		    if (method == "IO.read") {
		      this->handleIORead(client, id, params, sessionId);
		      return;
		    }

		    if (method == "IO.resolveBlob") {
		      // Stub: blob UUIDs are used by Chromium DevTools internals; Oro does
		      // not currently expose a blob-by-UUID fetch path.
		      this->sendCDPResult(client, id, JSON::Object::Entries { {"uuid", uuid::v7()} }, sessionId);
		      return;
		    }

		    if (method == "IO.close") {
		      this->handleIOClose(client, id, params, sessionId);
		      return;
		    }

	    if (method == "Network.setExtraHTTPHeaders") {
	      if (!params.isObject()) {
	        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	        return;
      }

      const auto headersAny = params.as<JSON::Object>().get("headers");
      const auto headersJson = headersAny.isObject() ? headersAny.str() : "{}";

      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (targetId.empty()) {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();
      if (!maybeWindow.has_value() || !app) {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      const int windowIndex = maybeWindow.value();

      // Persist at runtime level so native-handled requests (SchemeHandlers)
      // can apply extra headers too.
      http::Headers extra;
      if (headersAny.isObject()) {
        for (const auto& kv : headersAny.as<JSON::Object>().value()) {
          const auto& k = kv.first;
          const auto& v = kv.second;
          if (v.isString()) {
            extra.set(k, v.as<JSON::String>().value());
          } else if (v.isNumber()) {
            extra.set(k, std::to_string(v.as<JSON::Number>().value()));
          } else if (v.isBoolean()) {
            extra.set(k, v.as<JSON::Boolean>().value() ? "true" : "false");
          }
        }
      }

      {
        Lock lock(this->mutex);
        if (extra.empty()) {
          this->windowExtraHTTPHeaders.erase(windowIndex);
        } else {
          this->windowExtraHTTPHeaders.insert_or_assign(windowIndex, extra);
        }
      }

      // Applies to fetch/XHR via injected instrumentation.
      if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
        const auto script = std::string(
          "(function(){"
        ) + ORO_CDP_BOOTSTRAP + std::string(
          " try {"
          "  const __oro = globalThis.__oro_cdp;"
          "  if (__oro && __oro.net && typeof __oro.net.setExtraHeaders === 'function') {"
          "    __oro.net.setExtraHeaders(" + headersJson + ");"
          "  }"
          " } catch (e) {}"
          "})()"
        );
        window->eval(script);
      }

	      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	      return;
	    }

	    if (
	      method == "Network.canClearBrowserCache" ||
	      method == "Network.canClearBrowserCookies" ||
	      method == "Network.canEmulateNetworkConditions"
	    ) {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Boolean(true)} }, sessionId);
	      return;
	    }

	    if (method == "Network.emulateNetworkConditionsByRule") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"ruleIds", JSON::Array::Entries {}} }, sessionId);
	      return;
	    }

	    if (method == "Network.getAllCookies") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"cookies", JSON::Array::Entries {}} }, sessionId);
	      return;
	    }

	    if (method == "Network.getCertificate") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"tableNames", JSON::Array::Entries {}} }, sessionId);
	      return;
	    }

	    if (method == "Network.searchInResponseBody") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Array::Entries {}} }, sessionId);
	      return;
	    }

	    if (method == "Network.setCookie") {
	      // Stub: cookie mutation is platform-dependent.
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"success", JSON::Boolean(true)} }, sessionId);
	      return;
	    }

	    if (method == "Network.streamResourceContent") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"bufferedData", ""} }, sessionId);
	      return;
	    }

	    if (method == "Network.getSecurityIsolationStatus") {
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"status", JSON::Object::Entries {}} }, sessionId);
	      return;
	    }

	    if (method == "Network.loadNetworkResource") {
	      this->sendCDPResult(client, id, JSON::Object::Entries {
	        {"resource", JSON::Object::Entries {
	          {"success", JSON::Boolean(false)},
	          {"netErrorName", "NotSupported"}
	        }}
	      }, sessionId);
	      return;
	    }

	    if (
	      method == "Network.setAcceptedEncodings" ||
	      method == "Network.clearAcceptedEncodingsOverride" ||
	      method == "Network.overrideNetworkState" ||
	      method == "Network.configureDurableMessages" ||
	      method == "Network.enableReportingApi" ||
	      method == "Network.setCookieControls" ||
	      method == "Network.setAttachDebugStack" ||
	      method == "Network.replayXHR"
	    ) {
	      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	      return;
	    }

	    const auto resolveWindowIndexForSession = [&]() -> std::optional<int> {
	      String targetId;
	      if (client->kind == Client::Kind::PageWS) {
	        targetId = client->boundTargetId;
	      } else if (!sessionId.empty()) {
	        const auto key = std::string(sessionId);
	        if (client->sessions.contains(key)) {
	          targetId = client->sessions.at(key).targetId;
	        }
	      }
	      if (targetId.empty()) {
	        return std::nullopt;
	      }
	      return this->resolveWindowIndexForTarget(targetId);
	    };

	    if (method == "Network.setBlockedURLs") {
	      const auto urlsAny = params.isObject() ? params.as<JSON::Object>().get("urls") : JSON::Any();
	      std::vector<String> patterns;
	      JSON::Array::Entries patternsJson;
	      if (urlsAny.isArray()) {
	        for (const auto& uAny : urlsAny.as<JSON::Array>()) {
	          if (!uAny.isString()) continue;
	          const auto u = uAny.as<JSON::String>().value();
	          if (!u.empty()) {
	            patterns.push_back(u);
	            patternsJson.push_back(u);
	          }
	        }
	      }

	      const auto maybeWindow = resolveWindowIndexForSession();
	      auto app = App::sharedApplication();
	      if (!maybeWindow.has_value() || !app) {
	        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	        return;
	      }

	      const int windowIndex = maybeWindow.value();
	      {
	        Lock lock(this->mutex);
	        if (patterns.empty()) {
	          this->windowBlockedURLPatterns.erase(windowIndex);
	        } else {
	          this->windowBlockedURLPatterns.insert_or_assign(windowIndex, patterns);
	        }
	      }

	      if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
	        const auto script = std::string(
	          "(function(){"
	        ) + ORO_CDP_BOOTSTRAP + std::string(
	          " try {"
	          "  const __oro = globalThis.__oro_cdp;"
	          "  if (__oro && __oro.net) {"
	        ) + (patterns.empty()
	          ? std::string("    if (typeof __oro.net.clearBlockedURLs === 'function') __oro.net.clearBlockedURLs();")
	          : std::string("    if (typeof __oro.net.setBlockedURLs === 'function') __oro.net.setBlockedURLs(") + JSON::Any(patternsJson).str() + ");"
	        ) + std::string(
	          "  }"
	          " } catch (e) {}"
	          "})()"
	        );
	        window->eval(script);
	      }

	      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	      return;
	    }

	    if (method == "Network.setRequestInterception") {
	      const auto patternsAny = params.isObject() ? params.as<JSON::Object>().get("patterns") : JSON::Any();
	      std::vector<FetchPattern> patterns;

	      if (patternsAny.isArray()) {
	        for (const auto& pAny : patternsAny.as<JSON::Array>()) {
	          if (!pAny.isObject()) continue;
	          const auto& pObj = pAny.as<JSON::Object>();
	          const auto stageAny = pObj.get("interceptionStage");
	          if (stageAny.isString() && stageAny.as<JSON::String>().value() != "Request") {
	            continue;
	          }

	          FetchPattern p;
	          const auto urlPatternAny = pObj.get("urlPattern");
	          if (urlPatternAny.isString()) {
	            p.urlPattern = urlPatternAny.as<JSON::String>().value();
	          }

	          const auto rtAny = pObj.get("resourceType");
	          if (rtAny.isString()) {
	            p.resourceType = rtAny.as<JSON::String>().value();
	          }

	          patterns.push_back(p);
	        }
	      }

	      const auto maybeWindow = resolveWindowIndexForSession();
	      auto app = App::sharedApplication();
	      if (!maybeWindow.has_value() || !app) {
	        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	        return;
	      }

	      const int windowIndex = maybeWindow.value();
	      const bool enabled = !patterns.empty();

	      std::vector<PendingFetchRequest> toResolve;
	      {
	        Lock lock(this->mutex);
	        this->windowNetworkRequestInterceptionEnabled.insert_or_assign(windowIndex, enabled);
	        this->windowFetchEnabled.insert_or_assign(windowIndex, enabled);

	        if (enabled) {
	          this->windowFetchPatterns.insert_or_assign(windowIndex, patterns);
	        } else {
	          this->windowFetchPatterns.erase(windowIndex);
	          for (auto it = this->pendingFetchRequests.begin(); it != this->pendingFetchRequests.end();) {
	            if (it->second.windowIndex == windowIndex) {
	              toResolve.push_back(it->second);
	              it = this->pendingFetchRequests.erase(it);
	            } else {
	              ++it;
	            }
	          }
	        }
	      }

	      for (auto& pending : toResolve) {
	        if (pending.resolve) {
	          pending.resolve(JSON::Any(JSON::Object::Entries {{"action", "continue"}}));
	        }
	      }

	      // Enable/disable in-page interception hooks via injected instrumentation.
	      if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
	        JSON::Array::Entries patternsOut;
	        patternsOut.reserve(patterns.size());
	        for (const auto& p : patterns) {
	          JSON::Object::Entries o;
	          if (p.urlPattern.has_value() && p.urlPattern.value().size() > 0) {
	            o.insert_or_assign("urlPattern", p.urlPattern.value());
	          }
	          if (p.resourceType.has_value() && p.resourceType.value().size() > 0) {
	            o.insert_or_assign("resourceType", p.resourceType.value());
	          }
	          patternsOut.push_back(o);
	        }

	        const auto patternsJson = JSON::Any(patternsOut).str();
	        const auto script = std::string(
	          "(function(){"
	        ) + ORO_CDP_BOOTSTRAP + std::string(
	          " try {"
	          "  const __oro = globalThis.__oro_cdp;"
	          "  if (__oro && __oro.net) {"
	        ) + (enabled
	          ? std::string("    if (typeof __oro.net.setFetchPatterns === 'function') { __oro.net.setFetchPatterns(") + patternsJson + "); }"
	          : std::string("    if (typeof __oro.net.clearFetchPatterns === 'function') { __oro.net.clearFetchPatterns(); }")
	        ) + std::string(
	          "  }"
	          " } catch (e) {}"
	          "})()"
	        );
	        window->eval(script);
	      }

	      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	      return;
	    }

	    if (method == "Network.continueInterceptedRequest") {
	      const auto interceptionId = jsonStringOrEmpty(params, "interceptionId");
	      if (interceptionId.empty()) {
	        this->sendCDPError(client, id, -32602, "Missing interceptionId", sessionId);
	        return;
	      }

	      PendingFetchRequest pending;
	      bool found = false;
	      {
	        Lock lock(this->mutex);
	        const auto key = std::string(interceptionId);
	        if (this->pendingFetchRequests.contains(key)) {
	          pending = this->pendingFetchRequests.at(key);
	          this->pendingFetchRequests.erase(key);
	          found = true;
	        }
	      }

	      if (!found) {
	        this->sendCDPError(client, id, -32000, "Request not found", sessionId);
	        return;
	      }

	      auto headersObjectOrEmpty = [](const JSON::Any& headersAny) -> JSON::Object::Entries {
	        JSON::Object::Entries out;
	        if (!headersAny.isObject()) return out;
	        for (const auto& kv : headersAny.as<JSON::Object>().value()) {
	          if (kv.second.isString()) {
	            out.insert_or_assign(kv.first, kv.second.as<JSON::String>().value());
	          }
	        }
	        return out;
	      };

	      JSON::Any decision = JSON::Any(JSON::Object::Entries {{"action", "continue"}});

	      const auto errorReason = jsonStringOrEmpty(params, "errorReason");
	      const auto rawResponse = jsonStringOrEmpty(params, "rawResponse");

	      if (!errorReason.empty()) {
	        decision = JSON::Any(JSON::Object::Entries {
	          {"action", "fail"},
	          {"errorReason", errorReason}
	        });
	      } else if (!rawResponse.empty()) {
	        int statusCode = 200;
	        JSON::Object::Entries responseHeaders;
	        String bodyBase64 = "";

	        try {
	          const auto decoded = bytes::base64::decode(rawResponse);
	          const std::string dec(decoded.begin(), decoded.end());
	          const auto headerPos = dec.find("\r\n\r\n");
	          if (headerPos != std::string::npos) {
	            const auto head = dec.substr(0, headerPos);
	            const auto body = dec.substr(headerPos + 4);
	            bodyBase64 = bytes::base64::encode(body);

	            std::vector<std::string> lines;
	            size_t start = 0;
	            while (start <= head.size()) {
	              const auto end = head.find("\r\n", start);
	              if (end == std::string::npos) {
	                lines.push_back(head.substr(start));
	                break;
	              }
	              lines.push_back(head.substr(start, end - start));
	              start = end + 2;
	            }

	            if (!lines.empty()) {
	              const auto& statusLine = lines[0];
	              const auto firstSpace = statusLine.find(' ');
	              if (firstSpace != std::string::npos) {
	                const auto secondSpace = statusLine.find(' ', firstSpace + 1);
	                const auto codeText = statusLine.substr(firstSpace + 1, (secondSpace == std::string::npos ? statusLine.size() : secondSpace) - (firstSpace + 1));
	                try { statusCode = std::stoi(codeText); } catch (...) {}
	              }
	            }

	            for (size_t i = 1; i < lines.size(); i++) {
	              const auto& line = lines[i];
	              const auto colon = line.find(':');
	              if (colon == std::string::npos) continue;
	              const auto name = oro::runtime::string::trim(line.substr(0, colon));
	              const auto value = oro::runtime::string::trim(line.substr(colon + 1));
	              if (!name.empty()) {
	                responseHeaders.insert_or_assign(name, value);
	              }
	            }
	          }
	        } catch (...) {}

	        auto entries = JSON::Object::Entries {
	          {"action", "fulfill"},
	          {"responseCode", JSON::Number(static_cast<double>(statusCode))},
	          {"responseHeaders", responseHeaders}
	        };

	        if (!bodyBase64.empty()) {
	          entries.insert_or_assign("body", bodyBase64);
	          entries.insert_or_assign("bodyBase64Encoded", JSON::Boolean(true));
	        }

	        decision = JSON::Any(entries);
	      } else {
	        auto entries = JSON::Object::Entries {{"action", "continue"}};
	        const auto urlAny = params.isObject() ? params.as<JSON::Object>().get("url") : JSON::Any();
	        if (urlAny.isString()) {
	          entries.insert_or_assign("url", urlAny.as<JSON::String>().value());
	        }
	        const auto methodAny = params.isObject() ? params.as<JSON::Object>().get("method") : JSON::Any();
	        if (methodAny.isString()) {
	          entries.insert_or_assign("method", methodAny.as<JSON::String>().value());
	        }
	        const auto headersAny = params.isObject() ? params.as<JSON::Object>().get("headers") : JSON::Any();
	        const auto headers = headersObjectOrEmpty(headersAny);
	        if (!headers.empty()) {
	          entries.insert_or_assign("headers", headers);
	        }
	        const auto postDataAny = params.isObject() ? params.as<JSON::Object>().get("postData") : JSON::Any();
	        if (postDataAny.isString()) {
	          auto postData = postDataAny.as<JSON::String>().value();
	          if (postData.size() > MAX_NETWORK_POSTDATA_RESULT_BYTES) {
	            postData = "";
	          }
	          if (!postData.empty()) {
	            entries.insert_or_assign("postData", postData);
	          }
	        }
	        decision = JSON::Any(entries);
	      }

	      if (pending.resolve) {
	        pending.resolve(decision);
	      }

	      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	      return;
	    }

	    if (method == "Network.getCookies" || method == "Storage.getCookies") {
	      // Stub: cookie inspection is platform-dependent.
	      this->sendCDPResult(client, id, JSON::Object::Entries { {"cookies", JSON::Array::Entries {}} }, sessionId);
	      return;
	    }

    if (method == "Network.setCookies" || method == "Storage.setCookies" || method == "Network.deleteCookies") {
      // Stub: cookie mutation is platform-dependent.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Storage.clearCookies") {
      // Stub: cookie mutation is platform-dependent.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Page.navigate") {
      this->handlePageNavigate(client, id, params, sessionId);
      return;
    }

    if (method == "Page.setDocumentContent") {
      this->handlePageSetDocumentContent(client, id, params, sessionId);
      return;
    }

    if (method == "Page.getFrameTree") {
      this->handlePageGetFrameTree(client, id, sessionId);
      return;
    }

    if (method == "Page.getResourceTree") {
      this->handlePageGetResourceTree(client, id, sessionId);
      return;
    }

    if (method == "Page.getResourceContent") {
      this->handlePageGetResourceContent(client, id, params, sessionId);
      return;
    }

    if (method == "Page.getLayoutMetrics") {
      this->handlePageGetLayoutMetrics(client, id, sessionId);
      return;
    }

    if (method == "Page.getNavigationHistory") {
      this->handlePageGetNavigationHistory(client, id, sessionId);
      return;
    }

    if (method == "Page.navigateToHistoryEntry") {
      this->handlePageNavigateToHistoryEntry(client, id, params, sessionId);
      return;
    }

    if (method == "Page.bringToFront") {
      this->handlePageBringToFront(client, id, sessionId);
      return;
    }

    if (method == "Page.reload") {
      this->handlePageReload(client, id, params, sessionId);
      return;
    }

    if (method == "Page.stopLoading") {
      this->handlePageStopLoading(client, id, params, sessionId);
      return;
    }

    if (method == "Page.close") {
      this->handlePageClose(client, id, params, sessionId);
      return;
    }

    if (method == "Page.startScreencast" || method == "Page.stopScreencast" || method == "Page.screencastFrameAck") {
      // Stub: streaming screencasts are not implemented (use Page.captureScreenshot when available).
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Page.captureScreenshot") {
      this->handlePageCaptureScreenshot(client, id, params, sessionId);
      return;
    }

    if (method == "Page.printToPDF") {
      // Not supported on most platforms yet.
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method == "Page.handleJavaScriptDialog") {
      // Stub: dialogs are handled by the platform UI.
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Page.addScriptToEvaluateOnNewDocument") {
      this->handlePageAddScriptToEvaluateOnNewDocument(client, id, params, sessionId);
      return;
    }

    if (method == "Page.removeScriptToEvaluateOnNewDocument") {
      this->handlePageRemoveScriptToEvaluateOnNewDocument(client, id, params, sessionId);
      return;
    }

    if (method == "Page.createIsolatedWorld") {
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (targetId.empty()) {
        this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
        return;
      }

      const auto frameIdParam = jsonStringOrEmpty(params, "frameId");
      const auto worldName = jsonStringOrEmpty(params, "worldName");
      const auto frameId = frameIdParam.empty() ? targetId : frameIdParam;

      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();
      if (!maybeWindow.has_value() || !app) {
        this->sendCDPError(client, id, -32000, "Target not found", sessionId);
        return;
      }

      auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
      if (!window) {
        this->sendCDPError(client, id, -32000, "Window not found", sessionId);
        return;
      }

      const int windowIndex = maybeWindow.value();
      const auto origin = window->bridge->navigator.location.origin;

      int contextId = 0;
      {
        Lock lock(this->mutex);

        if (worldName.empty()) {
          if (this->windowExecutionContexts.contains(windowIndex)) {
            contextId = this->windowExecutionContexts.at(windowIndex);
          } else {
            contextId = this->nextExecutionContextId.fetch_add(1);
            this->windowExecutionContexts[windowIndex] = contextId;
          }
        } else {
          const auto frameKey = std::string(frameId);
          auto& worldMap = this->windowIsolatedWorldExecutionContexts[windowIndex][frameKey];

          const auto worldKey = std::string(worldName);
          if (worldMap.contains(worldKey)) {
            contextId = worldMap.at(worldKey);
          } else {
            contextId = this->nextExecutionContextId.fetch_add(1);
            worldMap[worldKey] = contextId;
          }
        }
      }

      if (!worldName.empty()) {
        // Emit an executionContextCreated event so automation tooling can map
        // worldName -> ExecutionContext (Puppeteer/Playwright expect this).
        this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
          {"context", JSON::Object::Entries {
            {"id", JSON::Number(contextId)},
            {"origin", origin},
            {"name", worldName},
            {"auxData", JSON::Object::Entries {
              {"isDefault", JSON::Boolean(false)},
              {"type", "isolated"},
              {"frameId", frameId}
            }}
          }}
        }, sessionId);
      }

      this->sendCDPResult(client, id, JSON::Object::Entries { {"executionContextId", JSON::Number(contextId)} }, sessionId);
      return;
    }

    if (method == "Runtime.evaluate") {
      this->handleRuntimeEvaluate(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.callFunctionOn") {
      this->handleRuntimeCallFunctionOn(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.getProperties") {
      this->handleRuntimeGetProperties(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.releaseObject") {
      this->handleRuntimeReleaseObject(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.releaseObjectGroup") {
      this->handleRuntimeReleaseObjectGroup(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.addBinding") {
      this->handleRuntimeAddBinding(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.removeBinding") {
      this->handleRuntimeRemoveBinding(client, id, params, sessionId);
      return;
    }

    if (method == "Runtime.queryObjects") {
      // Not supported (requires heap/object graph integration).
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method == "DOM.getDocument") {
      this->handleDOMGetDocument(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getOuterHTML") {
      this->handleDOMGetOuterHTML(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.setOuterHTML") {
      this->handleDOMSetOuterHTML(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getAttributes") {
      this->handleDOMGetAttributes(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.requestChildNodes") {
      this->handleDOMRequestChildNodes(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.setAttributeValue") {
      this->handleDOMSetAttributeValue(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.removeAttribute") {
      this->handleDOMRemoveAttribute(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.setNodeValue") {
      this->handleDOMSetNodeValue(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.querySelector") {
      this->handleDOMQuerySelector(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.querySelectorAll") {
      this->handleDOMQuerySelectorAll(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.describeNode") {
      this->handleDOMDescribeNode(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.resolveNode") {
      this->handleDOMResolveNode(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getContentQuads") {
      this->handleDOMGetContentQuads(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getNodeForLocation") {
      this->handleDOMGetNodeForLocation(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getBoxModel") {
      this->handleDOMGetBoxModel(client, id, params, sessionId);
      return;
    }

    if (method == "CSS.getComputedStyleForNode") {
      this->handleCSSGetComputedStyleForNode(client, id, params, sessionId);
      return;
    }

    if (method == "CSS.getInlineStylesForNode") {
      this->handleCSSGetInlineStylesForNode(client, id, params, sessionId);
      return;
    }

    if (method == "CSS.getMatchedStylesForNode") {
      this->handleCSSGetMatchedStylesForNode(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.scrollIntoViewIfNeeded") {
      this->handleDOMScrollIntoViewIfNeeded(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.focus") {
      this->handleDOMFocus(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.getFrameOwner") {
      this->handleDOMGetFrameOwner(client, id, params, sessionId);
      return;
    }

    if (method == "DOM.setFileInputFiles") {
      this->handleDOMSetFileInputFiles(client, id, params, sessionId);
      return;
    }

    if (method == "Input.dispatchMouseEvent") {
      this->handleInputDispatchMouseEvent(client, id, params, sessionId);
      return;
    }

    if (method == "Input.dispatchKeyEvent") {
      this->handleInputDispatchKeyEvent(client, id, params, sessionId);
      return;
    }

    if (method == "Input.insertText") {
      this->handleInputInsertText(client, id, params, sessionId);
      return;
    }

    if (method == "Emulation.setDeviceMetricsOverride") {
      this->handleEmulationSetDeviceMetricsOverride(client, id, params, sessionId);
      return;
    }

    if (method == "Emulation.clearDeviceMetricsOverride") {
      this->handleEmulationClearDeviceMetricsOverride(client, id, sessionId);
      return;
    }

    // No-op stubs for commonly-used automation surfaces. These are intended
    // to keep Puppeteer/Playwright flows working, even when the underlying
    // engine/runtime cannot provide Chrome/V8 parity.
    if (
      method == "Performance.getMetrics" ||
      method == "Memory.getDOMCounters" ||
      method == "Memory.getBrowserCounters" ||
      method == "Runtime.getHeapUsage"
    ) {
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (targetId.empty()) {
        if (method == "Performance.getMetrics") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", JSON::Array::Entries {}} }, sessionId);
        } else if (method == "Runtime.getHeapUsage") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"usedSize", 0}, {"totalSize", 0} }, sessionId);
        } else if (method == "Memory.getBrowserCounters") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0}, {"jsHeapSizeUsed", 0} }, sessionId);
        } else {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0} }, sessionId);
        }
        return;
      }

      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();
      if (!maybeWindow.has_value() || !app) {
        if (method == "Performance.getMetrics") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", JSON::Array::Entries {}} }, sessionId);
        } else if (method == "Runtime.getHeapUsage") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"usedSize", 0}, {"totalSize", 0} }, sessionId);
        } else if (method == "Memory.getBrowserCounters") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0}, {"jsHeapSizeUsed", 0} }, sessionId);
        } else {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0} }, sessionId);
        }
        return;
      }

      auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
      if (!window) {
        if (method == "Performance.getMetrics") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", JSON::Array::Entries {}} }, sessionId);
        } else if (method == "Runtime.getHeapUsage") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"usedSize", 0}, {"totalSize", 0} }, sessionId);
        } else if (method == "Memory.getBrowserCounters") {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0}, {"jsHeapSizeUsed", 0} }, sessionId);
        } else {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0} }, sessionId);
        }
        return;
      }

      std::string script;
      if (method == "Performance.getMetrics") {
        script = std::string(
          "(function(){"
          " try {"
          "  const metrics = [];"
          "  const t = (typeof performance !== 'undefined' && performance && typeof performance.now === 'function') ? (performance.now() / 1000) : 0;"
          "  metrics.push({ name: 'Timestamp', value: Number(t) || 0 });"
          "  let nodes = 0;"
          "  try { nodes = document && document.getElementsByTagName ? document.getElementsByTagName('*').length : 0; } catch (e) {}"
          "  metrics.push({ name: 'Documents', value: 1 });"
          "  metrics.push({ name: 'Nodes', value: Number(nodes) || 0 });"
          "  let used = 0, total = 0;"
          "  try {"
          "   if (performance && performance.memory) {"
          "    used = Number(performance.memory.usedJSHeapSize || 0) || 0;"
          "    total = Number(performance.memory.totalJSHeapSize || 0) || 0;"
          "   }"
          "  } catch (e) {}"
          "  metrics.push({ name: 'JSHeapUsedSize', value: used });"
          "  metrics.push({ name: 'JSHeapTotalSize', value: total });"
          "  return JSON.stringify({ metrics });"
          " } catch (e) {"
          "  return JSON.stringify({ metrics: [] });"
          " }"
          "})()"
        );
      } else if (method == "Memory.getBrowserCounters") {
        script = std::string(
          "(function(){"
          " try {"
          "  let nodes = 0;"
          "  try { nodes = document && document.getElementsByTagName ? document.getElementsByTagName('*').length : 0; } catch (e) {}"
          "  let used = 0;"
          "  try { used = (performance && performance.memory && performance.memory.usedJSHeapSize) ? Number(performance.memory.usedJSHeapSize) || 0 : 0; } catch (e) {}"
          "  return JSON.stringify({ documents: 1, nodes: Number(nodes) || 0, jsEventListeners: 0, jsHeapSizeUsed: used });"
          " } catch (e) {"
          "  return JSON.stringify({ documents: 0, nodes: 0, jsEventListeners: 0, jsHeapSizeUsed: 0 });"
          " }"
          "})()"
        );
      } else if (method == "Runtime.getHeapUsage") {
        script = std::string(
          "(function(){"
          " try {"
          "  let used = 0, total = 0;"
          "  try {"
          "   if (performance && performance.memory) {"
          "    used = Number(performance.memory.usedJSHeapSize || 0) || 0;"
          "    total = Number(performance.memory.totalJSHeapSize || 0) || 0;"
          "   }"
          "  } catch (e) {}"
          "  return JSON.stringify({ usedSize: used, totalSize: total });"
          " } catch (e) {"
          "  return JSON.stringify({ usedSize: 0, totalSize: 0 });"
          " }"
          "})()"
        );
      } else {
        // Memory.getDOMCounters
        script = std::string(
          "(function(){"
          " try {"
          "  let nodes = 0;"
          "  try { nodes = document && document.getElementsByTagName ? document.getElementsByTagName('*').length : 0; } catch (e) {}"
          "  return JSON.stringify({ documents: 1, nodes: Number(nodes) || 0, jsEventListeners: 0 });"
          " } catch (e) {"
          "  return JSON.stringify({ documents: 0, nodes: 0, jsEventListeners: 0 });"
          " }"
          "})()"
        );
      }

      const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
      const auto clientId = client->id;
      const auto methodCopy = std::string(method);

      window->eval(script, [this, stream, clientId, id, sessionId, methodCopy](const JSON::Any& value) mutable {
        this->loop.dispatch([this, stream, clientId, id, sessionId, methodCopy, value]() mutable {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) {
            return;
          }

          const auto parsedOpt = parseJSONResultString(value);
          if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
            const auto& obj = parsedOpt.value().as<JSON::Object>();

            if (methodCopy == "Performance.getMetrics") {
              const auto& metricsAny = obj.get("metrics");
              if (metricsAny.isArray()) {
                this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", metricsAny} }, sessionId);
              } else {
                this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", JSON::Array::Entries {}} }, sessionId);
              }
              return;
            }

            if (methodCopy == "Runtime.getHeapUsage") {
              const auto usedAny = obj.get("usedSize");
              const auto totalAny = obj.get("totalSize");
              const auto used = usedAny.isNumber() ? usedAny.as<JSON::Number>().value() : 0.0;
              const auto total = totalAny.isNumber() ? totalAny.as<JSON::Number>().value() : 0.0;
              this->sendCDPResult(client, id, JSON::Object::Entries { {"usedSize", JSON::Number(used)}, {"totalSize", JSON::Number(total)} }, sessionId);
              return;
            }

            const auto documentsAny = obj.get("documents");
            const auto nodesAny = obj.get("nodes");
            const auto listenersAny = obj.get("jsEventListeners");
            const auto docs = documentsAny.isNumber() ? documentsAny.as<JSON::Number>().value() : 0.0;
            const auto nodes = nodesAny.isNumber() ? nodesAny.as<JSON::Number>().value() : 0.0;
            const auto listeners = listenersAny.isNumber() ? listenersAny.as<JSON::Number>().value() : 0.0;

            if (methodCopy == "Memory.getBrowserCounters") {
              const auto heapAny = obj.get("jsHeapSizeUsed");
              const auto heap = heapAny.isNumber() ? heapAny.as<JSON::Number>().value() : 0.0;
              this->sendCDPResult(client, id, JSON::Object::Entries {
                {"documents", JSON::Number(docs)},
                {"nodes", JSON::Number(nodes)},
                {"jsEventListeners", JSON::Number(listeners)},
                {"jsHeapSizeUsed", JSON::Number(heap)}
              }, sessionId);
              return;
            }

            this->sendCDPResult(client, id, JSON::Object::Entries {
              {"documents", JSON::Number(docs)},
              {"nodes", JSON::Number(nodes)},
              {"jsEventListeners", JSON::Number(listeners)}
            }, sessionId);
            return;
          }

          if (methodCopy == "Performance.getMetrics") {
            this->sendCDPResult(client, id, JSON::Object::Entries { {"metrics", JSON::Array::Entries {}} }, sessionId);
          } else if (methodCopy == "Runtime.getHeapUsage") {
            this->sendCDPResult(client, id, JSON::Object::Entries { {"usedSize", 0}, {"totalSize", 0} }, sessionId);
          } else if (methodCopy == "Memory.getBrowserCounters") {
            this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0}, {"jsHeapSizeUsed", 0} }, sessionId);
          } else {
            this->sendCDPResult(client, id, JSON::Object::Entries { {"documents", 0}, {"nodes", 0}, {"jsEventListeners", 0} }, sessionId);
          }
        });
      });
      return;
    }

    if (method == "Accessibility.getFullAXTree" || method == "Accessibility.queryAXTree") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"nodes", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "CSS.getStyleSheetText") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"text", ""} }, sessionId);
      return;
    }

    if (method == "CSS.startRuleUsageTracking") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "CSS.stopRuleUsageTracking") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"ruleUsage", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "Debugger.getScriptSource") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"scriptSource", ""} }, sessionId);
      return;
    }

    if (method == "Debugger.setSkipAllPauses") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Profiler.startPreciseCoverage") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Profiler.takePreciseCoverage" || method == "Profiler.stopPreciseCoverage") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "HeapProfiler.collectGarbage" || method == "HeapProfiler.startSampling") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "HeapProfiler.getSamplingProfile" || method == "HeapProfiler.stopSampling") {
      this->sendCDPResult(client, id, JSON::Object::Entries {
        {"profile", JSON::Object::Entries {
          {"head", JSON::Object::Entries {
            {"callFrame", JSON::Object::Entries {
              {"functionName", ""},
              {"scriptId", "0"},
              {"url", ""},
              {"lineNumber", JSON::Number(0)},
              {"columnNumber", JSON::Number(0)}
            }},
            {"selfSize", JSON::Number(0)},
            {"id", JSON::Number(0)},
            {"children", JSON::Array::Entries {}}
          }},
          {"samples", JSON::Array::Entries {}}
        }}
      }, sessionId);
      return;
    }

    if (method == "HeapProfiler.takeHeapSnapshot") {
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method == "Tracing.start" || method == "Tracing.end") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method.rfind("Overlay.", 0) == 0) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    // Some tooling sends a domain-less initialize during startup.
    if (method == "initialize") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Fetch.enable" || method == "Fetch.disable") {
      String targetId;

      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }

      if (!targetId.empty()) {
        const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
        if (maybeWindow.has_value()) {
          const int windowIndex = maybeWindow.value();

          std::vector<FetchPattern> patterns;
          if (method == "Fetch.enable" && params.isObject()) {
            const auto patternsAny = params.as<JSON::Object>().get("patterns");
            if (patternsAny.isArray()) {
              for (const auto& pAny : patternsAny.as<JSON::Array>()) {
                if (!pAny.isObject()) {
                  continue;
                }

                const auto& pObj = pAny.as<JSON::Object>();

                const auto stageAny = pObj.get("requestStage");
                if (stageAny.isString() && stageAny.as<JSON::String>().value() != "Request") {
                  continue;
                }

                FetchPattern p;

                const auto urlPatternAny = pObj.get("urlPattern");
                if (urlPatternAny.isString()) {
                  p.urlPattern = urlPatternAny.as<JSON::String>().value();
                }

                const auto rtAny = pObj.get("resourceType");
                if (rtAny.isString()) {
                  p.resourceType = rtAny.as<JSON::String>().value();
                }

                patterns.push_back(p);
              }
            }
          }

          std::vector<PendingFetchRequest> toResolve;
          {
            Lock lock(this->mutex);

            if (method == "Fetch.enable") {
              this->windowFetchEnabled.insert_or_assign(windowIndex, true);
              this->windowFetchPatterns.insert_or_assign(windowIndex, patterns);
            } else {
              this->windowFetchEnabled.insert_or_assign(windowIndex, false);
              this->windowFetchPatterns.erase(windowIndex);

              for (auto it = this->pendingFetchRequests.begin(); it != this->pendingFetchRequests.end();) {
                if (it->second.windowIndex == windowIndex) {
                  toResolve.push_back(it->second);
                  it = this->pendingFetchRequests.erase(it);
                } else {
                  ++it;
                }
              }
            }
          }

          // Disabling should not permanently stall any currently paused requests.
          for (auto& pending : toResolve) {
            if (pending.resolve) {
              pending.resolve(JSON::Any(JSON::Object::Entries {{"action", "continue"}}));
            }
          }

          // Enable/disable in-page fetch() interception hooks via injected
          // instrumentation. This supports Fetch.* for in-page fetch() on
          // engines without native request interception.
          if (auto app = App::sharedApplication()) {
            if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
              JSON::Array::Entries patternsOut;
              patternsOut.reserve(patterns.size());
              for (const auto& p : patterns) {
                JSON::Object::Entries o;
                if (p.urlPattern.has_value() && p.urlPattern.value().size() > 0) {
                  o.insert_or_assign("urlPattern", p.urlPattern.value());
                }
                if (p.resourceType.has_value() && p.resourceType.value().size() > 0) {
                  o.insert_or_assign("resourceType", p.resourceType.value());
                }
                patternsOut.push_back(o);
              }

              const auto patternsJson = JSON::Any(patternsOut).str();
              const auto script = std::string(
                "(function(){"
              ) + ORO_CDP_BOOTSTRAP + std::string(
                " try {"
                "  const __oro = globalThis.__oro_cdp;"
                "  if (__oro && __oro.net) {"
              ) + (method == "Fetch.enable"
                ? std::string("    if (typeof __oro.net.setFetchPatterns === 'function') { __oro.net.setFetchPatterns(") + patternsJson + "); }"
                : std::string("    if (typeof __oro.net.clearFetchPatterns === 'function') { __oro.net.clearFetchPatterns(); }")
              ) + std::string(
                "  }"
                " } catch (e) {}"
                "})()"
              );
              window->eval(script);
            }
          }
        }
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

	    if (
	      method == "Fetch.continueRequest" ||
	      method == "Fetch.continueResponse" ||
	      method == "Fetch.fulfillRequest" ||
	      method == "Fetch.failRequest" ||
	      method == "Fetch.continueWithAuth"
	    ) {
      const auto requestId = jsonStringOrEmpty(params, "requestId");
      if (requestId.empty()) {
        this->sendCDPError(client, id, -32602, "Missing requestId", sessionId);
        return;
      }

      PendingFetchRequest pending;
      bool found = false;
      {
        Lock lock(this->mutex);
        const auto key = std::string(requestId);
        if (this->pendingFetchRequests.contains(key)) {
          pending = this->pendingFetchRequests.at(key);
          this->pendingFetchRequests.erase(key);
          found = true;
        }
      }

      if (!found) {
        this->sendCDPError(client, id, -32000, "Request not found", sessionId);
        return;
      }

      auto headerArrayToObject = [](const JSON::Any& headersAny) -> JSON::Object::Entries {
        JSON::Object::Entries out;
        if (headersAny.isObject()) {
          for (const auto& kv : headersAny.as<JSON::Object>().value()) {
            if (kv.second.isString()) {
              out.insert_or_assign(kv.first, kv.second.as<JSON::String>().value());
            }
          }
          return out;
        }

        if (!headersAny.isArray()) {
          return out;
        }

        for (const auto& hAny : headersAny.as<JSON::Array>()) {
          if (!hAny.isObject()) continue;
          const auto& h = hAny.as<JSON::Object>();
          const auto nameAny = h.get("name");
          const auto valueAny = h.get("value");
          if (!nameAny.isString()) continue;
          const auto name = nameAny.as<JSON::String>().value();
          const auto value = valueAny.isString() ? valueAny.as<JSON::String>().value() : "";
          if (!name.empty()) {
            out.insert_or_assign(name, value);
          }
        }

        return out;
      };

      JSON::Any decision = JSON::Any(JSON::Object::Entries {{"action", "continue"}});

      if (method == "Fetch.failRequest") {
        const auto errorReason = jsonStringOrEmpty(params, "errorReason");
        decision = JSON::Any(JSON::Object::Entries {
          {"action", "fail"},
          {"errorReason", errorReason.size() ? errorReason : String("Failed")}
        });
      } else if (method == "Fetch.fulfillRequest") {
        const int responseCode = jsonIntOr(params, "responseCode", 200);
        const auto body = jsonStringOrEmpty(params, "body");
        const auto responseHeadersAny = params.isObject() ? params.as<JSON::Object>().get("responseHeaders") : JSON::Any();
        const auto responseHeaders = headerArrayToObject(responseHeadersAny);

        auto entries = JSON::Object::Entries {
          {"action", "fulfill"},
          {"responseCode", JSON::Number(static_cast<double>(responseCode))},
          {"responseHeaders", responseHeaders}
        };

        if (!body.empty()) {
          entries.insert_or_assign("body", body);
          entries.insert_or_assign("bodyBase64Encoded", JSON::Boolean(true));
        }

        decision = JSON::Any(entries);
      } else if (method == "Fetch.continueRequest") {
        auto entries = JSON::Object::Entries {{"action", "continue"}};

        const auto urlAny = params.isObject() ? params.as<JSON::Object>().get("url") : JSON::Any();
        if (urlAny.isString()) {
          entries.insert_or_assign("url", urlAny.as<JSON::String>().value());
        }

        const auto methodAny = params.isObject() ? params.as<JSON::Object>().get("method") : JSON::Any();
        if (methodAny.isString()) {
          entries.insert_or_assign("method", methodAny.as<JSON::String>().value());
        }

        const auto headersAny = params.isObject() ? params.as<JSON::Object>().get("headers") : JSON::Any();
        const auto headers = headerArrayToObject(headersAny);
        if (!headers.empty()) {
          entries.insert_or_assign("headers", headers);
        }

        const auto postDataAny = params.isObject() ? params.as<JSON::Object>().get("postData") : JSON::Any();
        if (postDataAny.isString()) {
          auto postData = postDataAny.as<JSON::String>().value();
          if (postData.size() > MAX_NETWORK_POSTDATA_RESULT_BYTES) {
            postData = "";
          }
          if (!postData.empty()) {
            entries.insert_or_assign("postData", postData);
          }
        }

        decision = JSON::Any(entries);
      } else if (method == "Fetch.continueWithAuth") {
        // Auth challenges are not modeled today; continue the request.
        decision = JSON::Any(JSON::Object::Entries {{"action", "continue"}});
      }

      if (pending.resolve) {
        pending.resolve(decision);
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Extensions.loadUnpacked" || method == "Extensions.uninstall") {
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (
      method == "DeviceAccess.enable" ||
      method == "DeviceAccess.selectPrompt" ||
      method == "DeviceAccess.cancelPrompt" ||
      method == "Autofill.trigger"
    ) {
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    // Common no-op stubs for automation clients.
    if (method.size() > 7 && method.rfind(".enable") == method.size() - 7) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method.size() > 8 && method.rfind(".disable") == method.size() - 8) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (
      method == "Network.setCacheDisabled" ||
      method == "Network.setBypassServiceWorker" ||
      method == "Network.setExtraHTTPHeaders" ||
      method == "Network.setUserAgentOverride" ||
      method == "Network.emulateNetworkConditions" ||
      method == "Emulation.setDefaultBackgroundColorOverride" ||
      method == "Emulation.setCPUThrottlingRate" ||
      method == "Emulation.setIdleOverride" ||
      method == "Emulation.clearIdleOverride" ||
      method == "Emulation.setGeolocationOverride" ||
      method == "Emulation.setScriptExecutionDisabled" ||
      method == "Emulation.setEmulatedMedia" ||
      method == "Emulation.setEmulatedVisionDeficiency" ||
      method == "Emulation.setUserAgentOverride" ||
      method == "Emulation.setTimezoneOverride" ||
      method == "Emulation.setLocaleOverride" ||
      method == "Emulation.setTouchEmulationEnabled" ||
      method == "Emulation.setFocusEmulationEnabled" ||
      method == "Input.dispatchTouchEvent" ||
      method == "Input.dispatchDragEvent" ||
      method == "Input.setInterceptDrags" ||
      method == "Page.setLifecycleEventsEnabled" ||
      method == "Page.setBypassCSP" ||
      method == "Page.setFontFamilies" ||
      method == "Page.setInterceptFileChooserDialog"
    ) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    // Additional compatibility stubs used by DevTools/automation tooling.
    if (method == "Schema.getDomains") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"domains", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "Network.clearBrowserCache" || method == "Network.clearBrowserCookies") {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "Page.getAppManifest") {
      this->sendCDPResult(client, id, JSON::Object::Entries {
        {"url", ""},
        {"errors", JSON::Array::Entries {}}
      }, sessionId);
      return;
    }

    if (method == "Page.getInstallabilityErrors") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"installabilityErrors", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "Page.captureSnapshot") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"data", ""} }, sessionId);
      return;
    }

    if (method == "DOMDebugger.getEventListeners") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"listeners", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method.rfind("DOMDebugger.", 0) == 0) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method == "DOMSnapshot.captureSnapshot" || method == "DOMSnapshot.getSnapshot") {
      this->sendCDPResult(client, id, JSON::Object::Entries {
        {"documents", JSON::Array::Entries {}},
        {"strings", JSON::Array::Entries {}}
      }, sessionId);
      return;
    }

    if (method == "CSS.getPlatformFontsForNode") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"fonts", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "CSS.getMediaQueries") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"medias", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method == "CSS.collectClassNames") {
      this->sendCDPResult(client, id, JSON::Object::Entries { {"classNames", JSON::Array::Entries {}} }, sessionId);
      return;
    }

    if (method.rfind("Debugger.", 0) == 0) {
      if (method == "Debugger.getPossibleBreakpoints") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"locations", JSON::Array::Entries {}} }, sessionId);
        return;
      }

      if (method == "Debugger.searchInContent") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Array::Entries {}} }, sessionId);
        return;
      }

      if (method == "Debugger.setBreakpointByUrl" || method == "Debugger.setBreakpoint") {
        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"breakpointId", ""},
          {"locations", JSON::Array::Entries {}}
        }, sessionId);
        return;
      }

      if (
        method == "Debugger.removeBreakpoint" ||
        method == "Debugger.pause" ||
        method == "Debugger.resume" ||
        method == "Debugger.stepOver" ||
        method == "Debugger.stepInto" ||
        method == "Debugger.stepOut" ||
        method == "Debugger.setAsyncCallStackDepth" ||
        method == "Debugger.setBlackboxPatterns" ||
        method == "Debugger.setPauseOnExceptions" ||
        method == "Debugger.setBreakpointsActive" ||
        method == "Debugger.setInstrumentationBreakpoint"
      ) {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      if (method == "Debugger.setScriptSource") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"callFrames", JSON::Array::Entries {}} }, sessionId);
        return;
      }

      if (method == "Debugger.getStackTrace") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"stackTrace", JSON::Object::Entries { {"callFrames", JSON::Array::Entries {}} }} }, sessionId);
        return;
      }

      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method.rfind("Profiler.", 0) == 0) {
      if (method == "Profiler.setSamplingInterval" || method == "Profiler.start") {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      if (method == "Profiler.stop") {
        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"profile", JSON::Object::Entries {
            {"nodes", JSON::Array::Entries {}},
            {"startTime", JSON::Number(0)},
            {"endTime", JSON::Number(0)},
            {"samples", JSON::Array::Entries {}},
            {"timeDeltas", JSON::Array::Entries {}}
          }}
        }, sessionId);
        return;
      }

      if (method == "Profiler.getBestEffortCoverage") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Array::Entries {}} }, sessionId);
        return;
      }

      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (method.rfind("Coverage.", 0) == 0) {
      if (
        method == "Coverage.startJSCoverage" ||
        method == "Coverage.stopJSCoverage" ||
        method == "Coverage.startCSSCoverage" ||
        method == "Coverage.stopCSSCoverage"
      ) {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      if (method == "Coverage.takePreciseCoverage") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"result", JSON::Array::Entries {}} }, sessionId);
        return;
      }

      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method.rfind("HeapProfiler.", 0) == 0) {
      if (method == "HeapProfiler.getObjectByHeapObjectId") {
        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"result", JSON::Object::Entries { {"type", "undefined"} }}
        }, sessionId);
        return;
      }

      if (method == "HeapProfiler.getHeapObjectId") {
        this->sendCDPResult(client, id, JSON::Object::Entries { {"heapSnapshotObjectId", ""} }, sessionId);
        return;
      }

      if (
        method == "HeapProfiler.addInspectedHeapObject" ||
        method == "HeapProfiler.startTrackingHeapObjects" ||
        method == "HeapProfiler.stopTrackingHeapObjects"
      ) {
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
        return;
      }

      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    if (method.rfind("Tracing.", 0) == 0) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (
      method.rfind("ServiceWorker.", 0) == 0 ||
      method.rfind("BackgroundService.", 0) == 0 ||
      method.rfind("BackgroundFetch.", 0) == 0 ||
      method.rfind("Audits.", 0) == 0 ||
      method.rfind("Audits2.", 0) == 0 ||
      method.rfind("WebAuthn.", 0) == 0 ||
      method.rfind("WebAudio.", 0) == 0 ||
      method.rfind("Media.", 0) == 0 ||
      method.rfind("Cast.", 0) == 0 ||
      method.rfind("DeviceOrientation.", 0) == 0
    ) {
      this->sendCDPError(client, id, -32000, "Not supported", sessionId);
      return;
    }

    // Unknown methods: prefer a stable Not supported error for known CDP
    // domains, and reserve Method not found for truly unknown domains.
    const auto dot = method.find('.');
    if (dot != std::string::npos) {
      const auto domain = method.substr(0, dot);
      if (isKnownCDPDomain(domain)) {
        this->sendCDPError(client, id, -32000, "Not supported", sessionId);
        return;
      }
    }

    // Unknown.
    bool shouldLog = false;
    bool shouldLogSaturation = false;
    {
      Lock lock(this->mutex);
      if (!this->loggedUnhandledMethodsSaturated) {
        if (this->loggedUnhandledMethods.size() < MAX_UNHANDLED_METHODS_LOGGED) {
          shouldLog = this->loggedUnhandledMethods.insert(std::string(method)).second;
          if (this->loggedUnhandledMethods.size() >= MAX_UNHANDLED_METHODS_LOGGED) {
            this->loggedUnhandledMethodsSaturated = true;
            shouldLogSaturation = true;
          }
        } else {
          this->loggedUnhandledMethodsSaturated = true;
          shouldLogSaturation = true;
        }
      }
    }
    if (shouldLogSaturation) {
      std::fprintf(
        stderr,
        "CDP: reached %zu unique unhandled methods; suppressing additional unknown-method logs\n",
        MAX_UNHANDLED_METHODS_LOGGED
      );
      std::fflush(stderr);
    }
    if (shouldLog) {
      std::fprintf(stderr, "CDP: unhandled method '%s'\n", method.c_str());
      std::fflush(stderr);
    }
    this->sendCDPError(client, id, -32601, "Method not found", sessionId);
  }

  void CDP::handleTargetSetDiscoverTargets (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const bool discover = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("discover"), false)
      : false;
    client->discoverTargets = discover;

    client->discoverFilter = params.isObject()
      ? parseTargetFilter(params.as<JSON::Object>().get("filter"))
      : Vector<AutoAttachOptions::TargetFilterRule> {};

    if (discover) {
      // Emit Target.targetCreated for current targets.
      // Ordering matters: events should arrive before the command response.
      for (const auto& t : this->snapshotTargets()) {
        if (!targetMatchesFilter(client->discoverFilter, t.type)) {
          continue;
        }

        auto info = JSON::Object::Entries {
          {"targetId", t.targetId},
          {"type", t.type},
          {"title", t.title},
          {"url", t.url},
          {"attached", JSON::Boolean(this->clientAttachedToTarget(client, t.targetId))}
        };

        if (!t.browserContextId.empty() && t.type == "page") {
          info.insert_or_assign("browserContextId", t.browserContextId);
        }

        this->sendCDPEvent(client, "Target.targetCreated", JSON::Object::Entries { {"targetInfo", info} }, sessionId);
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleTargetGetTargets (Client* client, int id, const String& sessionId) {
    JSON::Array::Entries infos;
    for (const auto& t : this->snapshotTargets()) {
      auto info = JSON::Object::Entries {
        {"targetId", t.targetId},
        {"type", t.type},
        {"title", t.title},
        {"url", t.url},
        {"attached", JSON::Boolean(this->clientAttachedToTarget(client, t.targetId))}
      };
      if (!t.browserContextId.empty() && t.type == "page") {
        info.insert_or_assign("browserContextId", t.browserContextId);
      }
      infos.push_back(info);
    }
    this->sendCDPResult(client, id, JSON::Object::Entries { {"targetInfos", infos} }, sessionId);
  }

  void CDP::handleTargetSetAutoAttach (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    AutoAttachOptions options;
    options.autoAttach = params.isObject() ? parseBool(params.as<JSON::Object>().get("autoAttach"), false) : false;
    options.flatten = params.isObject() ? parseBool(params.as<JSON::Object>().get("flatten"), true) : true;
    options.waitForDebuggerOnStart = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("waitForDebuggerOnStart"), false)
      : false;

    options.filter = params.isObject()
      ? parseTargetFilter(params.as<JSON::Object>().get("filter"))
      : Vector<AutoAttachOptions::TargetFilterRule> {};

    client->autoAttachBySession[std::string(sessionId)] = options;

    // Session-level auto-attach is for child targets (workers, etc.) of the
    // session's target. We currently don't model those, but Puppeteer expects
    // this command to succeed on page sessions.
    if (!sessionId.empty() || client->kind != Client::Kind::BrowserWS) {
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    if (options.autoAttach) {
      // Attach to existing targets matching the filter.
      // Ordering matters: events should arrive before the command response.
      auto app = App::sharedApplication();

      for (const auto& t : this->snapshotTargets()) {
        if (t.type == "browser") continue;
        if (!targetMatchesFilter(options.filter, t.type)) continue;

        bool alreadyAttached = false;
        for (const auto& kv : client->sessions) {
          if (kv.second.targetId == t.targetId) {
            alreadyAttached = true;
            break;
          }
        }
        if (alreadyAttached) {
          continue;
        }

        const auto sid = uuid::v7();
        client->sessions.emplace(std::string(sid), SessionInfo {
          .sessionId = sid,
          .targetId = t.targetId,
          .flatten = options.flatten,
          .parentSessionId = sessionId
        });

        auto info = JSON::Object::Entries {
          {"targetId", t.targetId},
          {"type", t.type},
          {"title", t.title},
          {"url", t.url},
          {"attached", true}
        };
        if (!t.browserContextId.empty() && t.type == "page") {
          info.insert_or_assign("browserContextId", t.browserContextId);
        }

        this->sendCDPEvent(client, "Target.attachedToTarget", JSON::Object::Entries {
          {"sessionId", sid},
          {"targetInfo", info},
          {"waitingForDebugger", JSON::Boolean(options.waitForDebuggerOnStart)}
        }, sessionId);

        if (app) {
          const auto maybeWindow = this->resolveWindowIndexForTarget(t.targetId);
          if (maybeWindow.has_value()) {
            auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
            if (window) {
              const int contextId = [&]() {
                Lock lock(this->mutex);
                if (this->windowExecutionContexts.contains(maybeWindow.value())) {
                  return this->windowExecutionContexts.at(maybeWindow.value());
                }
                return 0;
              }();

              if (contextId > 0) {
                this->sendCDPEvent(client, "Runtime.executionContextCreated", JSON::Object::Entries {
                  {"context", JSON::Object::Entries {
                    {"id", JSON::Number(contextId)},
                    {"origin", window->bridge->navigator.location.origin},
                    {"name", ""},
                    {"auxData", JSON::Object::Entries {
                      {"isDefault", JSON::Boolean(true)},
                      {"type", "default"},
                      {"frameId", t.targetId}
                    }}
                  }}
                }, sid);
              }

              this->sendCDPEvent(client, "Page.frameNavigated", JSON::Object::Entries {
                {"frame", JSON::Object::Entries {
                  {"id", t.targetId},
                  {"loaderId", t.targetId},
                  {"url", window->bridge->navigator.location.str()},
                  {"name", ""},
                  {"mimeType", "text/html"},
                  {"securityOrigin", window->bridge->navigator.location.origin}
                }}
              }, sid);
            }
          }
        }
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleTargetAttachToTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto targetId = jsonStringOrEmpty(params, "targetId");
    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing targetId", sessionId);
    }

    const bool flatten = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("flatten"), true)
      : true;
    const auto sid = uuid::v7();

    // Ensure target exists before attaching.
    JSON::Object::Entries info;
    bool found = false;
    for (const auto& t : this->snapshotTargets()) {
      if (t.targetId != targetId) continue;
      found = true;
      info = JSON::Object::Entries {
        {"targetId", t.targetId},
        {"type", t.type},
        {"title", t.title},
        {"url", t.url},
        {"attached", true}
      };
      if (!t.browserContextId.empty() && t.type == "page") {
        info.insert_or_assign("browserContextId", t.browserContextId);
      }
      break;
    }

    if (!found) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    client->sessions.emplace(std::string(sid), SessionInfo {
      .sessionId = sid,
      .targetId = targetId,
      .flatten = flatten,
      .parentSessionId = sessionId
    });

    // Ordering matters for Puppeteer: Target.attachedToTarget must arrive before
    // the Target.attachToTarget response resolves.
    this->sendCDPEvent(client, "Target.attachedToTarget", JSON::Object::Entries {
      {"sessionId", sid},
      {"targetInfo", info},
      {"waitingForDebugger", false}
    }, sessionId);

    this->sendCDPResult(client, id, JSON::Object::Entries { {"sessionId", sid} }, sessionId);
  }

  void CDP::handleTargetDetachFromTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto sid = jsonStringOrEmpty(params, "sessionId");
    String targetId;
    String parentSessionId;
    if (!sid.empty()) {
      if (client->sessions.contains(sid)) {
        targetId = client->sessions.at(sid).targetId;
        parentSessionId = client->sessions.at(sid).parentSessionId;
      }
      client->sessions.erase(sid);
      client->autoAttachBySession.erase(sid);
    }
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);

    if (!sid.empty()) {
      auto ev = JSON::Object::Entries { {"sessionId", sid} };
      if (!targetId.empty()) {
        ev.insert_or_assign("targetId", targetId);
      }
      this->sendCDPEvent(client, "Target.detachedFromTarget", ev, parentSessionId.empty() ? sessionId : parentSessionId);
    }
  }

  void CDP::handleTargetSendMessageToTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto sid = jsonStringOrEmpty(params, "sessionId");
    if (sid.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing sessionId", sessionId);
    }

    const auto message = jsonStringOrEmpty(params, "message");
    if (message.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing message", sessionId);
    }

    if (sid.empty()) {
      return this->sendCDPError(client, id, -32602, "Invalid sessionId", sessionId);
    }

    if (!client->sessions.contains(sid)) {
      return this->sendCDPError(client, id, -32000, "Session not found", sessionId);
    }

    JSON::Any inner;
    try {
      inner = JSON::parse(message);
    } catch (...) {
      return this->sendCDPError(client, id, -32602, "Invalid message", sessionId);
    }

    if (!inner.isObject()) {
      return this->sendCDPError(client, id, -32602, "Invalid message", sessionId);
    }

    // The outer request returns immediately; responses/events for the inner
    // command are delivered via Target.receivedMessageFromTarget.
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);

    auto innerEntries = inner.as<JSON::Object>().value();
    innerEntries.insert_or_assign("sessionId", sid);
    this->handleCDPCommand(client, innerEntries);
  }

  void CDP::handleTargetGetTargetInfo (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    String targetId = jsonStringOrEmpty(params, "targetId");

    // CDP allows targetId to be omitted, in which case the "current" target is
    // implied by the session. Playwright probes Target.getTargetInfo without
    // parameters during connectOverCDP().
    if (targetId.empty()) {
      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      } else {
        targetId = this->browserId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    for (const auto& t : this->snapshotTargets()) {
      if (t.targetId != targetId) continue;

      auto info = JSON::Object::Entries {
        {"targetId", t.targetId},
        {"type", t.type},
        {"title", t.title},
        {"url", t.url},
        {"attached", JSON::Boolean(this->clientAttachedToTarget(client, t.targetId))}
      };

      if (!t.browserContextId.empty() && t.type == "page") {
        info.insert_or_assign("browserContextId", t.browserContextId);
      }

      this->sendCDPResult(client, id, JSON::Object::Entries { {"targetInfo", info} }, sessionId);
      return;
    }

    this->sendCDPError(client, id, -32000, "Target not found", sessionId);
  }

  void CDP::handleTargetCreateTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    auto url = jsonStringOrEmpty(params, "url");
    if (url.empty()) {
      url = "about:blank";
    }

    const auto browserContextId = jsonStringOrEmpty(params, "browserContextId");
    if (!browserContextId.empty()) {
      bool ok = false;
      {
        Lock lock(this->mutex);
        ok = this->browserContextIds.contains(std::string(browserContextId));
      }
      if (!ok) {
        return this->sendCDPError(client, id, -32000, "Browser context not found", sessionId);
      }
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    app->dispatch([=, this]() {
      const int windowIndex = app->runtime.windowManager.getRandomWindowIndex(false);
      if (windowIndex < 0) {
        this->loop.dispatch([=, this]() {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) return;
          this->sendCDPError(client, id, -32000, "No available window slots", sessionId);
        });
        return;
      }

      if (!browserContextId.empty()) {
        Lock lock(this->mutex);
        this->windowBrowserContextId[windowIndex] = std::string(browserContextId);
      }

      window::Window::Options options;
      options.index = windowIndex;
      options.headless = app->runtime.userConfig["build_headless"] == "true";
      options.debug = true;

      auto createdWindow = app->runtime.windowManager.createWindow(options);
      if (!createdWindow) {
        this->loop.dispatch([=, this]() {
          Client* client = nullptr;
          {
            Lock lock(this->mutex);
            if (this->clients.contains(stream)) {
              auto candidate = this->clients.at(stream);
              if (candidate && candidate->id == clientId) {
                client = candidate;
              }
            }
          }

          if (!client || client->closing || client->closed) return;
          this->sendCDPError(client, id, -32000, "Failed to create window", sessionId);
        });
        return;
      }

      if (!url.empty()) {
        createdWindow->navigate(url);
      }

    #if !ORO_RUNTIME_PLATFORM_ANDROID
      createdWindow->show();
    #endif

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto target = this->getOrCreateTargetForWindow(windowIndex);
        if (target.targetId.empty()) {
          return this->sendCDPError(client, id, -32000, "Failed to create target", sessionId);
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"targetId", target.targetId} }, sessionId);
      });
    });
  }

  void CDP::handleTargetCloseTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto targetId = jsonStringOrEmpty(params, "targetId");
    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing targetId", sessionId);
    }
    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPResult(client, id, JSON::Object::Entries { {"success", false} }, sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    app->dispatch([=, this]() {
      app->runtime.windowManager.destroyWindow(maybeWindow.value());
      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;
        this->sendCDPResult(client, id, JSON::Object::Entries { {"success", true} }, sessionId);
      });
    });
  }

  void CDP::handleTargetActivateTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto targetId = jsonStringOrEmpty(params, "targetId");
    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing targetId", sessionId);
    }
    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      const bool ok = [&]() {
        auto window = app->runtime.windowManager.getWindow(windowIndex);
        if (!window) {
          return false;
        }
        window->focus();
        return true;
      }();

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handlePageNavigate (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto url = jsonStringOrEmpty(params, "url");
    if (url.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing url", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key) && !client->sessions.at(key).targetId.empty()) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    // Emit a synthetic navigation request so automation clients
    // can observe navigation via Network.*.
    const auto requestId = uuid::v7();
    std::optional<std::string> previousPendingRequestId;
    {
      Lock lock(this->mutex);
      if (this->windowPendingNavigationRequestId.contains(maybeWindow.value())) {
        previousPendingRequestId = this->windowPendingNavigationRequestId.at(maybeWindow.value());
      }
      this->windowPendingNavigationRequestId[maybeWindow.value()] = requestId;
    }

    if (previousPendingRequestId.has_value()) {
      const auto prev = String(previousPendingRequestId.value());
      const double ts = monotonicSeconds();
      broadcastToAttachedTarget(targetId, [this, prev, targetId, ts](auto client, const auto& sid) {
        this->sendCDPEvent(client, "Network.loadingFailed", JSON::Object::Entries {
          {"requestId", prev},
          {"loaderId", targetId},
          {"timestamp", ts},
          {"type", "Document"},
          {"errorText", "Cancelled"},
          {"canceled", true}
        }, sid);
      });
    }

    const auto beforeURL = window->bridge->navigator.location.str();
    const double requestTs = monotonicSeconds();
    const double requestWallTime = wallTimeSeconds();
    broadcastToAttachedTarget(targetId, [this, requestId, beforeURL, url, targetId, requestTs, requestWallTime](auto client, const auto& sid) {
      this->sendCDPEvent(client, "Network.requestWillBeSent", JSON::Object::Entries {
        {"requestId", requestId},
        {"loaderId", targetId},
        {"documentURL", beforeURL},
        {"request", JSON::Object::Entries {
          {"url", url},
          {"method", "GET"},
          {"headers", JSON::Object::Entries {}}
        }},
        {"timestamp", requestTs},
        {"wallTime", requestWallTime},
        {"initiator", JSON::Object::Entries { {"type", "other"} }},
        {"type", "Document"},
        {"frameId", targetId},
        {"hasUserGesture", false}
      }, sid);
    });

    window->navigate(url);

    // Minimal response: frameId is synthetic.
    this->sendCDPResult(client, id, JSON::Object::Entries {
      {"frameId", targetId},
      {"loaderId", targetId}
    }, sessionId);
  }

  void CDP::handlePageSetDocumentContent (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    if (!params.isObject()) {
      return this->sendCDPError(client, id, -32602, "Missing frameId/html", sessionId);
    }

    String html;
    const auto& obj = params.as<JSON::Object>();
    if (!obj.has("html") || !obj.get("html").isString()) {
      return this->sendCDPError(client, id, -32602, "Missing html", sessionId);
    }

    html = obj.get("html").as<JSON::String>().value();
    if (html.size() > MAX_PAGE_SET_DOCUMENT_CONTENT_BYTES) {
      return this->sendCDPError(client, id, -32000, "Content too large", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key) && !client->sessions.at(key).targetId.empty()) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto htmlLiteral = JSON::String(html).str();
    const auto script = std::string(
      "(function(){"
      " try {"
      "  const html = " + htmlLiteral + ";"
      "  document.open();"
      "  document.write(html);"
      "  document.close();"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handlePageGetFrameTree (Client* client, int id, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto frameIdLiteral = JSON::String(targetId).str();
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const rootId = " + frameIdLiteral + ";"
      "  const maxFrames = 128;"
      "  const mkFrame = (id, parentId, url, name, mimeType) => {"
      "    let origin = '';"
      "    try { origin = url ? (new URL(String(url), String((globalThis.location && globalThis.location.href) || 'about:blank'))).origin : ''; } catch (e) { origin = ''; }"
      "    const frame = {"
      "      id: String(id || ''),"
      "      loaderId: String(id || ''),"
      "      url: String(url || ''),"
      "      name: String(name || ''),"
      "      mimeType: String(mimeType || 'text/html'),"
      "      securityOrigin: String(origin || '')"
      "    };"
      "    if (parentId) frame.parentId = String(parentId);"
      "    return frame;"
      "  };"
      "  const walk = (doc, frameId, parentId, nameHint) => {"
      "    let url = '';"
      "    let mimeType = 'text/html';"
      "    try { url = String((doc && doc.location && doc.location.href) || (globalThis.location && globalThis.location.href) || ''); } catch (e) {}"
      "    try { mimeType = String((doc && doc.contentType) || (globalThis.document && globalThis.document.contentType) || 'text/html'); } catch (e) {}"
      "    const tree = { frame: mkFrame(frameId, parentId, url, String(nameHint || ''), mimeType), childFrames: [] };"
      "    try {"
      "      const els = doc && doc.querySelectorAll ? doc.querySelectorAll('iframe,frame') : [];"
      "      for (let i = 0; i < els.length && tree.childFrames.length < maxFrames; i++) {"
      "        const el = els[i];"
      "        const nid = __oro && __oro.getOrCreateNodeId ? __oro.getOrCreateNodeId(el) : (i + 1);"
      "        const cid = String(frameId) + ':frame:' + String(nid);"
      "        let curl = '';"
      "        try { curl = String((el.contentWindow && el.contentWindow.location && el.contentWindow.location.href) || ''); } catch (e) {}"
      "        if (!curl) { try { curl = String(el.src || (el.getAttribute && el.getAttribute('src')) || ''); } catch (e) {} }"
      "        let cname = '';"
      "        try { cname = String(el.name || el.id || ''); } catch (e) {}"
      "        try {"
      "          const cd = el.contentDocument;"
      "          if (cd && cd.location && doc && doc.location && cd.location.origin === doc.location.origin) {"
      "            tree.childFrames.push(walk(cd, cid, frameId, cname));"
      "            continue;"
      "          }"
      "        } catch (e) {}"
      "        tree.childFrames.push({ frame: mkFrame(cid, frameId, curl, cname, 'text/html'), childFrames: [] });"
      "      }"
      "    } catch (e) {}"
      "    return tree;"
      "  };"
      "  const frameTree = walk(globalThis.document, rootId, null, '');"
      "  return JSON.stringify({ ok: true, frameTree });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, frameTree: null });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId, targetId](const JSON::Any& value) mutable {
      this->loop.dispatch([this, stream, clientId, id, sessionId, targetId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto treeAny = obj.get("frameTree");
          if (parseBool(obj.get("ok"), false) && treeAny.isObject()) {
            this->sendCDPResult(client, id, JSON::Object::Entries { {"frameTree", treeAny} }, sessionId);
            return;
          }
        }

        // Fallback to a single top-level frame.
        this->sendCDPResult(client, id, JSON::Object::Entries { {"frameTree", JSON::Object::Entries {
          {"frame", JSON::Object::Entries {
            {"id", targetId},
            {"loaderId", targetId},
            {"url", ""},
            {"name", ""},
            {"mimeType", "text/html"},
            {"securityOrigin", ""}
          }},
          {"childFrames", JSON::Array::Entries {}}
        }} }, sessionId);
      });
    });
  }

  void CDP::handlePageGetResourceTree (Client* client, int id, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const url = String((globalThis.location && globalThis.location.href) || '');"
      "  const origin = String((globalThis.location && globalThis.location.origin) || '');"
      "  const mimeType = String((globalThis.document && globalThis.document.contentType) || 'text/html');"
      "  const res = [];"
      "  const limit = 256;"
      "  if (url) res.push({ url, type: 'Document', mimeType });"
      "  try {"
      "    const scripts = globalThis.document && globalThis.document.scripts ? globalThis.document.scripts : [];"
      "    for (let i = 0; i < scripts.length && res.length < limit; i++) {"
      "      const s = scripts[i];"
      "      const src = s && s.src ? String(s.src) : '';"
      "      if (src) res.push({ url: src, type: 'Script', mimeType: 'application/javascript' });"
      "    }"
      "  } catch (e) {}"
      "  try {"
      "    const links = globalThis.document && globalThis.document.querySelectorAll"
      "      ? globalThis.document.querySelectorAll('link[rel~=\"stylesheet\"][href]')"
      "      : [];"
      "    for (let i = 0; i < links.length && res.length < limit; i++) {"
      "      const l = links[i];"
      "      const href = l && l.href ? String(l.href) : '';"
      "      if (href) res.push({ url: href, type: 'Stylesheet', mimeType: 'text/css' });"
      "    }"
      "  } catch (e) {}"
      "  return JSON.stringify({ ok: true, url, origin, mimeType, resources: res });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, url: '', origin: '', mimeType: 'text/html', resources: [] });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId, targetId](const JSON::Any& value) mutable {
      this->loop.dispatch([this, stream, clientId, id, sessionId, targetId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        String url;
        String origin;
        String mimeType = "text/html";
        JSON::Array::Entries resources;

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          if (parseBool(obj.get("ok"), false)) {
            const auto urlAny = obj.get("url");
            const auto originAny = obj.get("origin");
            const auto mimeAny = obj.get("mimeType");
            const auto resAny = obj.get("resources");
            if (urlAny.isString()) url = urlAny.as<JSON::String>().value();
            if (originAny.isString()) origin = originAny.as<JSON::String>().value();
            if (mimeAny.isString()) mimeType = mimeAny.as<JSON::String>().value();
            if (resAny.isArray()) resources = resAny.as<JSON::Array>().value();
          }
        }

        const auto frame = JSON::Object::Entries {
          {"id", targetId},
          {"loaderId", targetId},
          {"url", url},
          {"name", ""},
          {"mimeType", mimeType},
          {"securityOrigin", origin}
        };

        const auto tree = JSON::Object::Entries {
          {"frameTree", JSON::Object::Entries {
            {"frame", frame},
            {"resources", resources},
            {"childFrames", JSON::Array::Entries {}}
          }}
        };

        this->sendCDPResult(client, id, tree, sessionId);
      });
    });
  }

  void CDP::handlePageGetResourceContent (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto urlRequested = jsonStringOrEmpty(params, "url");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto urlLiteral = JSON::String(urlRequested).str();
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const requested = " + urlLiteral + ";"
      "  const current = String((globalThis.location && globalThis.location.href) || '');"
      "  let content = '';"
      "  if (!requested || requested === current) {"
      "    try {"
      "      const node = globalThis.document && globalThis.document.documentElement ? globalThis.document.documentElement : null;"
      "      content = node && node.outerHTML !== undefined ? String(node.outerHTML) : '';"
      "    } catch (e) { content = ''; }"
      "  }"
      "  const limit = " + std::to_string(MAX_PAGE_SET_DOCUMENT_CONTENT_BYTES) + ";"
      "  if (content && content.length > limit) content = content.slice(0, limit);"
      "  return JSON.stringify({ ok: true, content });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, content: '' });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        String content;
        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto cAny = obj.get("content");
          if (parseBool(obj.get("ok"), false) && cAny.isString()) {
            content = cAny.as<JSON::String>().value();
          }
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"content", content},
          {"base64Encoded", JSON::Boolean(false)}
        }, sessionId);
      });
    });
  }

  void CDP::handlePageGetLayoutMetrics (Client* client, int id, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string(
      "(function(){"
      " try {"
      "  const docEl = document.documentElement || {};"
      "  const body = document.body || {};"
      "  const cssScrollWidth = Math.max(Number(docEl.scrollWidth||0), Number(body.scrollWidth||0));"
      "  const cssScrollHeight = Math.max(Number(docEl.scrollHeight||0), Number(body.scrollHeight||0));"
      "  const cssClientWidth = Number(docEl.clientWidth||window.innerWidth||0);"
      "  const cssClientHeight = Number(docEl.clientHeight||window.innerHeight||0);"
      "  const cssPageX = Number(window.scrollX||window.pageXOffset||0);"
      "  const cssPageY = Number(window.scrollY||window.pageYOffset||0);"
      "  const dpr = Number(window.devicePixelRatio||1);"
      "  const vv = window.visualViewport;"
      "  const cssVisualViewport = vv ? {"
      "    offsetX: Number(vv.offsetLeft||0),"
      "    offsetY: Number(vv.offsetTop||0),"
      "    pageX: Number(vv.pageLeft||cssPageX),"
      "    pageY: Number(vv.pageTop||cssPageY),"
      "    clientWidth: Number(vv.width||cssClientWidth),"
      "    clientHeight: Number(vv.height||cssClientHeight),"
      "    scale: Number(vv.scale||1),"
      "    zoom: Number(vv.zoom||vv.scale||1)"
      "  } : {"
      "    offsetX: 0,"
      "    offsetY: 0,"
      "    pageX: cssPageX,"
      "    pageY: cssPageY,"
      "    clientWidth: Number(window.innerWidth||cssClientWidth),"
      "    clientHeight: Number(window.innerHeight||cssClientHeight),"
      "    scale: 1,"
      "    zoom: 1"
      "  };"
      "  const cssLayoutViewport = {"
      "    pageX: Math.round(cssPageX),"
      "    pageY: Math.round(cssPageY),"
      "    clientWidth: Math.round(cssClientWidth),"
      "    clientHeight: Math.round(cssClientHeight)"
      "  };"
      "  const cssContentSize = { x: 0, y: 0, width: Math.max(0, cssScrollWidth), height: Math.max(0, cssScrollHeight) };"
      "  const layoutViewport = {"
      "    pageX: Math.round(cssLayoutViewport.pageX * dpr),"
      "    pageY: Math.round(cssLayoutViewport.pageY * dpr),"
      "    clientWidth: Math.round(cssLayoutViewport.clientWidth * dpr),"
      "    clientHeight: Math.round(cssLayoutViewport.clientHeight * dpr)"
      "  };"
      "  const visualViewport = {"
      "    offsetX: cssVisualViewport.offsetX * dpr,"
      "    offsetY: cssVisualViewport.offsetY * dpr,"
      "    pageX: cssVisualViewport.pageX * dpr,"
      "    pageY: cssVisualViewport.pageY * dpr,"
      "    clientWidth: cssVisualViewport.clientWidth * dpr,"
      "    clientHeight: cssVisualViewport.clientHeight * dpr,"
      "    scale: cssVisualViewport.scale,"
      "    zoom: cssVisualViewport.zoom"
      "  };"
      "  const contentSize = { x: 0, y: 0, width: cssContentSize.width * dpr, height: cssContentSize.height * dpr };"
      "  return JSON.stringify({ ok: true, result: {"
      "    layoutViewport,"
      "    visualViewport,"
      "    contentSize,"
      "    cssLayoutViewport,"
      "    cssVisualViewport: cssVisualViewport,"
      "    cssContentSize"
      "  } });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPError(client, id, -32000, "Failed to compute layout metrics", sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("result") && parsedObj.get("result").isObject()) {
          this->sendCDPResult(client, id, parsedObj.get("result"), sessionId);
          return;
        }

        this->sendCDPError(client, id, -32000, "Failed to compute layout metrics", sessionId);
      });
    });
  }

  void CDP::handlePageBringToFront (Client* client, int id, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      const bool ok = [&]() {
        auto window = app->runtime.windowManager.getWindow(windowIndex);
        if (!window) {
          return false;
        }
        window->focus();
        return true;
      }();

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handlePageReload (Client* client, int id, const JSON::Any&, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    window->eval("location.reload()");
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handlePageStopLoading (Client* client, int id, const JSON::Any&, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    auto window = app->runtime.windowManager.getWindow(windowIndex);
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    std::optional<std::string> pendingRequestId;
    {
      Lock lock(this->mutex);
      if (this->windowPendingNavigationRequestId.contains(windowIndex)) {
        pendingRequestId = this->windowPendingNavigationRequestId.at(windowIndex);
        this->windowPendingNavigationRequestId.erase(windowIndex);
      }
    }

    if (pendingRequestId.has_value()) {
      const auto requestId = String(pendingRequestId.value());
      const double ts = monotonicSeconds();
      broadcastToAttachedTarget(targetId, [this, requestId, targetId, ts](auto client, const auto& sid) {
        this->sendCDPEvent(client, "Network.loadingFailed", JSON::Object::Entries {
          {"requestId", requestId},
          {"loaderId", targetId},
          {"timestamp", ts},
          {"type", "Document"},
          {"errorText", "Cancelled"},
          {"canceled", true}
        }, sid);
      });
    }

    window->eval("(function(){ try { window.stop(); } catch (e) {} })()");
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handlePageClose (Client* client, int id, const JSON::Any&, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto windowIndex = maybeWindow.value();

    // Respond first (closing the window may tear down the underlying session).
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);

    app->dispatch([app, windowIndex]() {
      app->runtime.windowManager.destroyWindow(windowIndex);
    });
  }

  void CDP::handlePageGetNavigationHistory (Client* client, int id, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    JSON::Array::Entries entries;
    int currentIndex = 0;

    {
      Lock lock(this->mutex);
      auto& history = this->windowNavigationHistory[windowIndex];

      if (history.empty()) {
        auto app = App::sharedApplication();
        String urlNow = "about:blank";
        if (app) {
          if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
            urlNow = window->bridge->navigator.location.str();
          }
        }

        const int entryId = this->nextNavigationEntryId.fetch_add(1);
        history.push_back(NavigationEntry {
          .id = entryId,
          .url = urlNow,
          .title = ""
        });
        this->windowNavigationHistoryIndex[windowIndex] = 0;
      }

      if (this->windowNavigationHistoryIndex.contains(windowIndex)) {
        currentIndex = this->windowNavigationHistoryIndex.at(windowIndex);
      }
      if (currentIndex < 0) currentIndex = 0;
      if (currentIndex >= static_cast<int>(history.size())) {
        currentIndex = history.empty() ? 0 : static_cast<int>(history.size()) - 1;
      }

      for (const auto& e : history) {
        entries.push_back(JSON::Object::Entries {
          {"id", e.id},
          {"url", e.url},
          {"title", e.title},
          {"transitionType", "link"}
        });
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {
      {"currentIndex", currentIndex},
      {"entries", entries}
    }, sessionId);
  }

  void CDP::handlePageNavigateToHistoryEntry (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int entryId = jsonIntOr(params, "entryId", -1);
    if (entryId < 0) {
      return this->sendCDPError(client, id, -32602, "Missing entryId", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    String urlToNavigate = "";

    {
      Lock lock(this->mutex);
      if (!this->windowNavigationHistory.contains(windowIndex)) {
        return this->sendCDPError(client, id, -32000, "No navigation history", sessionId);
      }

      auto& history = this->windowNavigationHistory.at(windowIndex);
      for (int i = 0; i < static_cast<int>(history.size()); ++i) {
        if (history.at(i).id == entryId) {
          urlToNavigate = history.at(i).url;
          this->windowNavigationHistoryIndex[windowIndex] = i;
          break;
        }
      }
    }

    if (urlToNavigate.empty()) {
      return this->sendCDPError(client, id, -32000, "History entry not found", sessionId);
    }

    if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
      window->navigate(urlToNavigate);
      this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      return;
    }

    this->sendCDPError(client, id, -32000, "Window not found", sessionId);
  }

  void CDP::handlePageCaptureScreenshot (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto format = params.isObject() ? jsonStringOrEmpty(params, "format") : "";
    if (!format.empty() && format != "png") {
      return this->sendCDPError(client, id, -32000, "Not supported", sessionId);
    }

    const bool captureBeyondViewport = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("captureBeyondViewport"), false)
      : false;

    struct Clip {
      bool enabled = false;
      double x = 0;
      double y = 0;
      double width = 0;
      double height = 0;
      double scale = 1.0;
    } clip;

    if (params.isObject() && params.as<JSON::Object>().get("clip").isObject()) {
      const auto& c = params.as<JSON::Object>().get("clip");
      clip.enabled = true;
      clip.x = jsonDoubleOr(c, "x", 0);
      clip.y = jsonDoubleOr(c, "y", 0);
      clip.width = jsonDoubleOr(c, "width", 0);
      clip.height = jsonDoubleOr(c, "height", 0);
      clip.scale = jsonDoubleOr(c, "scale", 1.0);
      if (clip.scale <= 0) clip.scale = 1.0;
      if (clip.width <= 0 || clip.height <= 0) {
        clip.enabled = false;
      }
    }

    if (clip.enabled) {
      const int64_t w = static_cast<int64_t>(clip.width * clip.scale);
      const int64_t h = static_cast<int64_t>(clip.height * clip.scale);
      if (!screenshotDimensionsAllowed(w, h)) {
        return this->sendCDPError(client, id, -32000, "Screenshot too large", sessionId);
      }
    }

    const auto windowIndex = maybeWindow.value();
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

#if ORO_RUNTIME_PLATFORM_LINUX && !ORO_RUNTIME_DESKTOP_EXTENSION
    struct SnapshotRequest {
      oro::runtime::SharedPointer<App> app;
      CDP* cdp = nullptr;
      uv_stream_t* stream = nullptr;
      uint64_t clientId = 0;
      int commandId = -1;
      String sessionId;
      int windowIndex = -1;
      bool captureBeyondViewport = false;
      Clip clip;
    };

    auto req = new SnapshotRequest {
      .app = app,
      .cdp = this,
      .stream = stream,
      .clientId = clientId,
      .commandId = id,
      .sessionId = sessionId,
      .windowIndex = windowIndex,
      .captureBeyondViewport = captureBeyondViewport,
      .clip = clip
    };

    req->app->dispatch([req]() {
      auto window = req->app->runtime.windowManager.getWindow(req->windowIndex);
      if (!window || !window->webview) {
        const bool dispatched = req->cdp->loop.dispatch([req]() {
          Client* client = nullptr;
          {
            Lock lock(req->cdp->mutex);
            if (req->cdp->clients.contains(req->stream)) {
              auto candidate = req->cdp->clients.at(req->stream);
              if (candidate && candidate->id == req->clientId) {
                client = candidate;
              }
            }
          }

          if (client && !client->closing && !client->closed) {
            req->cdp->sendCDPError(client, req->commandId, -32000, "Window not found", req->sessionId);
          }
          delete req;
        });
        if (!dispatched) {
          delete req;
        }
        return;
      }

      const auto startSnapshot = [req, window]() {
        const auto region = req->captureBeyondViewport
          ? WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT
          : WEBKIT_SNAPSHOT_REGION_VISIBLE;

        webkit_web_view_get_snapshot(
          WEBKIT_WEB_VIEW(window->webview),
          region,
          WEBKIT_SNAPSHOT_OPTIONS_NONE,
          nullptr,
          +[](GObject* object, GAsyncResult* result, gpointer userData) {
            auto req = static_cast<SnapshotRequest*>(userData);
            GError* error = nullptr;
            cairo_surface_t* surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(object), result, &error);

            if (!surface) {
              const auto errMsg = String(error && error->message ? error->message : "Snapshot failed");
              if (error) g_error_free(error);
              const bool dispatched = req->cdp->loop.dispatch([req, errMsg]() {
                Client* client = nullptr;
                {
                  Lock lock(req->cdp->mutex);
                  if (req->cdp->clients.contains(req->stream)) {
                    auto candidate = req->cdp->clients.at(req->stream);
                    if (candidate && candidate->id == req->clientId) {
                      client = candidate;
                    }
                  }
                }

                if (client && !client->closing && !client->closed) {
                  req->cdp->sendCDPError(client, req->commandId, -32000, errMsg.c_str(), req->sessionId);
                }
                delete req;
              });
              if (!dispatched) {
                delete req;
              }
              return;
            }

            cairo_surface_t* outSurface = surface;
            cairo_surface_t* cropped = nullptr;

            if (req->clip.enabled) {
              const int64_t w = static_cast<int64_t>(req->clip.width * req->clip.scale);
              const int64_t h = static_cast<int64_t>(req->clip.height * req->clip.scale);
              const double x = req->clip.x * req->clip.scale;
              const double y = req->clip.y * req->clip.scale;

              if (!screenshotDimensionsAllowed(w, h)) {
                if (surface) cairo_surface_destroy(surface);
                const bool dispatched = req->cdp->loop.dispatch([req]() {
                  Client* client = nullptr;
                  {
                    Lock lock(req->cdp->mutex);
                    if (req->cdp->clients.contains(req->stream)) {
                      auto candidate = req->cdp->clients.at(req->stream);
                      if (candidate && candidate->id == req->clientId) {
                        client = candidate;
                      }
                    }
                  }

                  if (client && !client->closing && !client->closed) {
                    req->cdp->sendCDPError(client, req->commandId, -32000, "Screenshot too large", req->sessionId);
                  }
                  delete req;
                });
                if (!dispatched) {
                  delete req;
                }
                return;
              }

              cropped = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(w), static_cast<int>(h));
              auto cr = cairo_create(cropped);
              cairo_set_source_surface(cr, surface, -x, -y);
              cairo_rectangle(cr, 0, 0, w, h);
              cairo_fill(cr);
              cairo_destroy(cr);
              outSurface = cropped;
            }

            struct PngBytes {
              Vector<uint8_t> bytes;
            } png;

            const auto writeCb = +[](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
              auto png = static_cast<PngBytes*>(closure);
              png->bytes.insert(png->bytes.end(), data, data + length);
              return CAIRO_STATUS_SUCCESS;
            };

            cairo_status_t status = cairo_surface_write_to_png_stream(outSurface, writeCb, &png);

            if (cropped) {
              cairo_surface_destroy(cropped);
            }
            cairo_surface_destroy(surface);

            if (status != CAIRO_STATUS_SUCCESS) {
              const bool dispatched = req->cdp->loop.dispatch([req]() {
                Client* client = nullptr;
                {
                  Lock lock(req->cdp->mutex);
                  if (req->cdp->clients.contains(req->stream)) {
                    auto candidate = req->cdp->clients.at(req->stream);
                    if (candidate && candidate->id == req->clientId) {
                      client = candidate;
                    }
                  }
                }

                if (client && !client->closing && !client->closed) {
                  req->cdp->sendCDPError(client, req->commandId, -32000, "Snapshot failed", req->sessionId);
                }
                delete req;
              });
              if (!dispatched) {
                delete req;
              }
              return;
            }

            if (png.bytes.size() > MAX_SCREENSHOT_PNG_BYTES) {
              const bool dispatched = req->cdp->loop.dispatch([req]() {
                Client* client = nullptr;
                {
                  Lock lock(req->cdp->mutex);
                  if (req->cdp->clients.contains(req->stream)) {
                    auto candidate = req->cdp->clients.at(req->stream);
                    if (candidate && candidate->id == req->clientId) {
                      client = candidate;
                    }
                  }
                }

                if (client && !client->closing && !client->closed) {
                  req->cdp->sendCDPError(client, req->commandId, -32000, "Screenshot too large", req->sessionId);
                }
                delete req;
              });
              if (!dispatched) {
                delete req;
              }
              return;
            }

            const bool dispatched = req->cdp->loop.dispatch([req, bytes = std::move(png.bytes)]() mutable {
              Client* client = nullptr;
              {
                Lock lock(req->cdp->mutex);
                if (req->cdp->clients.contains(req->stream)) {
                  auto candidate = req->cdp->clients.at(req->stream);
                  if (candidate && candidate->id == req->clientId) {
                    client = candidate;
                  }
                }
              }

              if (client && !client->closing && !client->closed) {
                const auto b64 = oro::runtime::bytes::base64::encode(bytes);
                if (b64.size() > MAX_WS_MESSAGE_SIZE) {
                  req->cdp->sendCDPError(client, req->commandId, -32000, "Screenshot too large", req->sessionId);
                } else {
                  req->cdp->sendCDPResult(client, req->commandId, JSON::Object::Entries { {"data", b64} }, req->sessionId);
                }
              }

              delete req;
            });
            if (!dispatched) {
              delete req;
            }
          },
          req
        );
      };

      if (req->captureBeyondViewport) {
        // Guard full-document snapshots; extremely tall pages can crash or OOM.
        window->eval(
          "(function(){"
          " try {"
          "  const d = document.documentElement;"
          "  const w = Math.max(Number(d && (d.scrollWidth || d.clientWidth) || 0), Number(window.innerWidth || 0));"
          "  const h = Math.max(Number(d && (d.scrollHeight || d.clientHeight) || 0), Number(window.innerHeight || 0));"
          "  return JSON.stringify({ width: w, height: h });"
          " } catch (e) {"
          "  return JSON.stringify({ width: 0, height: 0 });"
          " }"
          "})()",
          [req, startSnapshot](const JSON::Any& value) mutable {
            const auto parsedOpt = parseJSONResultString(value);
            int64_t w = 0;
            int64_t h = 0;
            if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
              w = static_cast<int64_t>(jsonDoubleOr(parsedOpt.value(), "width", 0));
              h = static_cast<int64_t>(jsonDoubleOr(parsedOpt.value(), "height", 0));
            }

            if (!screenshotDimensionsAllowed(w, h)) {
              const bool dispatched = req->cdp->loop.dispatch([req]() {
                Client* client = nullptr;
                {
                  Lock lock(req->cdp->mutex);
                  if (req->cdp->clients.contains(req->stream)) {
                    auto candidate = req->cdp->clients.at(req->stream);
                    if (candidate && candidate->id == req->clientId) {
                      client = candidate;
                    }
                  }
                }

                if (client && !client->closing && !client->closed) {
                  req->cdp->sendCDPError(client, req->commandId, -32000, "Screenshot too large", req->sessionId);
                }
                delete req;
              });
              if (!dispatched) {
                delete req;
              }
              return;
            }

            startSnapshot();
          }
        );
        return;
      }

      startSnapshot();
    });
#else
    (void) captureBeyondViewport;
    (void) clip;
    this->sendCDPError(client, id, -32000, "Not supported", sessionId);
#endif
  }

  void CDP::handlePageAddScriptToEvaluateOnNewDocument (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto source = jsonStringOrEmpty(params, "source");
    if (source.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing source", sessionId);
    }

    const auto worldName = jsonStringOrEmpty(params, "worldName");

    if (source.size() > MAX_NEW_DOCUMENT_SCRIPT_BYTES) {
      return this->sendCDPError(client, id, -32000, "Script too large", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto identifier = uuid::v7();
    bool tooManyScripts = false;
    bool storageLimitExceeded = false;

    {
      Lock lock(this->mutex);
      const auto key = std::string(targetId);
      auto& scripts = this->targetNewDocumentScripts[key];

      if (scripts.size() >= MAX_NEW_DOCUMENT_SCRIPTS_PER_TARGET) {
        tooManyScripts = true;
      } else {
        size_t totalBytes = 0;
        for (const auto& s : scripts) {
          totalBytes += s.source.size();
        }

        if (totalBytes + source.size() > MAX_NEW_DOCUMENT_SCRIPT_TOTAL_BYTES_PER_TARGET) {
          storageLimitExceeded = true;
        } else {
          scripts.push_back(ScriptToEvaluateOnNewDocument {
            .identifier = identifier,
            .source = source,
            .worldName = worldName
          });
        }
      }
    }

    if (tooManyScripts) {
      return this->sendCDPError(client, id, -32000, "Too many scripts", sessionId);
    }

    if (storageLimitExceeded) {
      return this->sendCDPError(client, id, -32000, "Script storage limit exceeded", sessionId);
    }

    const bool runImmediately = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("runImmediately"), false)
      : false;

    if (runImmediately) {
      const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
      auto app = App::sharedApplication();
      if (maybeWindow.has_value() && app) {
        auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
        if (window) {
          window->eval(source);
        }
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries { {"identifier", identifier} }, sessionId);
  }

  void CDP::handlePageRemoveScriptToEvaluateOnNewDocument (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto identifier = jsonStringOrEmpty(params, "identifier");
    if (identifier.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing identifier", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    {
      Lock lock(this->mutex);
      const auto key = std::string(targetId);
      if (this->targetNewDocumentScripts.contains(key)) {
        auto& scripts = this->targetNewDocumentScripts.at(key);
        auto it = scripts.begin();
        while (it != scripts.end()) {
          if (it->identifier == identifier) {
            it = scripts.erase(it);
          } else {
            ++it;
          }
        }
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleRuntimeEvaluate (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto expression = jsonStringOrEmpty(params, "expression");
    if (expression.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing expression", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const bool returnByValue = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("returnByValue"), false)
      : false;
    const bool awaitPromise = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("awaitPromise"), false)
      : false;
    const auto objectGroup = jsonStringOrEmpty(params, "objectGroup");

    const int windowIndex = maybeWindow.value();
    const int requestedContextId = params.isObject()
      ? static_cast<int>(jsonDoubleOr(params, "contextId", jsonDoubleOr(params, "executionContextId", 0.0)))
      : 0;

    int evalContextId = 0;
    bool invalidContextId = false;

    {
      Lock lock(this->mutex);
      const auto isKnownContextId = [&](int candidate) -> bool {
        if (candidate <= 0) {
          return false;
        }

        if (
          this->windowExecutionContexts.contains(windowIndex) &&
          this->windowExecutionContexts.at(windowIndex) == candidate
        ) {
          return true;
        }

        if (this->windowIsolatedWorldExecutionContexts.contains(windowIndex)) {
          const auto& frameMap = this->windowIsolatedWorldExecutionContexts.at(windowIndex);
          for (const auto& frameEntry : frameMap) {
            for (const auto& worldEntry : frameEntry.second) {
              if (worldEntry.second == candidate) {
                return true;
              }
            }
          }
        }

        return false;
      };

      if (requestedContextId > 0) {
        if (!isKnownContextId(requestedContextId)) {
          invalidContextId = true;
        } else {
          evalContextId = requestedContextId;
        }
      } else if (this->windowExecutionContexts.contains(windowIndex)) {
        evalContextId = this->windowExecutionContexts.at(windowIndex);
      } else {
        evalContextId = this->nextExecutionContextId.fetch_add(1);
        this->windowExecutionContexts[windowIndex] = evalContextId;
      }
    }

    if (invalidContextId) {
      return this->sendCDPError(client, id, -32000, "Cannot find context with specified id", sessionId);
    }

    // Evaluate by embedding the expression directly. This avoids relying on
    // `eval`/`new Function`, which may be blocked by CSP `unsafe-eval`.
    auto exprSource = trimRight(expression);
    while (!exprSource.empty() && exprSource.back() == ';') {
      exprSource.pop_back();
      exprSource = trimRight(exprSource);
    }
    const auto objectGroupLiteral = JSON::String(objectGroup).str();
    const auto wrapper = std::string(
      "(async () => {"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro_prev_executionContextId = globalThis.__oro_cdp_active_execution_context_id;"
      "  globalThis.__oro_cdp_active_execution_context_id = " + std::to_string(evalContextId) + ";"
      "  try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __returnByValue = " + (returnByValue ? std::string("true") : std::string("false")) + ";"
      "  const __awaitPromise = " + (awaitPromise ? std::string("true") : std::string("false")) + ";"
      "  const __objectGroup = " + objectGroupLiteral + ";"
      "  let __value = ("
    ) + exprSource + std::string(
      ");"
      "  if (__awaitPromise && __value && typeof __value.then === 'function') {"
      "    __value = await __value;"
      "  }"
      "  const __result = __oro.toRemoteObject(__value, { returnByValue: __returnByValue, objectGroup: __objectGroup });"
      "  return JSON.stringify({ ok: true, result: __result });"
      "  } finally {"
      "    globalThis.__oro_cdp_active_execution_context_id = __oro_prev_executionContextId;"
      "  }"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e), stack: String(e && e.stack || '') } });"
      " }"
      "})()"
    );

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    // Respond asynchronously once eval completes.
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      // Marshal back to loop thread for writing.
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        if (!value.isString()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }}
          }, sessionId);
          return;
        }

        const auto text = value.as<JSON::String>().value();
        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries {
              {"type", "string"},
              {"value", text}
            }}
          }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (!parsedObj.has("ok")) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }}
          }, sessionId);
          return;
        }

        if (!parseBool(parsedObj.get("ok"), false)) {
          String message = "Evaluation failed";
          if (parsedObj.has("error")) {
            const auto& errAny = parsedObj.get("error");
            const auto errMessage = jsonStringOrEmpty(errAny, "message");
            if (!errMessage.empty()) {
              message = errMessage;
            }
          }

          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }},
            {"exceptionDetails", JSON::Object::Entries { {"text", message} }}
          }, sessionId);

          return;
        }

        if (parsedObj.has("result")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"result", parsedObj.get("result")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"result", JSON::Object::Entries { {"type", "undefined"} }}
        }, sessionId);
      });
    });
  }

  void CDP::handleRuntimeCallFunctionOn (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto functionDeclaration = jsonStringOrEmpty(params, "functionDeclaration");
    if (functionDeclaration.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing functionDeclaration", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectId = jsonStringOrEmpty(params, "objectId");
    const bool returnByValue = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("returnByValue"), false)
      : false;
    const bool awaitPromise = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("awaitPromise"), false)
      : false;
    const auto objectGroup = jsonStringOrEmpty(params, "objectGroup");

    const int windowIndex = maybeWindow.value();
    const int requestedContextId = params.isObject()
      ? static_cast<int>(jsonDoubleOr(params, "executionContextId", jsonDoubleOr(params, "contextId", 0.0)))
      : 0;

    int evalContextId = 0;
    bool invalidContextId = false;

    {
      Lock lock(this->mutex);
      const auto isKnownContextId = [&](int candidate) -> bool {
        if (candidate <= 0) {
          return false;
        }

        if (
          this->windowExecutionContexts.contains(windowIndex) &&
          this->windowExecutionContexts.at(windowIndex) == candidate
        ) {
          return true;
        }

        if (this->windowIsolatedWorldExecutionContexts.contains(windowIndex)) {
          const auto& frameMap = this->windowIsolatedWorldExecutionContexts.at(windowIndex);
          for (const auto& frameEntry : frameMap) {
            for (const auto& worldEntry : frameEntry.second) {
              if (worldEntry.second == candidate) {
                return true;
              }
            }
          }
        }

        return false;
      };

      if (requestedContextId > 0) {
        if (!isKnownContextId(requestedContextId)) {
          invalidContextId = true;
        } else {
          evalContextId = requestedContextId;
        }
      } else if (this->windowExecutionContexts.contains(windowIndex)) {
        evalContextId = this->windowExecutionContexts.at(windowIndex);
      } else {
        evalContextId = this->nextExecutionContextId.fetch_add(1);
        this->windowExecutionContexts[windowIndex] = evalContextId;
      }
    }

    if (invalidContextId) {
      return this->sendCDPError(client, id, -32000, "Cannot find context with specified id", sessionId);
    }

    const auto objectIdLiteral = JSON::String(objectId).str();
    const auto argsLiteral = params.isObject() && params.as<JSON::Object>().get("arguments").isArray()
      ? params.as<JSON::Object>().get("arguments").str()
      : "[]";
    const auto objectGroupLiteral = JSON::String(objectGroup).str();

    auto fnSource = trimRight(functionDeclaration);
    while (!fnSource.empty() && fnSource.back() == ';') {
      fnSource.pop_back();
      fnSource = trimRight(fnSource);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro_prev_executionContextId = globalThis.__oro_cdp_active_execution_context_id;"
      "  globalThis.__oro_cdp_active_execution_context_id = " + std::to_string(evalContextId) + ";"
      "  try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __objectId = " + objectIdLiteral + ";"
      "  const __args = " + argsLiteral + ";"
      "  const __returnByValue = " + (returnByValue ? std::string("true") : std::string("false")) + ";"
      "  const __awaitPromise = " + (awaitPromise ? std::string("true") : std::string("false")) + ";"
      "  const __objectGroup = " + objectGroupLiteral + ";"
      "  const fn = ("
    ) + fnSource + std::string(
      ");"
      "  const thisObj = __objectId ? __oro.store.objects.get(__objectId) : undefined;"
      "  const args = Array.isArray(__args) ? __args.map((a) => {"
      "    if (!a || typeof a !== 'object') return undefined;"
      "    if (a.objectId) return __oro.store.objects.get(a.objectId);"
      "    if (typeof a.unserializableValue === 'string') return __oro.parseUnserializableValue(a.unserializableValue);"
      "    if ('value' in a) return a.value;"
      "    return undefined;"
      "  }) : [];"
      "  let __value = fn.apply(thisObj, args);"
      "  if (__awaitPromise && __value && typeof __value.then === 'function') {"
      "    __value = await __value;"
      "  }"
      "  const __result = __oro.toRemoteObject(__value, { returnByValue: __returnByValue, objectGroup: __objectGroup });"
      "  return JSON.stringify({ ok: true, result: __result });"
      "  } finally {"
      "    globalThis.__oro_cdp_active_execution_context_id = __oro_prev_executionContextId;"
      "  }"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e), stack: String(e && e.stack || '') } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        if (!value.isString()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }}
          }, sessionId);
          return;
        }

        const auto text = value.as<JSON::String>().value();
        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries {
              {"type", "string"},
              {"value", text}
            }}
          }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (!parsedObj.has("ok")) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }}
          }, sessionId);
          return;
        }

        if (!parseBool(parsedObj.get("ok"), false)) {
          String message = "Evaluation failed";
          if (parsedObj.has("error")) {
            const auto& errAny = parsedObj.get("error");
            const auto errMessage = jsonStringOrEmpty(errAny, "message");
            if (!errMessage.empty()) {
              message = errMessage;
            }
          }

          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Object::Entries { {"type", "undefined"} }},
            {"exceptionDetails", JSON::Object::Entries { {"text", message} }}
          }, sessionId);

          return;
        }

        if (parsedObj.has("result")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"result", parsedObj.get("result")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"result", JSON::Object::Entries { {"type", "undefined"} }}
        }, sessionId);
      });
    });
  }

  void CDP::handleRuntimeGetProperties (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto objectId = jsonStringOrEmpty(params, "objectId");
    if (objectId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing objectId", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectIdLiteral = JSON::String(objectId).str();

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __objectId = " + objectIdLiteral + ";"
      "  const obj = __oro.store.objects.get(__objectId);"
      "  if (!obj) return JSON.stringify({ ok: true, result: [] });"
      "  const descs = Object.getOwnPropertyDescriptors(obj);"
      "  const out = [];"
      "  const __keys = Object.keys(descs);"
      "  const __limit = " + std::to_string(MAX_RUNTIME_PROPERTY_DESCRIPTORS) + ";"
      "  for (let __i = 0; __i < __keys.length && __i < __limit; __i++) {"
      "    const key = __keys[__i];"
      "    const d = descs[key];"
      "    const pd = {"
      "      name: key,"
      "      enumerable: Boolean(d.enumerable),"
      "      configurable: Boolean(d.configurable),"
      "      isOwn: true"
      "    };"
      "    if ('value' in d) {"
      "      pd.value = __oro.toRemoteObject(d.value);"
      "      pd.writable = Boolean(d.writable);"
      "    }"
      "    if (d.get) pd.get = __oro.toRemoteObject(d.get);"
      "    if (d.set) pd.set = __oro.toRemoteObject(d.set);"
      "    out.push(pd);"
      "  }"
      "  return JSON.stringify({ ok: true, result: out });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e), stack: String(e && e.stack || '') } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        if (!value.isString()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Array::Entries {}}
          }, sessionId);
          return;
        }

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Array::Entries {}}
          }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (!parsedObj.has("ok")) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Array::Entries {}}
          }, sessionId);
          return;
        }

        if (!parseBool(parsedObj.get("ok"), false)) {
          String message = "getProperties failed";
          if (parsedObj.has("error")) {
            const auto& errAny = parsedObj.get("error");
            const auto errMessage = jsonStringOrEmpty(errAny, "message");
            if (!errMessage.empty()) {
              message = errMessage;
            }
          }

          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"result", JSON::Array::Entries {}},
            {"exceptionDetails", JSON::Object::Entries { {"text", message} }}
          }, sessionId);
          return;
        }

        if (parsedObj.has("result") && parsedObj.get("result").isArray()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"result", parsedObj.get("result")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"result", JSON::Array::Entries {}}
        }, sessionId);
      });
    });
  }

  void CDP::handleRuntimeReleaseObject (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto objectId = jsonStringOrEmpty(params, "objectId");
    if (objectId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing objectId", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectIdLiteral = JSON::String(objectId).str();

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __objectId = " + objectIdLiteral + ";"
      "  __oro.store.objects.delete(__objectId);"
      "  return JSON.stringify({ ok: true });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any&) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleRuntimeReleaseObjectGroup (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto objectGroup = jsonStringOrEmpty(params, "objectGroup");
    if (objectGroup.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing objectGroup", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectGroupLiteral = JSON::String(objectGroup).str();

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __objectGroup = " + objectGroupLiteral + ";"
      "  __oro.releaseObjectGroup(__objectGroup);"
      "  return JSON.stringify({ ok: true });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any&) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;
        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleRuntimeAddBinding (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto name = jsonStringOrEmpty(params, "name");
    if (name.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing name", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    bool tooManyBindings = false;

    {
      Lock lock(this->mutex);
      const auto key = std::string(targetId);
      const auto bindingName = std::string(name);
      auto& bindings = this->targetBindings[key];

      if (!bindings.contains(bindingName) && bindings.size() >= MAX_BINDINGS_PER_TARGET) {
        tooManyBindings = true;
      } else {
        bindings.insert(bindingName);
      }
    }

    if (tooManyBindings) {
      return this->sendCDPError(client, id, -32000, "Too many bindings", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (maybeWindow.has_value() && app) {
      auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
      if (window) {
        const int defaultContextId = [&]() {
          Lock lock(this->mutex);
          const int windowIndex = maybeWindow.value();
          if (this->windowExecutionContexts.contains(windowIndex)) {
            return this->windowExecutionContexts.at(windowIndex);
          }
          return 0;
        }();

        const auto nameLiteral = JSON::String(name).str();
        window->eval(
          "(function(){"
          " try {"
          "  const name = " + nameLiteral + ";"
          "  if (Object.prototype.hasOwnProperty.call(globalThis, name)) return;"
          "  Object.defineProperty(globalThis, name, {"
          "    configurable: true,"
          "    enumerable: false,"
          "    writable: false,"
          "    value: function(payload) {"
          "      try {"
          "        let p = '';"
          "        if (payload === undefined) p = '';"
          "        else if (typeof payload === 'string') p = payload;"
          "        else {"
          "          try { p = JSON.stringify(payload); } catch (e2) { p = String(payload); }"
          "        }"
          "        if (typeof p === 'string' && p.length > 1024 * 1024) p = p.slice(0, 1024 * 1024);"
          "        let executionContextId = 0;"
          "        try { executionContextId = Number(globalThis.__oro_cdp_active_execution_context_id || 0); } catch (e3) {}"
          "        if (!executionContextId || !Number.isFinite(executionContextId) || executionContextId <= 0) executionContextId = " + std::to_string(defaultContextId) + ";"
          "        import('oro:ipc').then((m) => {"
          "          const ipc = m && (m.default || m);"
          "          if (ipc && ipc.send) ipc.send('cdp.bindingCalled', { name, payload: p, executionContextId });"
          "        });"
          "      } catch (e) {}"
          "    }"
          "  });"
          " } catch (e) {}"
          "})();"
        );
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleRuntimeRemoveBinding (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto name = jsonStringOrEmpty(params, "name");
    if (name.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing name", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    {
      Lock lock(this->mutex);
      const auto key = std::string(targetId);
      if (this->targetBindings.contains(key)) {
        this->targetBindings.at(key).erase(name);
      }
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (maybeWindow.has_value() && app) {
      auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
      if (window) {
        const auto nameLiteral = JSON::String(name).str();
        window->eval(
          "(function(){"
          " try {"
          "  const name = " + nameLiteral + ";"
          "  try { delete globalThis[name]; } catch (e) {}"
          " } catch (e) {}"
          "})();"
        );
      }
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMGetDocument (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    int depth = jsonIntOr(params, "depth", 1);
    if (depth < 0) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    } else if (depth > MAX_DOM_SERIALIZE_DEPTH) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    }
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const root = __oro.serializeNode(document, " + std::to_string(depth) + ");"
      "  return JSON.stringify({ ok: true, root });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"root", JSON::Object::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("root")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"root", parsedObj.get("root")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"root", JSON::Object::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleDOMGetOuterHTML (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ") || document.documentElement;"
      "  let html = '';"
      "  try { html = node && node.outerHTML !== undefined ? String(node.outerHTML) : ''; } catch (e) { html = ''; }"
      "  const limit = " + std::to_string(MAX_PAGE_SET_DOCUMENT_CONTENT_BYTES) + ";"
      "  if (html && html.length > limit) html = html.slice(0, limit);"
      "  return JSON.stringify({ ok: true, outerHTML: html });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, outerHTML: '' });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"outerHTML", ""} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& obj = parsed.as<JSON::Object>();
        const auto outer = obj.get("outerHTML");
        if (parseBool(obj.get("ok"), false) && outer.isString()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"outerHTML", outer.as<JSON::String>().value()} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"outerHTML", ""} }, sessionId);
      });
    });
  }

  void CDP::handleDOMSetOuterHTML (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const auto outerHTML = jsonStringOrEmpty(params, "outerHTML");
    if (outerHTML.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing outerHTML", sessionId);
    }

    if (outerHTML.size() > MAX_PAGE_SET_DOCUMENT_CONTENT_BYTES) {
      return this->sendCDPError(client, id, -32000, "outerHTML too large", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto htmlLiteral = JSON::String(outerHTML).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  if (node && node.outerHTML !== undefined) {"
      "    node.outerHTML = " + htmlLiteral + ";"
      "  }"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMGetAttributes (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : null;"
      "  const out = [];"
      "  if (el && el.attributes) {"
      "    const limit = 2048;"
      "    for (let i = 0; i < el.attributes.length && out.length < limit; i++) {"
      "      const a = el.attributes[i];"
      "      if (!a) continue;"
      "      const n = String(a.name || '');"
      "      const v = String(a.value || '');"
      "      out.push(n, v);"
      "    }"
      "  }"
      "  return JSON.stringify({ ok: true, attributes: out });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, attributes: [] });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"attributes", JSON::Array::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& obj = parsed.as<JSON::Object>();
        const auto attrs = obj.get("attributes");
        if (parseBool(obj.get("ok"), false) && attrs.isArray()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"attributes", attrs} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"attributes", JSON::Array::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleDOMRequestChildNodes (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    int depth = jsonIntOr(params, "depth", 1);

    if (depth < 0) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    } else if (depth > MAX_DOM_SERIALIZE_DEPTH) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const parent = node ? __oro.serializeNode(node, " + std::to_string(depth) + ") : null;"
      "  const nodes = parent && Array.isArray(parent.children) ? parent.children : [];"
      "  return JSON.stringify({ ok: true, nodes });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, nodes: [] });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId, nodeId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, nodeId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        JSON::Any nodesAny = JSON::Array::Entries {};
        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto n = obj.get("nodes");
          if (parseBool(obj.get("ok"), false) && n.isArray()) {
            nodesAny = n;
          }
        }

        this->sendCDPEvent(client, "DOM.setChildNodes", JSON::Object::Entries {
          {"parentId", JSON::Number(nodeId)},
          {"nodes", nodesAny}
        }, sessionId);

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleDOMSetAttributeValue (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const auto name = jsonStringOrEmpty(params, "name");
    const auto value = jsonStringOrEmpty(params, "value");

    if (nodeId <= 0 || name.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing nodeId/name", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto nameLiteral = JSON::String(name).str();
    const auto valueLiteral = JSON::String(value).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : null;"
      "  if (el && el.setAttribute) el.setAttribute(" + nameLiteral + ", " + valueLiteral + ");"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMRemoveAttribute (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const auto name = jsonStringOrEmpty(params, "name");

    if (nodeId <= 0 || name.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing nodeId/name", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto nameLiteral = JSON::String(name).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : null;"
      "  if (el && el.removeAttribute) el.removeAttribute(" + nameLiteral + ");"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMSetNodeValue (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const auto value = jsonStringOrEmpty(params, "value");

    if (nodeId <= 0) {
      return this->sendCDPError(client, id, -32602, "Missing nodeId", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto valueLiteral = JSON::String(value).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  if (node && ('nodeValue' in node)) node.nodeValue = " + valueLiteral + ";"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMQuerySelector (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto selector = jsonStringOrEmpty(params, "selector");
    if (selector.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing selector", sessionId);
    }

    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto selectorLiteral = JSON::String(selector).str();
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ") || document;"
      "  const sel = " + selectorLiteral + ";"
      "  const el = node && node.querySelector ? node.querySelector(sel) : null;"
      "  const outId = el ? __oro.getOrCreateNodeId(el) : 0;"
      "  return JSON.stringify({ ok: true, nodeId: outId });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeId", JSON::Number(0)} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("nodeId")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeId", parsedObj.get("nodeId")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeId", JSON::Number(0)} }, sessionId);
      });
    });
  }

  void CDP::handleDOMQuerySelectorAll (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto selector = jsonStringOrEmpty(params, "selector");
    if (selector.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing selector", sessionId);
    }

    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto selectorLiteral = JSON::String(selector).str();
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ") || document;"
      "  const sel = " + selectorLiteral + ";"
      "  const list = node && node.querySelectorAll ? node.querySelectorAll(sel) : null;"
      "  const ids = [];"
      "  const limit = " + std::to_string(MAX_DOM_QUERY_SELECTOR_ALL_RESULTS) + ";"
      "  if (list && typeof list.length === 'number') {"
      "    for (let i = 0; i < list.length && i < limit; i++) {"
      "      ids.push(__oro.getOrCreateNodeId(list[i]));"
      "    }"
      "  }"
      "  return JSON.stringify({ ok: true, nodeIds: ids });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeIds", JSON::Array::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("nodeIds")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeIds", parsedObj.get("nodeIds")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"nodeIds", JSON::Array::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleDOMDescribeNode (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectId = jsonStringOrEmpty(params, "objectId");
    int depth = jsonIntOr(params, "depth", 1);
    if (depth < 0) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    } else if (depth > MAX_DOM_SERIALIZE_DEPTH) {
      depth = MAX_DOM_SERIALIZE_DEPTH;
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const bool hasNodeRef = resolvedNodeId > 0 || !objectId.empty();
    const auto objectIdLiteral = JSON::String(objectId).str();

    const auto nodeExpr = hasNodeRef
      ? std::string(
        "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
        "  if (!node) return JSON.stringify({ ok: false, error: { message: 'Node not found' } });"
        "  const out = __oro.serializeNode(node, " + std::to_string(depth) + ");"
      )
      : std::string(
        "  const out = __oro.serializeNode(document, " + std::to_string(depth) + ");"
      );

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
    ) + nodeExpr + std::string(
      "  return JSON.stringify({ ok: true, node: out });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"node", JSON::Object::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("node") && !parsedObj.get("node").isNull()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"node", parsedObj.get("node")} }, sessionId);
          return;
        }

        if (parsedObj.has("error")) {
          const auto message = jsonStringOrEmpty(parsedObj.get("error"), "message");
          if (!message.empty()) {
            this->sendCDPError(client, id, -32000, message, sessionId);
            return;
          }
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"node", JSON::Object::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleDOMResolveNode (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectGroup = jsonStringOrEmpty(params, "objectGroup");
    const auto objectId = jsonStringOrEmpty(params, "objectId");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectGroupLiteral = JSON::String(objectGroup).str();
    const auto objectIdLiteral = JSON::String(objectId).str();
    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
      "  const obj = __oro.toRemoteObject(node, { returnByValue: false, objectGroup: " + objectGroupLiteral + " });"
      "  return JSON.stringify({ ok: true, object: obj });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"object", JSON::Object::Entries { {"type", "undefined"} }}
          }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("object")) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"object", parsedObj.get("object")} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"object", JSON::Object::Entries { {"type", "undefined"} }}
        }, sessionId);
      });
    });
  }

  void CDP::handleDOMGetContentQuads (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectId = jsonStringOrEmpty(params, "objectId");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const bool hasNodeRef = resolvedNodeId > 0 || !objectId.empty();
    const auto objectIdLiteral = JSON::String(objectId).str();

    const auto nodeExpr = hasNodeRef
      ? std::string(
        "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
        "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
        "  if (!el || !el.getClientRects) return JSON.stringify({ ok: false, error: { message: 'Node does not have a layout object' } });"
        "  if (!el.isConnected) return JSON.stringify({ ok: false, error: { message: 'Node is detached from document' } });"
        "  try { if (el.getClientRects().length === 0) return JSON.stringify({ ok: false, error: { message: 'Node does not have a layout object' } }); } catch (e) {}"
        "  const r = el.getBoundingClientRect();"
        "  const quad = [r.left, r.top, r.right, r.top, r.right, r.bottom, r.left, r.bottom];"
        "  return JSON.stringify({ ok: true, quads: [quad] });"
      )
      : std::string(
        "  return JSON.stringify({ ok: false, error: { message: 'Missing nodeId' } });"
      );

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
    ) + nodeExpr + std::string(
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"quads", JSON::Array::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("quads") && parsedObj.get("quads").isArray()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"quads", parsedObj.get("quads")} }, sessionId);
          return;
        }

        if (parsedObj.has("error")) {
          const auto message = jsonStringOrEmpty(parsedObj.get("error"), "message");
          if (!message.empty()) {
            this->sendCDPError(client, id, -32000, message, sessionId);
            return;
          }
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"quads", JSON::Array::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleDOMGetNodeForLocation (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    if (!params.isObject()) {
      return this->sendCDPError(client, id, -32602, "Missing x/y", sessionId);
    }

    const auto& obj = params.as<JSON::Object>();
    if (!obj.has("x") || !obj.has("y")) {
      return this->sendCDPError(client, id, -32602, "Missing x/y", sessionId);
    }

    const double x = jsonDoubleOr(params, "x", 0.0);
    const double y = jsonDoubleOr(params, "y", 0.0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const x = " + std::to_string(x) + ";"
      "  const y = " + std::to_string(y) + ";"
      "  const el = document.elementFromPoint(x, y) || document.body || document.documentElement;"
      "  const nodeId = __oro.getOrCreateNodeId(el);"
      "  return JSON.stringify({ ok: true, nodeId });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId, targetId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, targetId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPError(client, id, -32000, "Node not found", sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("nodeId")) {
          const int foundNodeId = jsonIntOr(parsed, "nodeId", 0);
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"backendNodeId", JSON::Number(foundNodeId)},
            {"nodeId", JSON::Number(foundNodeId)},
            {"frameId", targetId}
          }, sessionId);
          return;
        }

        this->sendCDPError(client, id, -32000, "Node not found", sessionId);
      });
    });
  }

  void CDP::handleDOMGetBoxModel (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectId = jsonStringOrEmpty(params, "objectId");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const auto objectIdLiteral = JSON::String(objectId).str();
    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
      "  const bm = __oro.getBoxModel(node);"
      "  if (!bm) return JSON.stringify({ ok: false, error: { message: 'No box model' } });"
      "  return JSON.stringify({ ok: true, bm });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPError(client, id, -32000, "No box model", sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& parsedObj = parsed.as<JSON::Object>();
        if (parseBool(parsedObj.get("ok"), false) && parsedObj.has("bm") && parsedObj.get("bm").isObject()) {
          this->sendCDPResult(client, id, parsedObj.get("bm"), sessionId);
          return;
        }

        this->sendCDPError(client, id, -32000, "No box model", sessionId);
      });
    });
  }

  void CDP::handleCSSGetComputedStyleForNode (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
      "  const style = (el && typeof getComputedStyle === 'function') ? getComputedStyle(el) : null;"
      "  const out = [];"
      "  const limit = " + std::to_string(MAX_RUNTIME_PROPERTY_DESCRIPTORS) + ";"
      "  if (style) {"
      "    try {"
      "      for (let i = 0; i < style.length && out.length < limit; i++) {"
      "        const name = String(style[i] || '');"
      "        if (!name) continue;"
      "        let value = '';"
      "        try { value = String(style.getPropertyValue(name) || ''); } catch (e2) { value = ''; }"
      "        out.push({ name, value });"
      "      }"
      "    } catch (e) {}"
      "  }"
      "  return JSON.stringify({ ok: true, computedStyle: out });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, computedStyle: [] });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (!parsedOpt.has_value() || !parsedOpt.value().isObject()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"computedStyle", JSON::Array::Entries {}} }, sessionId);
          return;
        }

        const auto parsed = parsedOpt.value();
        const auto& obj = parsed.as<JSON::Object>();
        const auto computed = obj.get("computedStyle");
        if (parseBool(obj.get("ok"), false) && computed.isArray()) {
          this->sendCDPResult(client, id, JSON::Object::Entries { {"computedStyle", computed} }, sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries { {"computedStyle", JSON::Array::Entries {}} }, sessionId);
      });
    });
  }

  void CDP::handleCSSGetInlineStylesForNode (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
      "  let cssText = '';"
      "  try {"
      "   if (el && el.getAttribute) cssText = String(el.getAttribute('style') || '');"
      "   if (!cssText && el && el.style && el.style.cssText) cssText = String(el.style.cssText || '');"
      "  } catch (e) { cssText = ''; }"
      "  return JSON.stringify({ ok: true, cssText });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, cssText: '' });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        String cssText;
        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto cssAny = obj.get("cssText");
          if (parseBool(obj.get("ok"), false) && cssAny.isString()) {
            cssText = cssAny.as<JSON::String>().value();
          }
        }

        const auto style = JSON::Object::Entries {
          {"cssProperties", JSON::Array::Entries {}},
          {"shorthandEntries", JSON::Array::Entries {}},
          {"cssText", cssText}
        };

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"inlineStyle", style},
          {"attributesStyle", JSON::Object::Entries {
            {"cssProperties", JSON::Array::Entries {}},
            {"shorthandEntries", JSON::Array::Entries {}},
            {"cssText", ""}
          }}
        }, sessionId);
      });
    });
  }

  void CDP::handleCSSGetMatchedStylesForNode (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.getNode(" + std::to_string(nodeId) + ");"
      "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
      "  let cssText = '';"
      "  try {"
      "   if (el && el.getAttribute) cssText = String(el.getAttribute('style') || '');"
      "   if (!cssText && el && el.style && el.style.cssText) cssText = String(el.style.cssText || '');"
      "  } catch (e) { cssText = ''; }"
      "  return JSON.stringify({ ok: true, cssText });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, cssText: '' });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        String cssText;
        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto cssAny = obj.get("cssText");
          if (parseBool(obj.get("ok"), false) && cssAny.isString()) {
            cssText = cssAny.as<JSON::String>().value();
          }
        }

        const auto style = JSON::Object::Entries {
          {"cssProperties", JSON::Array::Entries {}},
          {"shorthandEntries", JSON::Array::Entries {}},
          {"cssText", cssText}
        };

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"inlineStyle", style},
          {"attributesStyle", JSON::Object::Entries {
            {"cssProperties", JSON::Array::Entries {}},
            {"shorthandEntries", JSON::Array::Entries {}},
            {"cssText", ""}
          }},
          {"matchedCSSRules", JSON::Array::Entries {}},
          {"pseudoElements", JSON::Array::Entries {}},
          {"inherited", JSON::Array::Entries {}},
          {"cssKeyframesRules", JSON::Array::Entries {}}
        }, sessionId);
      });
    });
  }

  void CDP::handleDOMScrollIntoViewIfNeeded (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectId = jsonStringOrEmpty(params, "objectId");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const auto objectIdLiteral = JSON::String(objectId).str();

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
      "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
      "  if (!el || !el.getClientRects) return JSON.stringify({ ok: false, error: { message: 'Node does not have a layout object' } });"
      "  if (!el.isConnected) return JSON.stringify({ ok: false, error: { message: 'Node is detached from document' } });"
      "  try { if (el.getClientRects().length === 0) return JSON.stringify({ ok: false, error: { message: 'Node does not have a layout object' } }); } catch (e) {}"
      "  try {"
      "    if (el.scrollIntoView) el.scrollIntoView({ block: 'center', inline: 'center' });"
      "  } catch (e) {"
      "    try { if (el.scrollIntoView) el.scrollIntoView(); } catch (e2) {}"
      "  }"
      "  return JSON.stringify({ ok: true });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          if (parseBool(obj.get("ok"), false)) {
            this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
            return;
          }

          const auto errAny = obj.get("error");
          const auto message = jsonStringOrEmpty(errAny, "message");
          if (!message.empty()) {
            this->sendCDPError(client, id, -32000, message, sessionId);
            return;
          }
        }

        this->sendCDPError(client, id, -32000, "Scroll failed", sessionId);
      });
    });
  }

  void CDP::handleDOMFocus (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int nodeId = jsonIntOr(params, "nodeId", 0);
    const int backendNodeId = jsonIntOr(params, "backendNodeId", 0);
    const auto objectId = jsonStringOrEmpty(params, "objectId");

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const int resolvedNodeId = nodeId > 0 ? nodeId : backendNodeId;
    const auto objectIdLiteral = JSON::String(objectId).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const node = __oro.resolveNodeRef({ nodeId: " + std::to_string(resolvedNodeId) + ", objectId: " + objectIdLiteral + " });"
      "  const el = node && node.nodeType === 1 ? node : (node && node.parentElement ? node.parentElement : null);"
      "  if (el && el.focus) el.focus();"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleDOMGetFrameOwner (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto frameId = jsonStringOrEmpty(params, "frameId");
    if (frameId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing frameId", sessionId);
    }

    const auto marker = std::string(":frame:");
    const auto frameIdStr = std::string(frameId);
    const auto pos = frameIdStr.rfind(marker);
    if (pos == std::string::npos) {
      return this->sendCDPError(client, id, -32000, "Frame with the given id was not found.", sessionId);
    }

    int nodeId = 0;
    try {
      nodeId = std::stoi(frameIdStr.substr(pos + marker.size()));
    } catch (...) {
      nodeId = 0;
    }

    if (nodeId <= 0) {
      return this->sendCDPError(client, id, -32000, "Frame with the given id was not found.", sessionId);
    }

    this->sendCDPResult(client, id, JSON::Object::Entries {
      {"backendNodeId", JSON::Number(nodeId)},
      {"nodeId", JSON::Number(nodeId)}
    }, sessionId);
  }

  void CDP::handleDOMSetFileInputFiles (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto objectId = jsonStringOrEmpty(params, "objectId");
    if (objectId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing objectId", sessionId);
    }

    JSON::Array::Entries filesJson;
    if (params.isObject()) {
      const auto filesAny = params.as<JSON::Object>().get("files");
      if (filesAny.isArray()) {
        for (const auto& entry : filesAny.as<JSON::Array>()) {
          if (!entry.isString()) continue;
          const auto p = entry.as<JSON::String>().value();
          filesJson.push_back(p);
        }
      }
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto objectIdLiteral = JSON::String(objectId).str();
    const auto filesLiteral = JSON::Any(filesJson).str();

    const auto wrapper = std::string("(async () => {") + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  const __objectId = " + objectIdLiteral + ";"
      "  const __paths = " + filesLiteral + ";"
      "  const input = __oro && __oro.store && __oro.store.objects ? __oro.store.objects.get(__objectId) : null;"
      "  if (!input) throw new Error('Input element not found');"
      "  if (String(input.tagName || '').toUpperCase() !== 'INPUT') throw new Error('Not an <input> element');"
      "  if (String(input.type || '').toLowerCase() !== 'file') throw new Error('Not a file input');"
      "  const mod = await import('oro:fs/promises');"
      "  const fs = mod && (mod.default || mod);"
      "  if (!fs || typeof fs.readFile !== 'function') throw new Error('Filesystem unavailable');"
      "  if (typeof DataTransfer !== 'function') throw new Error('DataTransfer unavailable');"
      "  const dt = new DataTransfer();"
      "  const paths = Array.isArray(__paths) ? __paths : [];"
      "  for (let i = 0; i < paths.length; i++) {"
      "    const p = String(paths[i] || '');"
      "    if (!p) continue;"
      "    const data = await fs.readFile(p);"
      "    let name = p;"
      "    try { name = String(p).split(/\\\\/).pop() || name; } catch (e) {}"
      "    try { name = String(name).split('/').pop() || name; } catch (e) {}"
      "    if (!name) name = 'file';"
      "    const file = new File([data], name);"
      "    dt.items.add(file);"
      "  }"
      "  try {"
      "    input.files = dt.files;"
      "  } catch (e) {"
      "    try { Object.defineProperty(input, 'files', { configurable: true, value: dt.files }); } catch (e2) {}"
      "  }"
      "  try { input.dispatchEvent(new Event('input', { bubbles: true })); } catch (e) {}"
      "  try { input.dispatchEvent(new Event('change', { bubbles: true })); } catch (e) {}"
      "  return JSON.stringify({ ok: true });"
      " } catch (e) {"
      "  return JSON.stringify({ ok: false, error: { message: String(e && e.message || e) } });"
      " }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;
    window->eval(wrapper, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          if (parseBool(obj.get("ok"), false)) {
            this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
            return;
          }

          const auto errAny = obj.get("error");
          const auto message = jsonStringOrEmpty(errAny, "message");
          if (!message.empty()) {
            this->sendCDPError(client, id, -32000, message, sessionId);
            return;
          }
        }

        this->sendCDPError(client, id, -32000, "Failed to set input files", sessionId);
      });
    });
  }

  void CDP::handleInputDispatchMouseEvent (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto type = jsonStringOrEmpty(params, "type");
    if (type.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing type", sessionId);
    }

    const double x = jsonDoubleOr(params, "x", 0.0);
    const double y = jsonDoubleOr(params, "y", 0.0);
    auto button = jsonStringOrEmpty(params, "button");
    if (button.empty()) button = "left";
    const int clickCount = jsonIntOr(params, "clickCount", 1);
    const double deltaX = jsonDoubleOr(params, "deltaX", 0.0);
    const double deltaY = jsonDoubleOr(params, "deltaY", 0.0);
    const int modifiers = jsonIntOr(params, "modifiers", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto typeLiteral = JSON::String(type).str();
    const auto buttonLiteral = JSON::String(button).str();

    const auto script = std::string(
      "(function(){"
      " try {"
      "  const type = " + typeLiteral + ";"
      "  const button = " + buttonLiteral + ";"
      "  const x = " + std::to_string(x) + ";"
      "  const y = " + std::to_string(y) + ";"
      "  const clickCount = " + std::to_string(clickCount) + ";"
      "  const deltaX = " + std::to_string(deltaX) + ";"
      "  const deltaY = " + std::to_string(deltaY) + ";"
      "  const modifiers = " + std::to_string(modifiers) + ";"
      "  const altKey = (modifiers & 1) !== 0;"
      "  const ctrlKey = (modifiers & 2) !== 0;"
      "  const metaKey = (modifiers & 4) !== 0;"
      "  const shiftKey = (modifiers & 8) !== 0;"
      "  const el = document.elementFromPoint(x, y) || document.body || document.documentElement;"
      "  const state = globalThis.__oro_cdp_input || (globalThis.__oro_cdp_input = { pressed: null });"
      "  const buttonMap = { left: 0, middle: 1, right: 2 };"
      "  const btn = Object.prototype.hasOwnProperty.call(buttonMap, button) ? buttonMap[button] : 0;"
      "  const common = { bubbles: true, cancelable: true, clientX: x, clientY: y, button: btn, detail: clickCount, altKey, ctrlKey, metaKey, shiftKey };"
      "  const dispatchPointer = (name) => {"
      "    try {"
      "      if (typeof PointerEvent !== 'function') return;"
      "      el.dispatchEvent(new PointerEvent(name, {"
      "        bubbles: true, cancelable: true, clientX: x, clientY: y, button: btn, altKey, ctrlKey, metaKey, shiftKey,"
      "        pointerId: 1, pointerType: 'mouse', isPrimary: true"
      "      }));"
      "    } catch (e) {}"
      "  };"
      "  if (type === 'mouseMoved') {"
      "    dispatchPointer('pointermove');"
      "    el.dispatchEvent(new MouseEvent('mousemove', common));"
      "  } else if (type === 'mousePressed') {"
      "    state.pressed = el;"
      "    try { if (el && el.focus) el.focus(); } catch (e) {}"
      "    dispatchPointer('pointerdown');"
      "    el.dispatchEvent(new MouseEvent('mousedown', common));"
      "  } else if (type === 'mouseReleased') {"
      "    const pressed = state.pressed || el;"
      "    state.pressed = null;"
      "    dispatchPointer('pointerup');"
      "    el.dispatchEvent(new MouseEvent('mouseup', common));"
      "    if (btn === 0 && pressed && typeof pressed.click === 'function') {"
      "      try { pressed.click(); } catch (e) { pressed.dispatchEvent(new MouseEvent('click', common)); }"
      "    } else {"
      "      el.dispatchEvent(new MouseEvent('click', common));"
      "    }"
      "  } else if (type === 'mouseWheel') {"
      "    try { window.scrollBy(deltaX, deltaY); } catch (e) {}"
      "    el.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, clientX: x, clientY: y, button: btn, detail: clickCount, deltaX, deltaY, altKey, ctrlKey, metaKey, shiftKey }));"
      "  }"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleInputDispatchKeyEvent (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto type = jsonStringOrEmpty(params, "type");
    if (type.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing type", sessionId);
    }

    const auto key = jsonStringOrEmpty(params, "key");
    const auto code = jsonStringOrEmpty(params, "code");
    const auto text = jsonStringOrEmpty(params, "text");
    const int modifiers = jsonIntOr(params, "modifiers", 0);
    const bool autoRepeat = params.isObject()
      ? parseBool(params.as<JSON::Object>().get("autoRepeat"), false)
      : false;
    const int location = jsonIntOr(params, "location", 0);

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto keySid = std::string(sessionId);
      if (client->sessions.contains(keySid)) {
        targetId = client->sessions.at(keySid).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto typeLiteral = JSON::String(type).str();
    const auto keyLiteral = JSON::String(key).str();
    const auto codeLiteral = JSON::String(code).str();
    const auto textLiteral = JSON::String(text).str();
    const auto repeatLiteral = autoRepeat ? "true" : "false";

    const auto script = std::string(
      "(function(){"
      " try {"
      "  const type = " + typeLiteral + ";"
      "  const key = " + keyLiteral + ";"
      "  const code = " + codeLiteral + ";"
      "  const text = " + textLiteral + ";"
      "  const modifiers = " + std::to_string(modifiers) + ";"
      "  const altKey = (modifiers & 1) !== 0;"
      "  const ctrlKey = (modifiers & 2) !== 0;"
      "  const metaKey = (modifiers & 4) !== 0;"
      "  const shiftKey = (modifiers & 8) !== 0;"
      "  const repeat = " + std::string(repeatLiteral) + ";"
      "  const location = " + std::to_string(location) + ";"
      "  const target = document.activeElement || document.body || document.documentElement;"
      "  const init = { bubbles: true, cancelable: true, key, code, altKey, ctrlKey, metaKey, shiftKey, repeat, location };"
      "  if (type === 'keyUp') {"
      "    target.dispatchEvent(new KeyboardEvent('keyup', init));"
      "    return;"
      "  }"
      "  if (type === 'char') {"
      "    if (text) {"
      "      try {"
      "        if (document.execCommand) {"
      "          document.execCommand('insertText', false, text);"
      "        }"
      "      } catch (e) {}"
      "    }"
      "    target.dispatchEvent(new KeyboardEvent('keypress', Object.assign({}, init, { key: key || text })));"
      "    return;"
      "  }"
      "  target.dispatchEvent(new KeyboardEvent('keydown', init));"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleInputInsertText (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto text = jsonStringOrEmpty(params, "text");
    if (text.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing text", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto keySid = std::string(sessionId);
      if (client->sessions.contains(keySid)) {
        targetId = client->sessions.at(keySid).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto textLiteral = JSON::String(text).str();
    const auto script = std::string(
      "(function(){"
      " try {"
      "  const text = " + textLiteral + ";"
      "  const el = document.activeElement;"
      "  if (el && typeof el.value === 'string' && typeof el.setRangeText === 'function') {"
      "    try {"
      "      const start = typeof el.selectionStart === 'number' ? el.selectionStart : el.value.length;"
      "      const end = typeof el.selectionEnd === 'number' ? el.selectionEnd : el.value.length;"
      "      el.setRangeText(text, start, end, 'end');"
      "      el.dispatchEvent(new Event('input', { bubbles: true }));"
      "      el.dispatchEvent(new Event('change', { bubbles: true }));"
      "      return;"
      "    } catch (e) {}"
      "  }"
      "  if (document.execCommand) {"
      "    document.execCommand('insertText', false, text);"
      "  }"
      " } catch (e) {}"
      "})();"
    );

    window->eval(script);
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleEmulationSetDeviceMetricsOverride (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    if (!params.isObject()) {
      return this->sendCDPError(client, id, -32602, "Missing width/height", sessionId);
    }

    const auto& obj = params.as<JSON::Object>();
    if (!obj.has("width") || !obj.has("height")) {
      return this->sendCDPError(client, id, -32602, "Missing width/height", sessionId);
    }

    const int width = jsonIntOr(params, "width", 0);
    const int height = jsonIntOr(params, "height", 0);

    if (width <= 0 || height <= 0) {
      return this->sendCDPError(client, id, -32602, "Invalid width/height", sessionId);
    }

    if (!screenshotDimensionsAllowed(static_cast<int64_t>(width), static_cast<int64_t>(height))) {
      return this->sendCDPError(client, id, -32000, "Window size too large", sessionId);
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      const bool ok = [&]() {
        auto window = app->runtime.windowManager.getWindow(windowIndex);
        if (!window) {
          return false;
        }
        window->setSize(width, height);
        return true;
      }();

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleEmulationClearDeviceMetricsOverride (Client* client, int id, const String& sessionId) {
    // No-op: restoring to previous size is not well-defined in Oro.
    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
  }

  void CDP::handleBrowserGetVersion (Client* client, int id, const String& sessionId) {
    const auto ua = String("OroRuntime/") + runtime::version::VERSION_STRING;
    const auto result = JSON::Object::Entries {
      {"protocolVersion", "1.3"},
      {"product", ua},
      {"revision", runtime::version::VERSION_HASH_STRING},
      {"userAgent", ua},
      {"jsVersion", "0"}
    };
    this->sendCDPResult(client, id, result, sessionId);
  }

  void CDP::handleBrowserGetWindowForTarget (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    String targetId = jsonStringOrEmpty(params, "targetId");

    if (targetId.empty()) {
      if (client->kind == Client::Kind::PageWS) {
        targetId = client->boundTargetId;
      } else if (!sessionId.empty()) {
        const auto key = std::string(sessionId);
        if (client->sessions.contains(key)) {
          targetId = client->sessions.at(key).targetId;
        }
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No target", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    auto app = App::sharedApplication();
    if (!maybeWindow.has_value() || !app) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      window::Window::Size size;
      bool ok = false;
      if (auto window = app->runtime.windowManager.getWindow(windowIndex)) {
        size = window->getSize();
        ok = true;
      }

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"windowId", windowIndex},
          {"bounds", JSON::Object::Entries {
            {"left", 0},
            {"top", 0},
            {"width", size.width},
            {"height", size.height},
            {"windowState", "normal"}
          }}
        }, sessionId);
      });
    });
  }

  void CDP::handleBrowserGetWindowBounds (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    int windowId = jsonIntOr(params, "windowId", -1);

    if (windowId < 0) {
      // Accept targetId as a convenience (some tooling uses it interchangeably).
      String targetId = jsonStringOrEmpty(params, "targetId");
      if (targetId.empty()) {
        if (client->kind == Client::Kind::PageWS) {
          targetId = client->boundTargetId;
        } else if (!sessionId.empty()) {
          const auto key = std::string(sessionId);
          if (client->sessions.contains(key)) {
            targetId = client->sessions.at(key).targetId;
          }
        }
      }

      if (!targetId.empty()) {
        const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
        if (maybeWindow.has_value()) {
          windowId = maybeWindow.value();
        }
      }
    }

    if (windowId < 0) {
      return this->sendCDPError(client, id, -32602, "Invalid windowId", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      window::Window::Size size;
      bool ok = false;
      if (auto window = app->runtime.windowManager.getWindow(windowId)) {
        size = window->getSize();
        ok = true;
      }

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"bounds", JSON::Object::Entries {
            {"left", 0},
            {"top", 0},
            {"width", size.width},
            {"height", size.height},
            {"windowState", "normal"}
          }}
        }, sessionId);
      });
    });
  }

  void CDP::handleBrowserSetContentsSize (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int windowId = jsonIntOr(params, "windowId", -1);
    const int width = jsonIntOr(params, "width", 0);
    const int height = jsonIntOr(params, "height", 0);

    if (windowId < 0 || width <= 0 || height <= 0) {
      return this->sendCDPError(client, id, -32602, "Invalid windowId/width/height", sessionId);
    }

    if (!screenshotDimensionsAllowed(static_cast<int64_t>(width), static_cast<int64_t>(height))) {
      return this->sendCDPError(client, id, -32000, "Window size too large", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      bool ok = false;
      if (auto window = app->runtime.windowManager.getWindow(windowId)) {
        window->setSize(width, height);
        ok = true;
      }

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleBrowserSetWindowBounds (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const int windowId = jsonIntOr(params, "windowId", -1);
    if (windowId < 0 || !params.isObject()) {
      return this->sendCDPError(client, id, -32602, "Invalid windowId", sessionId);
    }

    const auto boundsAny = params.as<JSON::Object>().get("bounds");
    if (!boundsAny.isObject()) {
      return this->sendCDPError(client, id, -32602, "Missing bounds", sessionId);
    }

    const auto& bounds = boundsAny.as<JSON::Object>();
    const bool hasLeft = bounds.has("left");
    const bool hasTop = bounds.has("top");
    const bool hasWidth = bounds.has("width");
    const bool hasHeight = bounds.has("height");

    const int left = jsonIntOr(boundsAny, "left", 0);
    const int top = jsonIntOr(boundsAny, "top", 0);
    const int width = jsonIntOr(boundsAny, "width", 0);
    const int height = jsonIntOr(boundsAny, "height", 0);
    const auto windowState = jsonStringOrEmpty(boundsAny, "windowState");

    if (hasWidth && hasHeight && width > 0 && height > 0) {
      if (!screenshotDimensionsAllowed(static_cast<int64_t>(width), static_cast<int64_t>(height))) {
        return this->sendCDPError(client, id, -32000, "Window size too large", sessionId);
      }
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    app->dispatch([=, this]() {
      bool ok = false;
      if (auto window = app->runtime.windowManager.getWindow(windowId)) {
        ok = true;

        if (hasWidth && hasHeight && width > 0 && height > 0) {
          window->setSize(width, height);
        }

        if (hasLeft && hasTop) {
          window->setPosition(static_cast<float>(left), static_cast<float>(top));
        }

        if (windowState == "minimized") {
          window->minimize();
        } else if (windowState == "maximized") {
          window->maximize();
        } else if (windowState == "normal") {
          window->restore();
        }
      }

      this->loop.dispatch([=, this]() {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) return;

        if (!ok) {
          this->sendCDPError(client, id, -32000, "Window not found", sessionId);
          return;
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
      });
    });
  }

  void CDP::handleNetworkGetResponseBody (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto requestId = jsonStringOrEmpty(params, "requestId");
    if (requestId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing requestId", sessionId);
    }

    // Prefer native-instrumented payloads (SchemeHandlers) when present.
    {
      StoredNetworkPayload payload;
      bool found = false;
      {
        Lock lock(this->mutex);
        const auto key = std::string(requestId);
        if (this->nativeNetworkPayloads.contains(key)) {
          payload = this->nativeNetworkPayloads.at(key);
          found = true;
        }
      }

      if (found) {
        auto body = payload.body;
        const bool base64Encoded = payload.base64Encoded;

        const size_t maxChars = base64Encoded
          ? MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS
          : MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES;

        if (body.size() > maxChars) {
          if (base64Encoded) {
            body = "";
          } else {
            body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
          }
        }

        this->sendCDPResult(client, id, JSON::Object::Entries {
          {"body", body},
          {"base64Encoded", base64Encoded}
        }, sessionId);
        return;
      }
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    const auto windowIndex = maybeWindow.value();
    auto window = app->runtime.windowManager.getWindow(windowIndex);
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const bool isNavRequest = [&]() {
      Lock lock(this->mutex);
      return (
        this->windowLastNavigationRequestId.contains(windowIndex) &&
        this->windowLastNavigationRequestId.at(windowIndex) == std::string(requestId)
      );
    }();

    const auto requestIdLiteral = JSON::String(requestId).str();
    const auto getBodyScript = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  if (!__oro || !__oro.net || typeof __oro.net.getResponseBody !== 'function') return null;"
      "  return __oro.net.getResponseBody(" + requestIdLiteral + ");"
      " } catch (e) { return null; }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

    window->eval(getBodyScript, [this, app, stream, clientId, id, sessionId, isNavRequest, window, requestIdLiteral](const JSON::Any& value) mutable {
      this->loop.dispatch([this, app, stream, clientId, id, sessionId, isNavRequest, window, requestIdLiteral, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto bodyAny = obj.get("body");
          const auto base64Any = obj.get("base64Encoded");
          auto body = bodyAny.isString() ? bodyAny.as<JSON::String>().value() : "";
          bool base64Encoded = parseBool(base64Any, false);

          const size_t maxChars = base64Encoded
            ? MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS
            : MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES;

          if (body.size() > maxChars) {
            if (base64Encoded) {
              // Truncating base64 would corrupt it; omit instead.
              body = "";
            } else {
              body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
            }
          }

          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"body", body},
            {"base64Encoded", base64Encoded}
          }, sessionId);
          return;
        }

        if (!isNavRequest) {
          // Unknown request id: return empty body rather than error.
          this->sendCDPResult(client, id, JSON::Object::Entries {
            {"body", ""},
            {"base64Encoded", false}
          }, sessionId);
          return;
        }

        // Fallback for navigation responses: use current document HTML.
        const auto htmlScript = std::string(
          "(function(){"
        ) + ORO_CDP_BOOTSTRAP + std::string(
          " try {"
          "  const max = " + std::to_string(MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES) + ";"
          "  let body = String(document.documentElement && document.documentElement.outerHTML || '');"
          "  if (typeof body === 'string' && body.length > max) body = body.slice(0, max);"
          "  return JSON.stringify({ body, base64Encoded: false });"
          " } catch (e) {"
          "  return JSON.stringify({ body: '', base64Encoded: false });"
          " }"
          "})()"
        );

        window->eval(htmlScript, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& htmlValue) mutable {
          this->loop.dispatch([this, app, stream, clientId, id, sessionId, htmlValue]() mutable {
            Client* client = nullptr;
            {
              Lock lock(this->mutex);
              if (this->clients.contains(stream)) {
                auto candidate = this->clients.at(stream);
                if (candidate && candidate->id == clientId) {
                  client = candidate;
                }
              }
            }

            if (!client || client->closing || client->closed) {
              return;
            }

            const auto parsedOpt = parseJSONResultString(htmlValue);
            if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
              const auto& obj = parsedOpt.value().as<JSON::Object>();
              const auto bodyAny = obj.get("body");
              const auto base64Any = obj.get("base64Encoded");
              auto body = bodyAny.isString() ? bodyAny.as<JSON::String>().value() : "";
              bool base64Encoded = parseBool(base64Any, false);

              const size_t maxChars = base64Encoded
                ? MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS
                : MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES;

              if (body.size() > maxChars) {
                if (base64Encoded) {
                  body = "";
                } else {
                  body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
                }
              }

              this->sendCDPResult(client, id, JSON::Object::Entries {
                {"body", body},
                {"base64Encoded", base64Encoded}
              }, sessionId);
              return;
            }

            this->sendCDPResult(client, id, JSON::Object::Entries {
              {"body", ""},
              {"base64Encoded", false}
            }, sessionId);
          });
        });
      });
    });
  }

	  void CDP::handleNetworkGetRequestPostData (Client* client, int id, const JSON::Any& params, const String& sessionId) {
    const auto requestId = jsonStringOrEmpty(params, "requestId");
    if (requestId.empty()) {
      return this->sendCDPError(client, id, -32602, "Missing requestId", sessionId);
    }

    // Prefer native-instrumented payloads (SchemeHandlers) when present.
    {
      StoredNetworkPayload payload;
      bool found = false;
      {
        Lock lock(this->mutex);
        const auto key = std::string(requestId);
        if (this->nativeNetworkPayloads.contains(key)) {
          payload = this->nativeNetworkPayloads.at(key);
          found = true;
        }
      }

      if (found) {
        auto postData = payload.postData;
        if (postData.size() > MAX_NETWORK_POSTDATA_RESULT_BYTES) {
          postData = "";
        }
        this->sendCDPResult(client, id, JSON::Object::Entries { {"postData", postData} }, sessionId);
        return;
      }
    }

    String targetId;

    if (client->kind == Client::Kind::PageWS) {
      targetId = client->boundTargetId;
    } else if (!sessionId.empty()) {
      const auto key = std::string(sessionId);
      if (client->sessions.contains(key)) {
        targetId = client->sessions.at(key).targetId;
      }
    }

    if (targetId.empty()) {
      return this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
    }

    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
    if (!maybeWindow.has_value()) {
      return this->sendCDPError(client, id, -32000, "Target not found", sessionId);
    }

    auto app = App::sharedApplication();
    if (!app) {
      return this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
    }

    auto window = app->runtime.windowManager.getWindow(maybeWindow.value());
    if (!window) {
      return this->sendCDPError(client, id, -32000, "Window not found", sessionId);
    }

    const auto requestIdLiteral = JSON::String(requestId).str();
    const auto script = std::string(
      "(function(){"
    ) + ORO_CDP_BOOTSTRAP + std::string(
      " try {"
      "  const __oro = globalThis.__oro_cdp;"
      "  if (!__oro || !__oro.net || typeof __oro.net.getRequestPostData !== 'function') return null;"
      "  return __oro.net.getRequestPostData(" + requestIdLiteral + ");"
      " } catch (e) { return null; }"
      "})()"
    );

    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
    const auto clientId = client->id;

	    window->eval(script, [this, app, window, stream, clientId, id, sessionId](const JSON::Any& value) mutable {
	      this->loop.dispatch([this, app, stream, clientId, id, sessionId, value]() mutable {
        Client* client = nullptr;
        {
          Lock lock(this->mutex);
          if (this->clients.contains(stream)) {
            auto candidate = this->clients.at(stream);
            if (candidate && candidate->id == clientId) {
              client = candidate;
            }
          }
        }

        if (!client || client->closing || client->closed) {
          return;
        }

        const auto parsedOpt = parseJSONResultString(value);
        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
          const auto& obj = parsedOpt.value().as<JSON::Object>();
          const auto postDataAny = obj.get("postData");
          auto postData = postDataAny.isString() ? postDataAny.as<JSON::String>().value() : "";
          if (postData.size() > MAX_NETWORK_POSTDATA_RESULT_BYTES) {
            postData = "";
          }
          this->sendCDPResult(client, id, JSON::Object::Entries { {"postData", postData} }, sessionId);
          return;
        }

	        this->sendCDPResult(client, id, JSON::Object::Entries { {"postData", ""} }, sessionId);
	      });
	    });
	  }

	  void CDP::handleTakeResponseBodyAsStream (
	    Client* client,
	    int id,
	    const JSON::Any& params,
	    const String& sessionId,
	    const char* requestIdKey
	  ) {
	    const auto keyName = requestIdKey ? requestIdKey : "requestId";
	    const auto requestId = jsonStringOrEmpty(params, keyName);
	    if (requestId.empty()) {
	      this->sendCDPError(client, id, -32602, std::string("Missing ") + keyName, sessionId);
	      return;
	    }

		    const auto createStream = [this](const String& body, bool base64Encoded) -> String {
		      const auto handle = String("oro-io:") + std::to_string(this->nextIOStreamId.fetch_add(1));
		      IOStream stream;
		      stream.base64Encoded = base64Encoded;
		      stream.offset = 0;
		      stream.data = base64Encoded ? bytes::base64::decode(body) : body;

		      {
		        Lock lock(this->mutex);
		        this->ioStreams.insert_or_assign(std::string(handle), stream);
		        this->ioStreamOrder.push_back(std::string(handle));
		        this->ioStreamTotalBytes += stream.data.size();

		        while (
		          this->ioStreamOrder.size() > MAX_IO_STREAM_ENTRIES ||
		          this->ioStreamTotalBytes > MAX_IO_STREAM_TOTAL_BYTES
		        ) {
	          const auto oldest = this->ioStreamOrder.front();
	          this->ioStreamOrder.pop_front();
	          if (this->ioStreams.contains(oldest)) {
	            const auto& oldEntry = this->ioStreams.at(oldest);
	            if (this->ioStreamTotalBytes >= oldEntry.data.size()) {
	              this->ioStreamTotalBytes -= oldEntry.data.size();
	            } else {
	              this->ioStreamTotalBytes = 0;
	            }
	            this->ioStreams.erase(oldest);
	          }
		        }
		      }

		      return handle;
		    };

	    // Prefer native-instrumented payloads (SchemeHandlers) when present.
	    {
	      StoredNetworkPayload payload;
	      bool found = false;
	      {
	        Lock lock(this->mutex);
	        const auto key = std::string(requestId);
	        if (this->nativeNetworkPayloads.contains(key)) {
	          payload = this->nativeNetworkPayloads.at(key);
	          found = true;
	        }
	      }

	      if (found) {
	        auto body = payload.body;
	        const bool base64Encoded = payload.base64Encoded;

		        const size_t maxChars = base64Encoded
		          ? MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS
		          : MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES;

		        if (body.size() > maxChars) {
		          if (base64Encoded) {
		            body = "";
	          } else {
	            body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
	          }
	        }

	        const auto handle = createStream(body, base64Encoded);
	        this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	        return;
	      }
	    }

	    String targetId;
	    if (client->kind == Client::Kind::PageWS) {
	      targetId = client->boundTargetId;
	    } else if (!sessionId.empty()) {
	      const auto key = std::string(sessionId);
	      if (client->sessions.contains(key)) {
	        targetId = client->sessions.at(key).targetId;
	      }
	    }

	    if (targetId.empty()) {
	      this->sendCDPError(client, id, -32000, "No attached target for session", sessionId);
	      return;
	    }

	    const auto maybeWindow = this->resolveWindowIndexForTarget(targetId);
	    if (!maybeWindow.has_value()) {
	      this->sendCDPError(client, id, -32000, "Target not found", sessionId);
	      return;
	    }

	    auto app = App::sharedApplication();
	    if (!app) {
	      this->sendCDPError(client, id, -32000, "Runtime unavailable", sessionId);
	      return;
	    }

	    const auto windowIndex = maybeWindow.value();
	    auto window = app->runtime.windowManager.getWindow(windowIndex);
	    if (!window) {
	      this->sendCDPError(client, id, -32000, "Window not found", sessionId);
	      return;
	    }

	    const bool isNavRequest = [&]() {
	      Lock lock(this->mutex);
	      return (
	        this->windowLastNavigationRequestId.contains(windowIndex) &&
	        this->windowLastNavigationRequestId.at(windowIndex) == std::string(requestId)
	      );
	    }();

	    const auto requestIdLiteral = JSON::String(requestId).str();
	    const auto getBodyScript = std::string(
	      "(function(){"
	    ) + ORO_CDP_BOOTSTRAP + std::string(
	      " try {"
	      "  const __oro = globalThis.__oro_cdp;"
	      "  if (!__oro || !__oro.net || typeof __oro.net.getResponseBody !== 'function') return null;"
	      "  return __oro.net.getResponseBody(" + requestIdLiteral + ");"
	      " } catch (e) { return null; }"
	      "})()"
	    );

	    const auto stream = reinterpret_cast<uv_stream_t*>(client->handle);
	    const auto clientId = client->id;

	    window->eval(getBodyScript, [this, stream, clientId, id, sessionId, isNavRequest, windowIndex, createStream](const JSON::Any& value) mutable {
	      this->loop.dispatch([this, stream, clientId, id, sessionId, isNavRequest, windowIndex, value, createStream]() mutable {
	        Client* client = nullptr;
	        {
	          Lock lock(this->mutex);
	          if (this->clients.contains(stream)) {
	            auto candidate = this->clients.at(stream);
	            if (candidate && candidate->id == clientId) {
	              client = candidate;
	            }
	          }
	        }

	        if (!client || client->closing || client->closed) {
	          return;
	        }

	        const auto parsedOpt = parseJSONResultString(value);
	        if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
	          const auto& obj = parsedOpt.value().as<JSON::Object>();
	          const auto bodyAny = obj.get("body");
	          const auto base64Any = obj.get("base64Encoded");
	          auto body = bodyAny.isString() ? bodyAny.as<JSON::String>().value() : "";
	          bool base64Encoded = parseBool(base64Any, false);

	          const size_t maxChars = base64Encoded
	            ? MAX_NETWORK_RESPONSE_BODY_RESULT_BASE64_CHARS
	            : MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES;

	          if (body.size() > maxChars) {
	            if (base64Encoded) {
	              body = "";
	            } else {
	              body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
	            }
	          }

	          const auto handle = createStream(body, base64Encoded);
	          this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	          return;
	        }

	        if (!isNavRequest) {
	          const auto handle = createStream("", false);
	          this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	          return;
	        }

	        auto app = App::sharedApplication();
	        if (!app) {
	          const auto handle = createStream("", false);
	          this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	          return;
	        }

	        auto window = app->runtime.windowManager.getWindow(windowIndex);
	        if (!window) {
	          const auto handle = createStream("", false);
	          this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	          return;
	        }

	        // Navigation fallback: use current document HTML.
	        const auto htmlScript = std::string(
	          "(function(){"
	        ) + ORO_CDP_BOOTSTRAP + std::string(
	          " try {"
	          "  const max = " + std::to_string(MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES) + ";"
	          "  let body = String(document.documentElement && document.documentElement.outerHTML || '');"
	          "  if (typeof body === 'string' && body.length > max) body = body.slice(0, max);"
	          "  return JSON.stringify({ body, base64Encoded: false });"
	          " } catch (e) {"
	          "  return JSON.stringify({ body: '', base64Encoded: false });"
	          " }"
	          "})()"
	        );

	        window->eval(htmlScript, [this, stream, clientId, id, sessionId, createStream](const JSON::Any& htmlValue) mutable {
	          this->loop.dispatch([this, stream, clientId, id, sessionId, htmlValue, createStream]() mutable {
	            Client* client = nullptr;
	            {
	              Lock lock(this->mutex);
	              if (this->clients.contains(stream)) {
	                auto candidate = this->clients.at(stream);
	                if (candidate && candidate->id == clientId) {
	                  client = candidate;
	                }
	              }
	            }

	            if (!client || client->closing || client->closed) {
	              return;
	            }

	            const auto parsedOpt = parseJSONResultString(htmlValue);
	            if (parsedOpt.has_value() && parsedOpt.value().isObject()) {
	              const auto& obj = parsedOpt.value().as<JSON::Object>();
	              const auto bodyAny = obj.get("body");
	              auto body = bodyAny.isString() ? bodyAny.as<JSON::String>().value() : "";
	              if (body.size() > MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES) {
	                body = body.substr(0, MAX_NETWORK_RESPONSE_BODY_RESULT_BYTES);
	              }
	              const auto handle = createStream(body, false);
	              this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	              return;
	            }

	            const auto handle = createStream("", false);
	            this->sendCDPResult(client, id, JSON::Object::Entries { {"stream", handle} }, sessionId);
	          });
	        });
	      });
	    });
	  }

		  void CDP::handleIORead (Client* client, int id, const JSON::Any& params, const String& sessionId) {
	    const auto handle = jsonStringOrEmpty(params, "handle");
	    if (handle.empty()) {
	      this->sendCDPError(client, id, -32602, "Missing handle", sessionId);
	      return;
	    }

	    bool hasOffset = false;
	    size_t offset = 0;
	    size_t size = MAX_IO_STREAM_READ_BYTES;

	    if (params.isObject()) {
	      const auto& obj = params.as<JSON::Object>();
	      const auto offsetAny = obj.get("offset");
	      if (offsetAny.isNumber()) {
	        const auto o = offsetAny.as<JSON::Number>().value();
	        if (o >= 0) {
	          hasOffset = true;
	          offset = static_cast<size_t>(o);
	        }
	      }
	      const auto sizeAny = obj.get("size");
	      if (sizeAny.isNumber()) {
	        const auto s = sizeAny.as<JSON::Number>().value();
	        if (s > 0) {
	          const auto desired = static_cast<size_t>(s);
	          size = desired < MAX_IO_STREAM_READ_BYTES ? desired : MAX_IO_STREAM_READ_BYTES;
	        }
	      }
	    }

	    String chunk = "";
	    bool base64Encoded = false;
	    bool eof = true;

		    bool found = false;
		    {
		      Lock lock(this->mutex);
		      const auto key = std::string(handle);
		      if (this->ioStreams.contains(key)) {
		        auto& stream = this->ioStreams.at(key);
		        found = true;
		        base64Encoded = stream.base64Encoded;

		        size_t start = hasOffset ? offset : stream.offset;
		        if (start > stream.data.size()) start = stream.data.size();
		        const size_t remaining = stream.data.size() - start;
		        const size_t take = remaining < size ? remaining : size;

		        if (take > 0) {
		          chunk.assign(stream.data.data() + start, take);
		        } else {
		          chunk = "";
		        }

		        stream.offset = start + take;
		        eof = stream.offset >= stream.data.size();
		      }
		    }

		    if (!found) {
		      this->sendCDPError(client, id, -32000, "Stream not found", sessionId);
		      return;
		    }

	    const auto out = base64Encoded ? bytes::base64::encode(chunk) : chunk;
	    this->sendCDPResult(client, id, JSON::Object::Entries {
	      {"data", out},
	      {"eof", JSON::Boolean(eof)},
	      {"base64Encoded", JSON::Boolean(base64Encoded)}
	    }, sessionId);
	  }

	  void CDP::handleIOClose (Client* client, int id, const JSON::Any& params, const String& sessionId) {
	    const auto handle = jsonStringOrEmpty(params, "handle");
	    if (!handle.empty()) {
	      Lock lock(this->mutex);
	      const auto key = std::string(handle);
	      if (this->ioStreams.contains(key)) {
	        const auto& s = this->ioStreams.at(key);
	        if (this->ioStreamTotalBytes >= s.data.size()) {
	          this->ioStreamTotalBytes -= s.data.size();
	        } else {
	          this->ioStreamTotalBytes = 0;
	        }
	        this->ioStreams.erase(key);
	      }

	      for (auto it = this->ioStreamOrder.begin(); it != this->ioStreamOrder.end(); ++it) {
	        if (*it == key) {
	          this->ioStreamOrder.erase(it);
	          break;
	        }
	      }
	    }

	    this->sendCDPResult(client, id, JSON::Object::Entries {}, sessionId);
	  }

	  void CDP::broadcastToAttachedTarget (
	    const String& targetId,
	    const Function<void(Client* client, const String& sessionId)>& fn
  ) {
    Lock lock(this->mutex);
    for (auto& entry : this->clients) {
      auto client = entry.second;
      if (!client || client->closing || client->closed) {
        continue;
      }

      // page websocket connection bound directly to target
      if (client->kind == Client::Kind::PageWS && client->boundTargetId == targetId) {
        fn(client, "");
        continue;
      }

      // browser websocket sessions
      if (client->kind == Client::Kind::BrowserWS) {
        for (const auto& kv : client->sessions) {
          const auto& sid = kv.second.sessionId;
          const auto& tid = kv.second.targetId;
          if (tid == targetId) {
            fn(client, sid);
          }
        }
      }
    }
  }
}
