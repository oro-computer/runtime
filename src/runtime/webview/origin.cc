#include "../webview.hh"

namespace oro::runtime::webview {
  String Origin::name () const {
    if (!this->scheme.empty()) {
      return this->scheme + "://" + this->hostname;
    }

    return "";
  }
}
