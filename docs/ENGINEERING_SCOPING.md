# Engineering Note: Scoped State, Metrics, and Limits

This note captures patterns for tracking metrics and enforcing limits across the runtime without introducing process‑wide behavior that can blur isolation between windows, origins, or embedded servers.

## Principles

- Avoid process‑wide global statics for counters or behavior (limits, caches) that affect runtime request handling.
- Scope state to a meaningful domain:
  - Per‑origin (e.g., `oro://com.app`) + route prefix (e.g., `/ai/llama`).
  - Per‑window when appropriate (bridge `client.id`) for finer attribution.
- Store scoped state in Services (e.g., `core::services::AI`) rather than in routes.
  - Services provide `getOrCreate(scopeKey)` APIs to retrieve state.
  - Scoped state should be atomic for hot paths; only the map lookup/insert needs a lock.

## Implementation Pattern

```cpp
// Example container
struct ServerStats {
  std::atomic<int> inflight{0};
  std::atomic<uint64_t> reqA{0}, reqB{0};
  std::atomic<long long> latASum{0}, latACount{0};
  std::atomic<int> tokens{0};
  std::atomic<long long> lastRefillMs{0};
  bool tryConsumeToken(int rps, int burst);
};

// Service state
Mutex statsMutex;
Map<String, SharedPointer<ServerStats>> stats;

SharedPointer<ServerStats> getStats(const String &scope) {
  Lock lock(statsMutex);
  if (stats.contains(scope)) return stats.at(scope);
  auto s = std::make_shared<ServerStats>();
  stats.insert_or_assign(scope, s);
  return s;
}
```

Routes:

- Build a scope key from `origin` + `prefix` (and optionally `client.id`).
- Retrieve stats once per request; update atomics in the hot path.
- Avoid heavy locks or per‑request allocations in streaming loops.

## Rate Limiting

- Use a token‑bucket per scope with steady‑clock refill (1s cadence is usually sufficient and cheap).
- Store `tokens` and `lastRefillMs` in the scoped stats; update atomics only.
- Enforce per‑scope concurrency (e.g., `inflight >= maxConcurrent`).
- Return a JSON error payload (e.g., 429/"Too Many Requests") when limits are exceeded.

## Metrics & /health

- Track counts and latency aggregates (sum + count) per scope.
- Report only the metrics for the scope serving the current request, not process‑global totals.
- Optionally include per‑window metrics alongside the aggregate origin+prefix view.
- Consider EWMA or percentiles if needed later; keep overhead minimal.

## Streaming (SSE/Chunks)

- Coalesce small writes to reduce syscall pressure.
- Track latency from start → finish for stream metrics.
- Respect cancellation; ensure resources (context, streams) are released.

## JSON Parsing & Caps

- Parse JSON bodies only when necessary and with a strict size cap (e.g., 1 MiB), falling back to query params.
- Avoid unbounded string growth; use small rolling windows for stop‑sequence detection.

## Tool‑Calling

- Do not execute arbitrary tool code in the runtime.
- Return a minimal tool_call stub (non‑stream) or a single stream chunk, then finish.
- Let applications resolve the tool function and post follow‑up prompts.

## Testing

- Add base tests that do not require external models (health/models/error paths, SSE shape, caps).
- Gate E2E tests that require a model behind env vars (e.g., `ORO_AI_MODEL_NAME` / `ORO_AI_MODEL_DIR`).

## Applying Broadly

- Where future services expose metrics/limits, follow the same scoping model (origin/prefix or window) and store the state in the appropriate Service.
- Keep hot paths atomic; lock only for map lookups/initialization.
