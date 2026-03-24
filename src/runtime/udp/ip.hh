#ifndef ORO_RUNTIME_UDP_IP_H
#define ORO_RUNTIME_UDP_IP_H

#include "../platform.hh"

namespace oro::runtime::udp::ip {
  static inline String addrToIPv4 (struct sockaddr_in* sin) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin->sin_addr, buf, INET_ADDRSTRLEN);
    return String(buf);
  }

  static inline String addrToIPv6 (struct sockaddr_in6* sin) {
    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sin->sin6_addr, buf, INET6_ADDRSTRLEN);
    return String(buf);
  }

  static inline void parseAddress (struct sockaddr *name, int* port, char* address, size_t size) {
    if (name->sa_family == AF_INET6) {
      auto* name_in6 = reinterpret_cast<struct sockaddr_in6*>(name);
      *port = ntohs(name_in6->sin6_port);
      uv_ip6_name(name_in6, address, size);
    } else {
      auto* name_in = reinterpret_cast<struct sockaddr_in*>(name);
      *port = ntohs(name_in->sin_port);
      uv_ip4_name(name_in, address, size);
    }
  }
}
#endif
