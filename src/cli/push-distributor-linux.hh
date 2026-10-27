#ifndef ORO_CLI_PUSH_DISTRIBUTOR_LINUX_H
#define ORO_CLI_PUSH_DISTRIBUTOR_LINUX_H

#include <oro/platform.h>
#if ORO_RUNTIME_PLATFORM_LINUX
#include "../runtime/runtime.hh"
#include "../runtime/native/development_distributor.hh"
#include <glib-unix.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <csignal>

namespace oro::cli {
  inline int runLinuxPushDistributor (const std::string& address, const std::filesystem::path& directory,
                                      bool allowLoopbackHTTP, const std::string& caFile) {
    using namespace oro::runtime;
    using namespace oro::runtime::native;
    const auto origin = DevelopmentDistributor::validateOrigin(address, allowLoopbackHTTP);
    if (directory.empty())
      throw StateError("EINVAL");
    const bool created = std::filesystem::create_directories(directory);
    struct stat info {};
    if (lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid())
      throw StateError("EACCES");
    if ((!created && (info.st_mode & 0077)) || (created && chmod(directory.c_str(), 0700)))
      throw StateError("EACCES");
    struct Lease {
      int descriptor = -1;
      ~Lease() {
        if (descriptor >= 0)
          close(descriptor);
      }
    } lease;
    lease.descriptor = open((directory / "distributor.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lease.descriptor < 0 || fstat(lease.descriptor, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() ||
        info.st_nlink != 1 || (info.st_mode & 0077))
      throw StateError("EACCES");
    if (flock(lease.descriptor, LOCK_EX | LOCK_NB))
      throw StateError("EBUSY");
    Runtime::Options options;
    options.autoStart = false;
    options.userConfig = {{"meta_bundle_identifier", "dev.oro.PushDistributor"},
                          {"build_name", "oro-push-development"}};
    Runtime runtime(options);
    if (!runtime.services.state.init() || !runtime.services.secureStorage.start())
      throw StateError("ENOTREADY");
    auto bus = std::make_shared<DesktopBus>();
    const auto path = std::filesystem::absolute(directory).lexically_normal().string();
    const auto hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), path.size());
    const std::string identity(hash);
    g_free(hash);
    RecordStore records(directory / "subscriptions-v2.sqlite", runtime.services.secureStorage.nativeRecordKeys(),
                        "distributor", {"dev.oro.PushDistributor", "development", identity});
    auto loop = g_main_loop_new(nullptr, false);
    struct Loop {
      GMainLoop* value;
      ~Loop() {
        g_main_loop_unref(value);
      }
    } ownedLoop{loop};
    DevelopmentDistributor distributor(bus, std::move(records), origin, allowLoopbackHTTP, caFile);
    const auto stop = +[] (gpointer pointer) -> gboolean {
      g_main_loop_quit(static_cast<GMainLoop*>(pointer));
      return G_SOURCE_CONTINUE;
    };
    const auto interrupt = g_unix_signal_add(SIGINT, stop, loop);
    const auto terminate = g_unix_signal_add(SIGTERM, stop, loop);
    std::cerr << "oroc push distributor: Linux development Distributor2 ready; encrypted queue, 2-second polling; no "
                 "product authentication\n";
    g_main_loop_run(loop);
    distributor.shutdown();
    g_source_remove(interrupt);
    g_source_remove(terminate);
    return 0;
  }
}  // namespace oro::cli
#endif
#endif
