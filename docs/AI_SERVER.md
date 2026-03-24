# Embedded LLaMA Server (AI, OpenAI-compatible)

The runtime embeds a llama.cpp-based server exposed via the in-process `oro:` scheme. There is no external listening socket; endpoints are available only inside the app.

## Endpoints (default prefix `/ai/llama`)

- `GET /health` — readiness + loaded models and basic metrics
- `GET /v1/models` — `{ object: "list", data: [{ id, object: "model" }] }`
- `POST/GET /v1/chat/completions` — OpenAI-compatible chat
  - SSE streaming: `?stream=true` or body `{ stream: true }`
- `POST/GET /v1/completions` — text completion
  - SSE streaming: `?stream=true` or body `{ stream: true }`
- `POST/GET /v1/embeddings` — vector for input
- Utilities: `POST/GET /tokenize`, `/detokenize`

## Model Loading (IPC)

```js
// Load a model by file name. Searches environment/userConfig paths.
await fetch('ipc://ai.llm.model.load?name=model.gguf')
// or provide `directory=/abs/path` explicitly
```

Search order for model files:

- `ORO_AI_LLM_MODEL_PATH` (env var: directory)
- userConfig key `ai_llm_model_path` (directory)
- explicit `directory` query parameter

## Request Semantics

- Location: `oro://<bundle>/<prefix>/<endpoint>`
- Content type: `application/json` supported; query params also accepted
- JSON body parse cap: 1 MiB (oversized bodies are not parsed)
- Prompt size cap: 128 KiB → 413
- Sampling defaults: taken from userConfig or server options when omitted
- Stop sequences:
  - Non-stream: output trimmed at earliest stop
  - Stream: rolling tail (size = longest stop) detects stop

### Tools/Function Calling (Stub)

When a request requires a function call (`tools` + required `tool_choice`), the server returns a minimal stub response:

- Non-stream: `choices[0].message.tool_calls[...]`, `finish_reason: "tool_calls"`
- Stream: a single chunk with `choices[0].delta.tool_calls[...]`, followed by `[DONE]`

Applications should interpret this and run their own vetted function implementation, then send a follow-up prompt.

## Configuration (userConfig)

- `ai_llm_server_prefix` (default `/ai/llama`)
- `ai_llm_default_model`
- `ai_llm_rate_limit_concurrency` (default 4)
- `ai_llm_rate_limit_rps` (0 = disabled)
- `ai_llm_rate_limit_burst` (0 = disabled)
- `ai_llm_default_max_tokens` (default 128)
- `ai_llm_default_temperature` (default 0.8)
- `ai_llm_default_top_p` (default 0.95)
- `ai_llm_default_top_k` (default 40)
- `ai_llm_default_min_p` (default 0.05)

Environment:

- `ORO_AI_LLM_MODEL_PATH` — directory containing models

## Metrics

`GET /health` returns metrics:

- `inflight`, `rateRPS`, `rateBurst`, `rateLimited`
- per-endpoint counts and average latencies (ms) for stream/non-stream chat and completions

## Security & Performance

- Internal-only exposure via `oro:`; no external ports
- Resource caps (prompt/JSON) to bound memory/CPU
- Concurrency and rate-limiting guards prevent overload; heavy jobs run off the UI thread
- SSE/chunk streams coalesce writes to reduce overhead
- Tool-calling does not execute arbitrary code; arguments are `{}` by default

## Testing Models in CI/Locally

- Optional env vars:
  - `ORO_AI_MODEL_NAME` (required)
  - `ORO_AI_MODEL_DIR` (optional directory)
- If set, E2E tests attempt to load the model and run basic streamed/non-stream chat checks.
