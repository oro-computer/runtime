#include "../env.hh"

#include "../serviceworker.hh"

namespace oro::runtime::serviceworker {
  Server::Server (Manager& manager, const Options& options)
    : windowOptions(options.window),
      userConfig(options.userConfig),
      origin(options.origin),
      manager(manager) {
    this->userConfig["webview_watch_reload"] = "false";
    this->container.origin = this->origin;
  }

  bool Server::init () {
    if (this->window && this->bridge) {
      return true;
    }

    const auto screen = window::Window::getScreenSize();

    this->windowOptions.createServiceWorker = false;
    this->windowOptions.shouldExitApplicationOnClose = false;
    this->windowOptions.minHeight = window::Window::getSizeInPixels("30%", screen.height);
    this->windowOptions.height = window::Window::getSizeInPixels("80%", screen.height);
    this->windowOptions.minWidth = window::Window::getSizeInPixels("40%", screen.width);
    this->windowOptions.width = window::Window::getSizeInPixels("80%", screen.width);
    this->windowOptions.index = this->manager.windowManager.getRandomWindowIndex(true);
    const auto debugEnv = [&]() {
      auto value = env::get("ORO_SERVICE_WORKER_DEBUG");
      if (value.size() == 0) {
        value = env::get("ORO_RUNTIME_SERVICE_WORKER_DEBUG");
      }
      return value;
    }();
    this->windowOptions.headless = debugEnv.size() == 0;
    this->windowOptions.userConfig = this->userConfig;
    this->windowOptions.features.useGlobalCommonJS = false;
    this->windowOptions.features.useGlobalNodeJS = false;

    this->window = this->manager.windowManager.createWindow(this->windowOptions);
    this->bridge = window->bridge;

    this->container.init(this->bridge);

    this->window->navigate(
      URL("/oro/service-worker/index.html", this->origin).str()
    );

    if (debugEnv.size() > 0) {
      this->window->show();
    } else {
      this->window->hide();
    }

    return true;
  }

  bool Server::fetch (
    const Request& request,
    const Fetch::Options& options,
    const Fetch::Callback callback
  ) {
    if (this->bridge && this->window) {
      return this->container.fetch(request, options, callback);
    }

    return false;
  }
}
