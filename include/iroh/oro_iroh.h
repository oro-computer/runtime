/*! \file oro_iroh.h
 *  \brief Oro Runtime bindings for UniFFI-backed iroh features.
 *
 *  This header defines the in-progress C ABI the native runtime depends on when
 *  bridging to the Rust implementation in `rust/oro-iroh`. It purposefully
 *  drops the legacy `irohnet.h` surface and documents the capabilities we
 *  expect to replace during the UniFFI migration.
 *
 *  NOTE: Timeout-aware operations, ticket helpers, and extended telemetry hooks
 *  will be added as the Rust layer grows parity with the legacy surface.
 *
 *  Ownership rules:
 *    - `char*` returned from functions must be freed with `oro_iroh_string_free`.
 *    - `oro_iroh_bytes_t` instances returned by value must be freed with
 *      `oro_iroh_bytes_free`.
 *    - All handles (`*_t`) are opaque and must be released with their matching
 *      `*_free` helper.
 */

#ifndef ORO_IROH_ORO_IROH_H
#define ORO_IROH_ORO_IROH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(ORO_IROH_HEADER_ALLOW_INCOMPLETE)
#  error "include/iroh/oro_iroh.h is experimental; define ORO_IROH_HEADER_ALLOW_INCOMPLETE before including while the UniFFI migration is in progress."
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oro_iroh_error {
  int32_t code;
  char* message;
} oro_iroh_error_t;

void oro_iroh_error_clear(oro_iroh_error_t* err);

typedef struct oro_iroh_bytes {
  uint8_t* data;
  size_t len;
} oro_iroh_bytes_t;

void oro_iroh_bytes_free(oro_iroh_bytes_t* bytes);

void oro_iroh_string_free(char* ptr);

#define ORO_IROH_ERROR_CODE_TIMEOUT 1

bool oro_iroh_set_log_level(int level, oro_iroh_error_t* err);

bool oro_iroh_path_to_key(
  const char* path,
  const char* prefix,
  const char* root,
  oro_iroh_bytes_t* out_bytes,
  oro_iroh_error_t* err
);

bool oro_iroh_key_to_path(
  const uint8_t* key_data,
  size_t key_len,
  const char* prefix,
  const char* root,
  char** out_path,
  oro_iroh_error_t* err
);

typedef struct oro_iroh_node oro_iroh_node_t;
typedef struct oro_iroh_net oro_iroh_net_t;
typedef struct oro_iroh_endpoint oro_iroh_endpoint_t;
typedef struct oro_iroh_connection oro_iroh_connection_t;
typedef struct oro_iroh_node_addr oro_iroh_node_addr_t;
typedef struct oro_iroh_send_stream oro_iroh_send_stream_t;
typedef struct oro_iroh_recv_stream oro_iroh_recv_stream_t;
typedef struct oro_iroh_conn_type_watcher oro_iroh_conn_type_watcher_t;

typedef struct oro_iroh_node_options {
  bool enable_docs;
  bool has_gc_interval;
  uint64_t gc_interval_millis;
  const char* ipv4_addr;
  const char* ipv6_addr;
  int32_t discovery; // 0 = default, 1 = none
  const uint8_t* secret_key;
  size_t secret_key_len;
} oro_iroh_node_options_t;

bool oro_iroh_node_memory(
  const oro_iroh_node_options_t* options,
  oro_iroh_node_t** out_node,
  oro_iroh_error_t* err
);

void oro_iroh_node_free(oro_iroh_node_t* node);

bool oro_iroh_node_net(
  oro_iroh_node_t* node,
  oro_iroh_net_t** out_net,
  oro_iroh_error_t* err
);

bool oro_iroh_node_endpoint(
  oro_iroh_node_t* node,
  oro_iroh_endpoint_t** out_endpoint,
  oro_iroh_error_t* err
);

void oro_iroh_endpoint_free(oro_iroh_endpoint_t* endpoint);

bool oro_iroh_endpoint_set_alpns(
  oro_iroh_endpoint_t* endpoint,
  const oro_iroh_bytes_t* alpns,
  size_t alpns_len,
  oro_iroh_error_t* err
);

bool oro_iroh_endpoint_accept(
  oro_iroh_endpoint_t* endpoint,
  const uint8_t* expected_alpn,
  size_t expected_alpn_len,
  oro_iroh_connection_t** out_conn,
  oro_iroh_bytes_t* out_negotiated_alpn,
  oro_iroh_error_t* err
);

bool oro_iroh_endpoint_accept_any(
  oro_iroh_endpoint_t* endpoint,
  oro_iroh_connection_t** out_conn,
  oro_iroh_bytes_t* out_negotiated_alpn,
  oro_iroh_error_t* err
);

bool oro_iroh_endpoint_close(
  oro_iroh_endpoint_t* endpoint,
  oro_iroh_error_t* err
);

bool oro_iroh_endpoint_network_change(
  oro_iroh_endpoint_t* endpoint,
  oro_iroh_error_t* err
);

// TODO: oro_iroh_endpoint_watch_connection_type.

bool oro_iroh_endpoint_connect(
  oro_iroh_endpoint_t* endpoint,
  const oro_iroh_node_addr_t* addr,
  const uint8_t* alpn,
  size_t alpn_len,
  oro_iroh_connection_t** out_conn,
  oro_iroh_error_t* err
);

void oro_iroh_connection_free(oro_iroh_connection_t* conn);

bool oro_iroh_connection_close(
  oro_iroh_connection_t* conn,
  uint64_t error_code,
  const uint8_t* reason,
  size_t reason_len,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_closed(
  oro_iroh_connection_t* conn,
  char** out_reason,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_wait_closed(
  oro_iroh_connection_t* conn,
  oro_iroh_error_t* err
);

typedef void (*oro_iroh_conn_type_callback)(
  void* user_data,
  int32_t result_code,
  int32_t connection_type,
  const char* direct_addr,
  const char* relay_url
);

bool oro_iroh_endpoint_watch_connection_type(
  oro_iroh_endpoint_t* endpoint,
  const char* node_id,
  oro_iroh_conn_type_callback callback,
  void* user_data,
  oro_iroh_conn_type_watcher_t** out_watcher,
  oro_iroh_error_t* err
);

void oro_iroh_conn_type_watcher_cancel(oro_iroh_conn_type_watcher_t* watcher);

bool oro_iroh_connection_send_datagram(
  oro_iroh_connection_t* conn,
  const uint8_t* data,
  size_t len,
  uint64_t timeout_ms,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_read_datagram(
  oro_iroh_connection_t* conn,
  uint64_t timeout_ms,
  oro_iroh_bytes_t* out_bytes,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_max_datagram_size(
  oro_iroh_connection_t* conn,
  size_t* out_size,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_open_bi(
  oro_iroh_connection_t* conn,
  oro_iroh_send_stream_t** out_send,
  oro_iroh_recv_stream_t** out_recv,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_open_uni(
  oro_iroh_connection_t* conn,
  oro_iroh_send_stream_t** out_send,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_accept_bi(
  oro_iroh_connection_t* conn,
  oro_iroh_send_stream_t** out_send,
  oro_iroh_recv_stream_t** out_recv,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_accept_uni(
  oro_iroh_connection_t* conn,
  oro_iroh_recv_stream_t** out_recv,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_alpn(
  oro_iroh_connection_t* conn,
  bool* out_present,
  oro_iroh_bytes_t* out_alpn,
  oro_iroh_error_t* err
);

bool oro_iroh_connection_rtt(
  oro_iroh_connection_t* conn,
  uint64_t* out_rtt,
  oro_iroh_error_t* err
);

void oro_iroh_send_stream_free(oro_iroh_send_stream_t* stream);
bool oro_iroh_send_stream_write(
  oro_iroh_send_stream_t* stream,
  const uint8_t* data,
  size_t len,
  uint64_t timeout_ms,
  uint64_t* out_written,
  oro_iroh_error_t* err
);
bool oro_iroh_send_stream_write_all(
  oro_iroh_send_stream_t* stream,
  const uint8_t* data,
  size_t len,
  oro_iroh_error_t* err
);
bool oro_iroh_send_stream_finish(
  oro_iroh_send_stream_t* stream,
  oro_iroh_error_t* err
);
bool oro_iroh_send_stream_id(
  oro_iroh_send_stream_t* stream,
  char** out_id,
  oro_iroh_error_t* err
);

void oro_iroh_recv_stream_free(oro_iroh_recv_stream_t* stream);
bool oro_iroh_recv_stream_read(
  oro_iroh_recv_stream_t* stream,
  uint32_t size_limit,
  uint64_t timeout_ms,
  oro_iroh_bytes_t* out_bytes,
  oro_iroh_error_t* err
);
bool oro_iroh_recv_stream_read_to_end(
  oro_iroh_recv_stream_t* stream,
  uint32_t size_limit,
  uint64_t timeout_ms,
  oro_iroh_bytes_t* out_bytes,
  oro_iroh_error_t* err
);
bool oro_iroh_recv_stream_read_exact(
  oro_iroh_recv_stream_t* stream,
  uint32_t size,
  uint64_t timeout_ms,
  oro_iroh_bytes_t* out_bytes,
  oro_iroh_error_t* err
);
bool oro_iroh_recv_stream_stop(
  oro_iroh_recv_stream_t* stream,
  uint64_t error_code,
  oro_iroh_error_t* err
);
bool oro_iroh_recv_stream_received_reset(
  oro_iroh_recv_stream_t* stream,
  bool* out_present,
  uint64_t* out_code,
  oro_iroh_error_t* err
);
bool oro_iroh_recv_stream_id(
  oro_iroh_recv_stream_t* stream,
  char** out_id,
  oro_iroh_error_t* err
);

void oro_iroh_net_free(oro_iroh_net_t* net);
bool oro_iroh_net_node_id(
  oro_iroh_net_t* net,
  char** out_str,
  oro_iroh_error_t* err
);
bool oro_iroh_net_node_addr(
  oro_iroh_net_t* net,
  oro_iroh_node_addr_t** out_addr,
  oro_iroh_error_t* err
);
bool oro_iroh_net_add_node_addr(
  oro_iroh_net_t* net,
  const oro_iroh_node_addr_t* addr,
  oro_iroh_error_t* err
);
bool oro_iroh_net_home_relay(
  oro_iroh_net_t* net,
  char** out_url,
  oro_iroh_error_t* err
);

void oro_iroh_node_addr_free(oro_iroh_node_addr_t* addr);
bool oro_iroh_node_addr_from_string(
  const char* addr,
  oro_iroh_node_addr_t** out_addr,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_from_public_key(
  const uint8_t* public_key,
  size_t len,
  oro_iroh_node_addr_t** out_addr,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_node_id(
  const oro_iroh_node_addr_t* addr,
  char** out_id,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_to_string(
  const oro_iroh_node_addr_t* addr,
  char** out_str,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_direct_addresses_len(
  const oro_iroh_node_addr_t* addr,
  size_t* out_len,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_direct_address_at(
  const oro_iroh_node_addr_t* addr,
  size_t index,
  char** out_str,
  oro_iroh_error_t* err
);
bool oro_iroh_node_addr_relay_url(
  const oro_iroh_node_addr_t* addr,
  char** out_url,
  oro_iroh_error_t* err
);

typedef struct oro_iroh_connection_stats {
  uint64_t max_datagram_size;
  uint64_t rtt_micros;
  uint64_t lost_packets;
  uint64_t sent_packets;
} oro_iroh_connection_stats_t;

bool oro_iroh_connection_stats(
  oro_iroh_connection_t* conn,
  oro_iroh_connection_stats_t* out_stats,
  oro_iroh_error_t* err
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ORO_IROH_ORO_IROH_H
