#include "../javascript.hh"
#include "../config.hh"
#include "../window.hh"
#include "../string.hh"
#include "../cwd.hh"
#include "../app.hh"

using oro::runtime::javascript::getResolveMenuSelectionJavaScript;
using oro::runtime::javascript::getEmitToRenderProcessJavaScript;
using oro::runtime::config::getUserConfig;
using oro::runtime::string::trim;
using oro::runtime::string::split;
using oro::runtime::string::convertStringToWString;
using oro::runtime::string::convertWStringToString;

namespace oro::runtime::app {
  static Atomic<bool> isConsoleVisible = false;
  static FILE* console = nullptr;

  static inline void alert (const WString &ws) {
    MessageBoxW(nullptr, ws.c_str(), L"Alert", MB_OK | MB_ICONSTOP);
  }

  static inline void alert (const String &s) {
    alert(convertStringToWString(s));
  }

  static inline void alert (const char* s) {
    alert(s != nullptr ? String(s) : String());
  }

  static void showWindowsConsole () {
    if (!isConsoleVisible) {
      if (AllocConsole() && freopen_s(&console, "CONOUT$", "w", stdout) == 0) {
        isConsoleVisible = true;
      } else {
        console = nullptr;
        FreeConsole();
      }
    }
  }

  static void hideWindowsConsole () {
    if (isConsoleVisible) {
      isConsoleVisible = false;
      if (console != nullptr) {
        fclose(console);
        console = nullptr;
      }
      FreeConsole();
    }
  }

  // message is defined in WinUser.h
  // https://raw.githubusercontent.com/tpn/winsdk-10/master/Include/10.0.10240.0/um/WinUser.h
  LRESULT CALLBACK onWindowProcMessage (
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
  ) {
    auto app = App::sharedApplication();

    if (app == nullptr) {
      return 0;
    }

    auto window = reinterpret_cast<oro::runtime::window::Manager::ManagedWindow*>(
      GetWindowLongPtr(hWnd, GWLP_USERDATA)
    );

    // Validate window userdata without dereferencing a potentially stale pointer.
    if (window != nullptr) {
      bool isManaged = false;
      for (const auto& managed : app->runtime.windowManager.windows) {
        if (managed.get() == window) {
          isManaged = true;
          break;
        }
      }

      if (!isManaged) {
        window = nullptr;
      }
    }

    auto userConfig = window != nullptr
      ? static_cast<oro::runtime::window::Window*>(window)->bridge->userConfig
      : getUserConfig();

    if (message == WM_COPYDATA) {
      auto copyData = reinterpret_cast<PCOPYDATASTRUCT>(lParam);
      constexpr DWORD maxDeepLinkBytes = 1024 * 1024;
      if (
        copyData == nullptr ||
        copyData->dwData != WM_HANDLE_DEEP_LINK ||
        copyData->cbData > maxDeepLinkBytes ||
        (copyData->cbData > 0 && copyData->lpData == nullptr)
      ) {
        return FALSE;
      }

      message = WM_HANDLE_DEEP_LINK;
      wParam = static_cast<WPARAM>(copyData->cbData);
      lParam = reinterpret_cast<LPARAM>(copyData->lpData);
    }

    switch (message) {
      case WM_ACTIVATEAPP: {
        // Propagate lifecycle and mirror DOM focus/blur
        auto w = window
          ? static_cast<oro::runtime::window::Window*>(window)
          : nullptr;
        if (wParam) {
          if (w) w->dispatchDomFocus();
          app->resume();
        } else {
          if (w) w->dispatchDomBlur();
          app->pause();
        }
        break;
      }

      case WM_POWERBROADCAST: {
        // Propagate lifecycle only
        switch (wParam) {
          case PBT_APMSUSPEND:
          case PBT_APMSTANDBY:
            app->pause();
            return TRUE;
          case PBT_APMRESUMEAUTOMATIC:
          case PBT_APMRESUMECRITICAL:
          case PBT_APMRESUMESUSPEND:
            app->resume();
            return TRUE;
        }
        break;
      }

      case WM_QUERYENDSESSION: {
        // Allow session end; mark app to exit soon
        app->shouldExit = true;
        return TRUE;
      }

      case WM_ENDSESSION: {
        if (wParam) {
          // Session is ending (shutdown/logoff)
          app->stop();
        }
        break;
      }

      case WM_SIZE: {
        // Propagate lifecycle and mirror DOM focus/blur
        auto w = window
          ? static_cast<oro::runtime::window::Window*>(window)
          : nullptr;
        if (wParam == SIZE_MINIMIZED) {
          if (w) w->dispatchDomBlur();
          app->pause();
        } else if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) {
          if (w) w->dispatchDomFocus();
          app->resume();
        }
        if (window == nullptr || window->controller == nullptr) {
          break;
        }

        RECT bounds;
        GetClientRect(hWnd, &bounds);
        window->size.height = bounds.bottom - bounds.top;
        window->size.width = bounds.right - bounds.left;
        window->controller->put_Bounds(bounds);
        break;
      }

      case WM_ORO_TRAY: {
        // XXX: confirm this is the correct predicate for detecting tray agent mode
        auto isAgent = userConfig.count("tray_icon") != 0;

        if (window != nullptr && lParam == WM_LBUTTONDOWN) {
          SetForegroundWindow(hWnd);
          if (isAgent) {
            POINT point;
            GetCursorPos(&point);
            TrackPopupMenu(
              window->menutray,
              TPM_BOTTOMALIGN | TPM_LEFTALIGN,
              point.x,
              point.y,
              0,
              hWnd,
              NULL
            );
          }

          PostMessage(hWnd, WM_NULL, 0, 0);

          // broadcast an event to all the windows that the tray icon was clicked
          for (auto window : app->runtime.windowManager.windows) {
            if (window != nullptr) {
              window->bridge->emit("tray", JSON::Object {});
            }
          }
        }

        // XXX: falls through to `WM_COMMAND` below
      }

      case WM_COMMAND: {
        if (window == nullptr) {
          break;
        }

        if (window->menuMap.contains(wParam)) {
          String meta(window->menuMap[wParam]);
          auto parts = split(meta, '\t');

          if (parts.size() > 1) {
            auto title = parts[0];
            auto parent = parts[1];

            if (title.find("About") == 0) {
              static_cast<oro::runtime::window::Window*>(window)->about();
              break;
            }

            if (title.find("Quit") == 0) {
              window->exit(0);
              break;
            }

            window->eval(getResolveMenuSelectionJavaScript("0", title, parent, "system"));
          }
        } else if (window->menuTrayMap.contains(wParam)) {
          String meta(window->menuTrayMap[wParam]);
          auto parts = split(meta, ':');

          if (parts.size() > 0) {
            auto title = trim(parts[0]);
            auto tag = parts.size() > 1 ? trim(parts[1]) : "";
            window->eval(getResolveMenuSelectionJavaScript("0", title, tag, "tray"));
          }
        }

        break;
      }

      case WM_SETTINGCHANGE: {
        // TODO(heapwolf): Dark mode
        break;
      }

      case WM_CREATE: {
        // TODO(heapwolf): Dark mode
        SetWindowTheme(hWnd, L"Explorer", NULL);
        SetMenu(hWnd, CreateMenu());
        break;
      }

      case WM_CLOSE: {
        if (!window || !window->getOptions().closable) {
          break;
        }

        auto index = window->index;
        const JSON::Object json = JSON::Object::Entries {
          {"data", index}
        };

        for (auto window : app->runtime.windowManager.windows) {
          if (window != nullptr && window->index != index) {
            window->eval(getEmitToRenderProcessJavaScript("windowclosed", json.str()));
          }
        }

        app->runtime.windowManager.destroyWindow(index);
        break;
      }

      case WM_HOTKEY: {
        if (window != nullptr) {
          window->hotkey.onHotKeyBindingCallback(
            (oro::runtime::window::HotKeyBinding::ID) wParam
          );
        }
        break;
      }

      case WM_HANDLE_DEEP_LINK: {
        const auto url = String(reinterpret_cast<const char*>(lParam), wParam);

        for (auto window : app->runtime.windowManager.windows) {
          if (window != nullptr) {
            window->handleApplicationURL(url);
          }
        }
        break;
      }

      case WM_GETMINMAXINFO: {
        const auto screen = oro::runtime::window::Window::getScreenSize();
        auto info = reinterpret_cast<LPMINMAXINFO>(lParam);

        info->ptMinTrackSize.x = oro::runtime::window::Window::getSizeInPixels(
          app->runtime.windowManager.options.defaultMinWidth,
          screen.width
        );

        info->ptMinTrackSize.y = oro::runtime::window::Window::getSizeInPixels(
          app->runtime.windowManager.options.defaultMinHeight,
          screen.height
        );

        info->ptMaxTrackSize.x = oro::runtime::window::Window::getSizeInPixels(
          app->runtime.windowManager.options.defaultMaxWidth,
          screen.width
        );

        info->ptMaxTrackSize.y = oro::runtime::window::Window::getSizeInPixels(
          app->runtime.windowManager.options.defaultMaxHeight,
          screen.height
        );
        break;
      }

      //case WM_WINDOWPOSCHANGING: { break; }

      default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    return 0;
  }

  void registerWindowClass (App* app) {
    auto userConfig = app->runtime.userConfig;
    // this fixes bad default quality DPI.
    SetProcessDPIAware();

    if (userConfig["win_logo"].size() == 0 && userConfig["win_icon"].size() > 0) {
      userConfig["win_logo"] = fs::path(userConfig["win_icon"]).filename().string();
    }

    auto iconPath = fs::path { getcwd() / fs::path { userConfig["win_logo"] } };

    const auto wideIconPath = iconPath.wstring();
    HICON icon = reinterpret_cast<HICON>(LoadImageW(
      NULL,
      wideIconPath.c_str(),
      IMAGE_ICON,
      GetSystemMetrics(SM_CXICON),
      GetSystemMetrics(SM_CXICON),
      LR_LOADFROMFILE
    ));

    auto windowClassName = userConfig["meta_bundle_identifier"];
    const auto wideWindowClassName = convertStringToWString(windowClassName);
    if (wideWindowClassName.empty()) {
      alert("Application bundle identifier is not valid UTF-8.");
      return;
    }

    app->wcex = {};
    app->wcex.cbSize = sizeof(WNDCLASSEXW);
    app->wcex.style = CS_HREDRAW | CS_VREDRAW;
    app->wcex.cbClsExtra = 0;
    app->wcex.cbWndExtra = 0;
    app->wcex.hInstance = app->instance;
    app->wcex.hIcon = LoadIcon(app->instance, IDI_APPLICATION);
    app->wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    app->wcex.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    app->wcex.lpszMenuName = NULL;
    app->wcex.lpszClassName = wideWindowClassName.c_str();
    app->wcex.hIconSm = icon; // ico doesn't auto scale, needs 16x16 icon lol fuck you bill
    app->wcex.hIcon = icon;
    app->wcex.lpfnWndProc = onWindowProcMessage;

    if (!RegisterClassExW(&app->wcex)) {
      alert("Application could not launch, possible missing resources.");
    }
  }
}
