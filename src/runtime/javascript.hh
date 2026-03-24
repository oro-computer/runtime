#ifndef ORO_RUNTIME_JAVASCRIPT_H
#define ORO_RUNTIME_JAVASCRIPT_H

#include "platform.hh"
#include "json.hh"

namespace oro::runtime::javascript {
  String createJavaScript (const String& name, const String& source);

  String getEmitToRenderProcessJavaScript (
    const String& event,
    const String& value,
    const String& target,
    const JSON::Object& options
  );

  String getEmitToRenderProcessJavaScript (
    const String& event,
    const String& value
  );

  String getResolveMenuSelectionJavaScript (
    const String& seq,
    const String& title,
    const String& parent,
    const String type = "system"
  );

  String getResolveToRenderProcessJavaScript (
    const String& seq,
    const String& state,
    const String& value
  );
}
#endif
