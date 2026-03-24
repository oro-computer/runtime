#include "../runtime.hh"
#include "../bridge.hh"

namespace oro::runtime::bridge {
  Manager::Manager (context::RuntimeContext& context)
    : context(context)
  {}

  Manager::~Manager () {}

  SharedPointer<Bridge> Manager::get (int index, const BridgeOptions& options) {
    Lock lock(this->mutex);
    if (index >= this->entries.size() || this->entries[index] == nullptr) {
      if (index >= this->entries.size()) {
        this->entries.resize(index + 1);
      }

      this->entries[index] = std::make_shared<Bridge>(Bridge::Options {
        .context = this->context,
        .dispatcher = static_cast<runtime::Runtime&>(this->context).dispatcher,
        .userConfig = options.userConfig
      });
    }

    return this->entries.at(index);
  }

  SharedPointer<Bridge> Manager::get (int index) {
    Lock lock(this->mutex);

    if (index >= this->entries.size()) {
      return nullptr;
    }

    return this->entries.at(index);
  }

  bool Manager::has (int index) const {
    return index < this->entries.size() && this->entries.at(index) != nullptr;
  }

  bool Manager::remove (int index) {
    Lock lock(this->mutex);

    if (index < 0 || index >= this->entries.size() || this->entries[index] == nullptr) {
      return false;
    }

    // Indices are stable across the runtime (window index == bridge index).
    // Do not `erase()` here, as it shifts subsequent entries and causes index
    // misalignment for any existing windows.
    this->entries[index] = nullptr;
    return true;
  }
}
