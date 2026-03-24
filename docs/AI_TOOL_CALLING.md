# Tool-Calling (Stub) Behavior

The embedded LLaMA server exposes OpenAI-compatible chat endpoints over the internal `oro:` scheme. For safety, when a request requires a function/tool call, the runtime does not execute arbitrary tool code. Instead, it returns a minimal, deterministic stub that applications can interpret and emulate client-side.

This is a frozen legacy limitation of this repository. Follow-up work belongs in
the new Oro Runtime repository; see
[Legacy Runtime Limitations](/home/werle/repos/oro-computer/legacy-runtime/docs/LEGACY_LIMITATIONS.md).

## When tool-calling stubs are used

- The request includes a `tools` array and a `tool_choice` that:
  - is `"required"`, or
  - an object specifying a particular function via `tool_choice.function.name`.

## What is returned

- Non-stream (`/v1/chat/completions`):

  ```json
  {
    "choices": [
      {
        "index": 0,
        "message": {
          "role": "assistant",
          "content": null,
          "tool_calls": [
            {
              "index": 0,
              "id": "call_...",
              "type": "function",
              "function": { "name": "<functionName>", "arguments": "{}" }
            }
          ]
        },
        "finish_reason": "tool_calls"
      }
    ]
  }
  ```

- Stream (`?stream=true`):
  - A single chunk with `choices[0].delta.tool_calls[...]`, then `data: [DONE]`.

## How to use this in applications

1. Detect a tool call in the response (non-stream) or streamed chunk.
2. Look up the function name (e.g., `weather.fetch`) in your own registry of allowed tools.
3. Execute the tool logic in your application environment (e.g., a JS function).
4. Append a follow-up message with the tool result and send another `/v1/chat/completions` request.

## Security notes

- The runtime never executes external code or system commands in response to tool-call requests.
- Arguments are returned as an empty JSON object (`{}`) to avoid passing arbitrary payloads. Applications should merge in validated arguments if desired.
- All inputs are size-limited and validated conservatively.
