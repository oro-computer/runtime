#include "extension.hh"

void oapi_javascript_evaluate (
  oapi_context_t* ctx,
  const char* name,
  const char* source
) {
  if (ctx == nullptr || name == nullptr || source == nullptr) return;
  if (!ctx->isAllowed("javascript_evaluate")) {
    oapi_debug(ctx, "'javascript_evaluate' is not allowed.");
    return;
  }

  auto script = oro::runtime::javascript::createJavaScript(name, source);
  dynamic_cast<oro::runtime::window::IBridge*>(&ctx->router->bridge)->evaluateJavaScript(script);
}
