#ifndef ORO_RUNTIME_NATIVE_DESKTOP_DIALOG_H
#define ORO_RUNTIME_NATIVE_DESKTOP_DIALOG_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "execution.hh"
#include <gtk/gtk.h>

namespace oro::runtime::native {
  inline constexpr const char* interactivePromptKey = "oro-native-interactive-prompt";
  bool ownsNativePrompt (GtkWidget*);
  std::shared_ptr<GtkWindow> retainNativeWindow (GtkWindow*);
  /** Worker-only, document-cancellable native PIN input. It never crosses the renderer bridge. */
  RecordBytes promptNativePin (GtkWindow*, const std::string& title, bool protectedPath,
                               const std::shared_ptr<ExecutionCancellation>&);
}  // namespace oro::runtime::native
#endif
#endif
