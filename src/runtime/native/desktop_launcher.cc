#include "desktop_launcher.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <gio/gio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <regex>
#include <set>

namespace oro::runtime::native {
  namespace {
    int64_t currentTime () {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
    }
    std::string uuid () {
      const auto raw = g_uuid_string_random();
      const std::string result(raw);
      g_free(raw);
      return result;
    }
    std::string text (const JSON::Object& value, const std::string& name) {
      if (!value.has(name) || value.get(name).type != JSON::Type::String)
        throw StateError("EINVAL");
      return value.get(name).as<JSON::String>().value();
    }
    bool opaque (const std::string& value) {
      return !value.empty() && value.size() <= 128 &&
             value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
               std::string::npos;
    }
    RecordBytes encode (const JSON::Any& value) {
      const auto source = value.str();
      return {source.begin(), source.end()};
    }
    JSON::Object decode (const RecordBytes& bytes) {
      try {
        return JSON::parse(std::string(bytes.begin(), bytes.end())).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
    std::string argument (const std::string& value) {
      std::string result = "\"";
      for (const auto character : value) {
        if (character == '\\' || character == '"' || character == '$' || character == '`')
          result += '\\';
        if (character == '%')
          result += '%';
        result += character;
      }
      return result + '"';
    }
    struct Descriptor {
      int value;
      explicit Descriptor(int value) : value(value) {
        if (value < 0)
          throw StateError("EACCES");
      }
      ~Descriptor() {
        close(value);
      }
    };
  }  // namespace

  DesktopLauncher::DesktopLauncher(RecordStore store, std::string bundle, std::string title, std::string protocol)
      : store(std::move(store)), bundle(std::move(bundle)), title(std::move(title)), protocol(std::move(protocol)) {
    if (!std::regex_match(this->bundle, std::regex("[a-zA-Z0-9][a-zA-Z0-9_.-]{1,199}")) ||
        !std::regex_match(this->protocol, std::regex("[a-z][a-z0-9+.-]{0,99}")) || this->title.empty() ||
        this->title.size() > 256 || this->title.find('\0') != std::string::npos)
      throw StateError("EINVAL");
    std::array<char, 4097> path;
    const auto size = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (size <= 0 || size >= static_cast<ssize_t>(path.size() - 1))
      throw StateError("ENOTREADY");
    this->executable.assign(path.data(), size);
    if (this->executable.find_first_of("=\n\r") != std::string::npos || this->executable.ends_with(" (deleted)"))
      throw StateError("ENOTREADY");
  }

  void DesktopLauncher::publish(const JSON::Object& publication) {
    const std::filesystem::path directory = std::filesystem::path(g_get_user_data_dir()) / "applications";
    if (!directory.is_absolute())
      throw StateError("EINVAL");
    std::error_code failure;
    const bool created = std::filesystem::create_directories(directory, failure);
    if (failure)
      throw StateError("EACCES");
    if (created)
      std::filesystem::permissions(directory, std::filesystem::perms::owner_all, failure);
    if (failure)
      throw StateError("EACCES");
    Descriptor parent(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat information {};
    if (fstat(parent.value, &information) || information.st_uid != getuid() || (information.st_mode & 0022))
      throw StateError("EACCES");
    const auto filename = this->bundle + ".oro-destinations.desktop";
    const int existing = openat(parent.value, filename.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (existing >= 0) {
      Descriptor previous(existing);
      if (fstat(previous.value, &information) || !S_ISREG(information.st_mode) || information.st_uid != getuid() ||
          information.st_size > 65536)
        throw StateError("EACCES");
      std::string content(static_cast<size_t>(information.st_size), '\0');
      if (read(previous.value, content.data(), content.size()) != static_cast<ssize_t>(content.size()) ||
          content.find("X-Oro-Runtime-Owned=" + this->bundle + "\n") == std::string::npos)
        throw StateError("EEXIST");
    } else if (errno != ENOENT)
      throw StateError("EACCES");
    const auto file = std::unique_ptr<GKeyFile, decltype(&g_key_file_free)>(g_key_file_new(), g_key_file_free);
    g_key_file_set_string(file.get(), "Desktop Entry", "Type", "Application");
    g_key_file_set_string(file.get(), "Desktop Entry", "Name", this->title.c_str());
    g_key_file_set_string(file.get(), "Desktop Entry", "Icon", this->bundle.c_str());
    g_key_file_set_string(file.get(), "Desktop Entry", "Exec", argument(this->executable).c_str());
    g_key_file_set_string(file.get(), "Desktop Entry", "X-Oro-Runtime-Owned", this->bundle.c_str());
    const auto generation = text(publication, "generation");
    const auto destinations = publication.get("destinations").as<JSON::Array>();
    std::vector<std::string> identifiers;
    for (const auto& destination : destinations) {
      const auto entry = destination.as<JSON::Object>();
      const auto id = text(entry, "id");
      identifiers.push_back(id);
      const auto section = "Desktop Action " + id;
      g_key_file_set_string(file.get(), section.c_str(), "Name", text(entry, "label").c_str());
      const auto command =
        argument(this->executable) + " " + argument(this->protocol + "://oro-native-launcher/" + generation + "/" + id);
      g_key_file_set_string(file.get(), section.c_str(), "Exec", command.c_str());
    }
    std::vector<const char*> pointers;
    for (const auto& id : identifiers)
      pointers.push_back(id.c_str());
    g_key_file_set_string_list(file.get(), "Desktop Entry", "Actions", pointers.data(), pointers.size());
    gsize size = 0;
    const auto raw = g_key_file_to_data(file.get(), &size, nullptr);
    if (!raw)
      throw StateError("EIO");
    const auto source = std::unique_ptr<char, decltype(&g_free)>(raw, g_free);
    const auto temporary = filename + "." + uuid();
    bool renamed = false;
    try {
      Descriptor output(
        openat(parent.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
      size_t offset = 0;
      while (offset < size) {
        const auto written = write(output.value, source.get() + offset, size - offset);
        if (written < 0 && errno == EINTR)
          continue;
        if (written <= 0)
          throw StateError("EIO");
        offset += written;
      }
      if (fsync(output.value) || renameat(parent.value, temporary.c_str(), parent.value, filename.c_str()))
        throw StateError("EIO");
      renamed = true;
      if (fsync(parent.value))
        throw StateError("EUNKNOWN");
    } catch (...) {
      if (!renamed)
        unlinkat(parent.value, temporary.c_str(), 0);
      throw;
    }
  }

  JSON::Object DesktopLauncher::replace(const JSON::Array& destinations) {
    if (destinations.size() > 16)
      throw StateError("EINVAL");
    std::set<std::string> ids;
    for (const auto& destination : destinations) {
      if (destination.type != JSON::Type::Object)
        throw StateError("EINVAL");
      const auto entry = destination.as<JSON::Object>();
      if (entry.size() != 4)
        throw StateError("EINVAL");
      const auto id = text(entry, "id");
      const auto label = text(entry, "label");
      if (!opaque(id) || !ids.insert(id).second || !opaque(text(entry, "accountRef")) ||
          !opaque(text(entry, "targetRef")) || label.empty() || g_utf8_strlen(label.c_str(), label.size()) > 80 ||
          label.size() > 320 || !g_utf8_validate(label.c_str(), label.size(), nullptr))
        throw StateError("EINVAL");
      for (const unsigned char character : label)
        if (character < 32 || character == 127)
          throw StateError("EINVAL");
    }
    auto snapshot = this->store.read();
    JSON::Object publication(JSON::Object::Entries{{"generation", uuid()}, {"destinations", destinations}});
    if (const auto found = snapshot.records.find("publication"); found != snapshot.records.end()) {
      const auto old = decode(found->second);
      if (old.get("destinations").str() == destinations.str())
        publication = old;
    }
    this->store.apply(snapshot.revision, {{"publication", encode(publication)}});
    try {
      this->publish(publication);
    } catch (...) {
      throw StateError("EUNKNOWN");
    }
    return JSON::Object::Entries{{"published", destinations.size()},
                                 {"authorizationEstablished", false},
                                 {"pinConfirmed", false},
                                 {"presentation", "desktop-actions"}};
  }

  JSON::Array DesktopLauncher::responses() {
    JSON::Array result;
    for (const auto& [key, bytes] : this->store.read().records) {
      if (!key.starts_with("receipt."))
        continue;
      const auto value = decode(bytes);
      if (value.get("receivedAt").as<JSON::Number>().value() >= currentTime() - 604800000)
        result.push(value);
    }
    return result;
  }

  void DesktopLauncher::acknowledge(const std::string& id) {
    if (!g_uuid_string_is_valid(id.c_str()))
      throw StateError("EINVAL");
    const auto snapshot = this->store.read();
    if (snapshot.records.contains("receipt." + id))
      this->store.apply(snapshot.revision, {{"receipt." + id, std::nullopt}});
  }

  bool DesktopLauncher::receive(const std::string& uri) {
    const auto prefix = this->protocol + "://oro-native-launcher/";
    if (!uri.starts_with(prefix) || uri.size() > prefix.size() + 36 + 1 + 128)
      return false;
    const auto suffix = uri.substr(prefix.size());
    if (suffix.size() < 38 || suffix[36] != '/' || !g_uuid_string_is_valid(suffix.substr(0, 36).c_str()) ||
        !opaque(suffix.substr(37)))
      throw StateError("EINVAL");
    const auto snapshot = this->store.read();
    const auto found = snapshot.records.find("publication");
    if (found == snapshot.records.end())
      throw StateError("ENOENT");
    const auto publication = decode(found->second);
    if (text(publication, "generation") != suffix.substr(0, 36))
      throw StateError("ESTALE");
    for (const auto& item : publication.get("destinations").as<JSON::Array>()) {
      const auto entry = item.as<JSON::Object>();
      if (text(entry, "id") != suffix.substr(37))
        continue;
      RecordChanges changes;
      size_t count = 0;
      std::pair<int64_t, std::string> oldest{INT64_MAX, ""};
      for (const auto& [key, bytes] : snapshot.records) {
        if (!key.starts_with("receipt."))
          continue;
        const auto time = static_cast<int64_t>(decode(bytes).get("receivedAt").as<JSON::Number>().value());
        if (time < currentTime() - 604800000)
          changes[key] = std::nullopt;
        else {
          count++;
          if (time < oldest.first)
            oldest = {time, key};
        }
      }
      if (count >= 32)
        changes[oldest.second] = std::nullopt;
      const auto id = uuid();
      changes["receipt." + id] = encode(JSON::Object(JSON::Object::Entries{{"receiptId", id},
                                                                           {"id", text(entry, "id")},
                                                                           {"accountRef", text(entry, "accountRef")},
                                                                           {"targetRef", text(entry, "targetRef")},
                                                                           {"static", false},
                                                                           {"receivedAt", currentTime()}}));
      this->store.apply(snapshot.revision, changes);
      return true;
    }
    throw StateError("ENOENT");
  }
}  // namespace oro::runtime::native
#endif
