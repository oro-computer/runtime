use std::convert::TryInto;
use std::ffi::{c_void, CStr, CString};
use std::fmt::Display;
use std::future::Future;
use std::net::SocketAddr;
use std::os::raw::{c_char, c_int};
use std::path::PathBuf;
use std::ptr;
use std::slice;
use std::str::FromStr;
use std::sync::{Arc, Mutex as StdMutex, OnceLock};

use anyhow::{anyhow, Result};
use bytes::Bytes;
use futures_util::StreamExt;
use iroh::endpoint::{
    Connection as IrohConnection, ConnectionType as EndpointConnectionType, Endpoint,
    ReadExactError, RecvStream, SendStream, VarInt,
};
use iroh::{NodeId, Watcher};
use iroh_base::ticket::NodeTicket;
use iroh_base::{NodeAddr, PublicKey, SecretKey};
use iroh_blobs::util::fs as blob_fs;
use once_cell::sync::Lazy;
use tokio::runtime::Runtime;
use tokio::sync::{oneshot, Mutex as TokioMutex};
use tokio::time::{sleep, Duration};
use tracing_subscriber::{
    filter::LevelFilter, fmt, layer::SubscriberExt, reload, util::SubscriberInitExt, Registry,
};

static RUNTIME: Lazy<Runtime> = Lazy::new(|| {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .thread_name("oro-iroh")
        .build()
        .expect("failed to initialize tokio runtime")
});

const ORO_IROH_ERROR_CODE_TIMEOUT: c_int = 1;

#[derive(Debug)]
enum OperationError {
    Timeout,
    Failure(String),
}

#[derive(Clone, Copy, Debug)]
enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Off,
}

impl LogLevel {
    fn as_level_filter(self) -> LevelFilter {
        match self {
            LogLevel::Trace => LevelFilter::TRACE,
            LogLevel::Debug => LevelFilter::DEBUG,
            LogLevel::Info => LevelFilter::INFO,
            LogLevel::Warn => LevelFilter::WARN,
            LogLevel::Error => LevelFilter::ERROR,
            LogLevel::Off => LevelFilter::OFF,
        }
    }
}

static LOG_RELOAD_HANDLE: OnceLock<reload::Handle<LevelFilter, Registry>> = OnceLock::new();

fn set_log_level(level: LogLevel) -> Result<()> {
    let desired = level.as_level_filter();

    if let Some(handle) = LOG_RELOAD_HANDLE.get() {
        handle
            .modify(|current| {
                *current = desired;
            })
            .map_err(|err| anyhow!(err.to_string()))?;
        return Ok(());
    }

    let (filter_layer, handle) = reload::Layer::new(desired);
    let subscriber = tracing_subscriber::registry()
        .with(filter_layer)
        .with(fmt::layer());

    subscriber
        .try_init()
        .map_err(|err| anyhow!(err.to_string()))?;

    // It's fine if another thread set this first; we just ignore the error.
    let _ = LOG_RELOAD_HANDLE.set(handle);
    Ok(())
}

fn duration_from_timeout_ms(timeout_ms: u64) -> Option<Duration> {
    if timeout_ms == 0 {
        None
    } else {
        Some(Duration::from_millis(timeout_ms))
    }
}

fn set_operation_error(err: *mut oro_iroh_error_t, error: OperationError) {
    match error {
        OperationError::Timeout => {
            set_error(err, ORO_IROH_ERROR_CODE_TIMEOUT, "operation timed out");
        }
        OperationError::Failure(message) => {
            set_error(err, -1, message);
        }
    }
}

fn block_on_future_with_timeout<F, Fut, T, E>(
    timeout: Option<Duration>,
    make_future: F,
) -> Result<T, OperationError>
where
    F: FnOnce() -> Fut,
    Fut: Future<Output = Result<T, E>>,
    E: Display,
{
    match timeout {
        Some(duration) => {
            let future = make_future();
            match block_on(tokio::time::timeout(duration, future)) {
                Ok(result) => result.map_err(|e| OperationError::Failure(e.to_string())),
                Err(_) => Err(OperationError::Timeout),
            }
        }
        None => {
            let future = make_future();
            block_on(future).map_err(|e| OperationError::Failure(e.to_string()))
        }
    }
}

async fn send_datagram_with_timeout(
    conn: Arc<IrohConnection>,
    data: Bytes,
    timeout: Option<Duration>,
) -> Result<(), OperationError> {
    let send_once = async {
        conn.send_datagram(data.clone())
            .map_err(|err| OperationError::Failure(err.to_string()))
    };

    if let Some(duration) = timeout {
        match tokio::time::timeout(duration, send_once).await {
            Ok(result) => result,
            Err(_) => Err(OperationError::Timeout),
        }
    } else {
        send_once.await
    }
}

fn block_on<F: std::future::Future>(future: F) -> F::Output {
    RUNTIME.block_on(future)
}

#[repr(C)]
pub struct oro_iroh_error_t {
    pub code: c_int,
    pub message: *mut c_char,
}

impl Default for oro_iroh_error_t {
    fn default() -> Self {
        Self {
            code: 0,
            message: ptr::null_mut(),
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_error_clear(err: *mut oro_iroh_error_t) {
    if let Some(err) = unsafe { err.as_mut() } {
        if !err.message.is_null() {
            unsafe {
                let _ = CString::from_raw(err.message);
            }
        }
        *err = oro_iroh_error_t::default();
    }
}

fn set_error(err: *mut oro_iroh_error_t, code: c_int, message: impl AsRef<str>) {
    if let Some(err) = unsafe { err.as_mut() } {
        oro_iroh_error_clear(err);
        err.code = code;
        err.message = CString::new(message.as_ref())
            .map(|s| s.into_raw())
            .unwrap_or(ptr::null_mut());
    }
}

fn clear_or_init_error(err: *mut oro_iroh_error_t) {
    if !err.is_null() {
        oro_iroh_error_clear(err);
    }
}

#[repr(C)]
pub struct oro_iroh_bytes_t {
    pub data: *mut u8,
    pub len: usize,
}

#[no_mangle]
pub extern "C" fn oro_iroh_bytes_free(bytes: *mut oro_iroh_bytes_t) {
    if let Some(bytes) = unsafe { bytes.as_mut() } {
        if !bytes.data.is_null() && bytes.len > 0 {
            unsafe {
                let slice = slice::from_raw_parts_mut(bytes.data, bytes.len);
                let _ = Vec::from_raw_parts(slice.as_mut_ptr(), bytes.len, bytes.len);
            }
        }
        bytes.data = ptr::null_mut();
        bytes.len = 0;
    }
}

fn alloc_bytes(buffer: Vec<u8>) -> oro_iroh_bytes_t {
    let mut buffer = buffer;
    let bytes = oro_iroh_bytes_t {
        data: buffer.as_mut_ptr(),
        len: buffer.len(),
    };
    std::mem::forget(buffer);
    bytes
}

fn cstring_from_ptr(ptr: *const c_char) -> Result<Option<String>> {
    if ptr.is_null() {
        return Ok(None);
    }
    unsafe {
        CStr::from_ptr(ptr)
            .to_str()
            .map(|s| Some(s.to_owned()))
            .map_err(|e| e.into())
    }
}

fn log_level_from_int(level: c_int) -> Option<LogLevel> {
    match level {
        0 => Some(LogLevel::Trace),
        1 => Some(LogLevel::Debug),
        2 => Some(LogLevel::Info),
        3 => Some(LogLevel::Warn),
        4 => Some(LogLevel::Error),
        5 => Some(LogLevel::Off),
        _ => None,
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_set_log_level(level: c_int, err: *mut oro_iroh_error_t) -> bool {
    clear_or_init_error(err);

    let Some(level) = log_level_from_int(level) else {
        set_error(err, -1, "invalid log level");
        return false;
    };

    if let Err(e) = set_log_level(level) {
        set_error(err, -1, e.to_string());
        return false;
    }

    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_path_to_key(
    path: *const c_char,
    prefix: *const c_char,
    root: *const c_char,
    out_bytes: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out) = (unsafe { out_bytes.as_mut() }) else {
        set_error(err, -1, "out_bytes must not be null");
        return false;
    };

    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(s) => s.to_owned(),
        Err(e) => {
            set_error(err, -1, format!("invalid path: {e}"));
            return false;
        }
    };

    let prefix = match cstring_from_ptr(prefix) {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid prefix: {e}"));
            return false;
        }
    };

    let root = match cstring_from_ptr(root) {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid root: {e}"));
            return false;
        }
    };

    let root_path = root.clone().map(PathBuf::from);
    match blob_fs::path_to_key(path, prefix, root_path) {
        Ok(bytes) => {
            *out = alloc_bytes(bytes.to_vec());
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_key_to_path(
    key_data: *const u8,
    key_len: usize,
    prefix: *const c_char,
    root: *const c_char,
    out_path: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    if key_data.is_null() {
        set_error(err, -1, "key_data must not be null");
        return false;
    }
    if out_path.is_null() {
        set_error(err, -1, "out_path must not be null");
        return false;
    }

    let key = unsafe { slice::from_raw_parts(key_data, key_len) }.to_vec();

    let prefix = match cstring_from_ptr(prefix) {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid prefix: {e}"));
            return false;
        }
    };

    let root = match cstring_from_ptr(root) {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid root: {e}"));
            return false;
        }
    };

    let root_path = root.clone().map(PathBuf::from);
    match blob_fs::key_to_path(key, prefix, root_path) {
        Ok(path) => match CString::new(path.to_string_lossy().into_owned()) {
            Ok(cstr) => {
                unsafe {
                    *out_path = cstr.into_raw();
                }
                true
            }
            Err(e) => {
                set_error(err, -1, format!("invalid path string: {e}"));
                false
            }
        },
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_string_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe {
            let _ = CString::from_raw(ptr);
        }
    }
}

#[derive(Clone, Copy, Debug)]
enum NodeDiscoveryConfig {
    Default,
    None,
}

#[derive(Debug, Default)]
struct NodeOptions {
    enable_docs: bool,
    gc_interval_millis: Option<u64>,
    ipv4_addr: Option<String>,
    ipv6_addr: Option<String>,
    node_discovery: Option<NodeDiscoveryConfig>,
    secret_key: Option<Vec<u8>>,
}

#[repr(C)]
pub struct oro_iroh_node_options_t {
    pub enable_docs: bool,
    pub has_gc_interval: bool,
    pub gc_interval_millis: u64,
    pub ipv4_addr: *const c_char,
    pub ipv6_addr: *const c_char,
    /// 0 = default, 1 = none
    pub discovery: c_int,
    pub secret_key: *const u8,
    pub secret_key_len: usize,
}

fn convert_node_options(opts: *const oro_iroh_node_options_t) -> Result<NodeOptions> {
    if opts.is_null() {
        return Ok(NodeOptions {
            gc_interval_millis: Some(0),
            enable_docs: false,
            ipv4_addr: None,
            ipv6_addr: None,
            node_discovery: None,
            secret_key: None,
        });
    }

    let opts = unsafe { &*opts };

    let discovery = match opts.discovery {
        1 => Some(NodeDiscoveryConfig::None),
        _ => Some(NodeDiscoveryConfig::Default),
    };

    let secret_key = if !opts.secret_key.is_null() && opts.secret_key_len > 0 {
        let slice = unsafe { slice::from_raw_parts(opts.secret_key, opts.secret_key_len) };
        Some(slice.to_vec())
    } else {
        None
    };

    let ipv4_addr = cstring_from_ptr(opts.ipv4_addr)?;
    let ipv6_addr = cstring_from_ptr(opts.ipv6_addr)?;

    Ok(NodeOptions {
        gc_interval_millis: if opts.has_gc_interval {
            Some(opts.gc_interval_millis)
        } else {
            Some(0)
        },
        enable_docs: opts.enable_docs,
        ipv4_addr,
        ipv6_addr,
        node_discovery: discovery,
        secret_key,
    })
}

async fn create_endpoint(opts: NodeOptions) -> Result<Endpoint> {
    let mut builder = Endpoint::builder();

    if let Some(secret_key) = opts.secret_key {
        let bytes: [u8; 32] = secret_key
            .as_slice()
            .try_into()
            .map_err(|_| anyhow!("secret_key must be exactly 32 bytes"))?;
        let secret = SecretKey::from_bytes(&bytes);
        builder = builder.secret_key(secret);
    }

    if let Some(addr) = opts.ipv4_addr {
        let socket: SocketAddr = addr
            .parse()
            .map_err(|e| anyhow!("invalid IPv4 bind address: {e}"))?;
        match socket {
            SocketAddr::V4(v4) => {
                builder = builder.bind_addr_v4(v4);
            }
            SocketAddr::V6(_) => {
                return Err(anyhow!("ipv4_addr must be an IPv4 socket address"));
            }
        }
    }

    if let Some(addr) = opts.ipv6_addr {
        let socket: SocketAddr = addr
            .parse()
            .map_err(|e| anyhow!("invalid IPv6 bind address: {e}"))?;
        match socket {
            SocketAddr::V6(v6) => {
                builder = builder.bind_addr_v6(v6);
            }
            SocketAddr::V4(_) => {
                return Err(anyhow!("ipv6_addr must be an IPv6 socket address"));
            }
        }
    }

    match opts.node_discovery.unwrap_or(NodeDiscoveryConfig::Default) {
        NodeDiscoveryConfig::Default => {
            builder = builder.discovery_n0();
        }
        NodeDiscoveryConfig::None => {
            builder = builder.clear_discovery();
        }
    }

    builder.bind().await.map_err(|err| anyhow!(err.to_string()))
}

struct NodeHandle {
    inner: Arc<Endpoint>,
}

struct NetHandle {
    inner: Arc<Endpoint>,
}

struct EndpointHandle {
    inner: Arc<Endpoint>,
}

struct ConnectionHandle {
    inner: Arc<IrohConnection>,
}

struct NodeAddrHandle {
    inner: Arc<NodeAddr>,
}

struct SendStreamHandle {
    inner: Arc<TokioMutex<SendStream>>,
}

struct RecvStreamHandle {
    inner: Arc<TokioMutex<RecvStream>>,
}

fn new_send_stream_handle(stream: SendStream) -> *mut oro_iroh_send_stream_t {
    let handle = Box::new(SendStreamHandle {
        inner: Arc::new(TokioMutex::new(stream)),
    });
    Box::into_raw(handle) as *mut oro_iroh_send_stream_t
}

fn new_recv_stream_handle(stream: RecvStream) -> *mut oro_iroh_recv_stream_t {
    let handle = Box::new(RecvStreamHandle {
        inner: Arc::new(TokioMutex::new(stream)),
    });
    Box::into_raw(handle) as *mut oro_iroh_recv_stream_t
}

#[repr(C)]
pub struct oro_iroh_node_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_net_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_endpoint_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_connection_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_node_addr_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_send_stream_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_recv_stream_t {
    _private: [u8; 0],
}

#[repr(C)]
pub struct oro_iroh_connection_stats_t {
    pub max_datagram_size: u64,
    pub rtt_micros: u64,
    pub lost_packets: u64,
    pub sent_packets: u64,
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_memory(
    options: *const oro_iroh_node_options_t,
    out_node: *mut *mut oro_iroh_node_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    if out_node.is_null() {
        set_error(err, -1, "out_node must not be null");
        return false;
    }

    let opts = match convert_node_options(options) {
        Ok(opts) => opts,
        Err(e) => {
            set_error(err, -1, e.to_string());
            return false;
        }
    };

    let endpoint = match block_on(create_endpoint(opts)) {
        Ok(endpoint) => endpoint,
        Err(e) => {
            set_error(err, -1, e.to_string());
            return false;
        }
    };

    let handle = Box::new(NodeHandle {
        inner: Arc::new(endpoint),
    });

    unsafe {
        *out_node = Box::into_raw(handle) as *mut oro_iroh_node_t;
    }

    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_free(node: *mut oro_iroh_node_t) {
    if node.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(node as *mut NodeHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_net(
    node: *mut oro_iroh_node_t,
    out_net: *mut *mut oro_iroh_net_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_net) = (unsafe { out_net.as_mut() }) else {
        set_error(err, -1, "out_net must not be null");
        return false;
    };

    let Some(node_handle) = (unsafe { (node as *mut NodeHandle).as_ref() }) else {
        set_error(err, -1, "node must not be null");
        return false;
    };

    let handle = Box::new(NetHandle {
        inner: Arc::clone(&node_handle.inner),
    });

    *out_net = Box::into_raw(handle) as *mut oro_iroh_net_t;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_endpoint(
    node: *mut oro_iroh_node_t,
    out_endpoint: *mut *mut oro_iroh_endpoint_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_endpoint) = (unsafe { out_endpoint.as_mut() }) else {
        set_error(err, -1, "out_endpoint must not be null");
        return false;
    };

    let Some(node_handle) = (unsafe { (node as *mut NodeHandle).as_ref() }) else {
        set_error(err, -1, "node must not be null");
        return false;
    };

    let handle = Box::new(EndpointHandle {
        inner: Arc::clone(&node_handle.inner),
    });

    *out_endpoint = Box::into_raw(handle) as *mut oro_iroh_endpoint_t;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_free(endpoint: *mut oro_iroh_endpoint_t) {
    if endpoint.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(endpoint as *mut EndpointHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_set_alpns(
    endpoint: *mut oro_iroh_endpoint_t,
    alpns: *const oro_iroh_bytes_t,
    alpns_len: usize,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    if alpns_len > 0 && alpns.is_null() {
        set_error(err, -1, "alpns must not be null when alpns_len > 0");
        return false;
    }

    let mut values = Vec::with_capacity(alpns_len);
    for i in 0..alpns_len {
        // SAFETY: bounds checked above and pointer validated for non-null when len > 0.
        let raw = unsafe { &*alpns.add(i) };
        if raw.len == 0 || raw.data.is_null() {
            values.push(Vec::new());
        } else {
            // SAFETY: caller guarantees the slice is valid for the provided length.
            let bytes = unsafe { slice::from_raw_parts(raw.data, raw.len) };
            values.push(bytes.to_vec());
        }
    }

    endpoint_handle.inner.set_alpns(values);
    true
}

unsafe fn take_connection_handle(conn: IrohConnection) -> *mut oro_iroh_connection_t {
    Box::into_raw(Box::new(ConnectionHandle {
        inner: Arc::new(conn),
    })) as *mut oro_iroh_connection_t
}

fn build_alpn_bytes(alpn: Option<Vec<u8>>) -> oro_iroh_bytes_t {
    match alpn {
        Some(bytes) if !bytes.is_empty() => alloc_bytes(bytes),
        _ => oro_iroh_bytes_t {
            data: ptr::null_mut(),
            len: 0,
        },
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_accept(
    endpoint: *mut oro_iroh_endpoint_t,
    expected_alpn: *const u8,
    expected_alpn_len: usize,
    out_conn: *mut *mut oro_iroh_connection_t,
    out_negotiated_alpn: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_conn) = (unsafe { out_conn.as_mut() }) else {
        set_error(err, -1, "out_conn must not be null");
        return false;
    };

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    if expected_alpn_len > 0 && expected_alpn.is_null() {
        set_error(
            err,
            -1,
            "expected_alpn must not be null when expected_alpn_len > 0",
        );
        return false;
    }

    let expected = if expected_alpn_len > 0 {
        // SAFETY: validated above.
        let slice = unsafe { slice::from_raw_parts(expected_alpn, expected_alpn_len) };
        Some(slice.to_vec())
    } else {
        None
    };

    match endpoint_accept_common(endpoint_handle, expected.as_deref()) {
        Ok((conn, negotiated_alpn)) => {
            unsafe {
                *out_conn = take_connection_handle(conn);
            }

            if let Some(out_bytes) = unsafe { out_negotiated_alpn.as_mut() } {
                *out_bytes = build_alpn_bytes(negotiated_alpn);
            }

            true
        }
        Err(message) => {
            set_error(err, -1, message);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_accept_any(
    endpoint: *mut oro_iroh_endpoint_t,
    out_conn: *mut *mut oro_iroh_connection_t,
    out_negotiated_alpn: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_conn) = (unsafe { out_conn.as_mut() }) else {
        set_error(err, -1, "out_conn must not be null");
        return false;
    };

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    match endpoint_accept_common(endpoint_handle, None) {
        Ok((conn, negotiated_alpn)) => {
            unsafe {
                *out_conn = take_connection_handle(conn);
            }

            if let Some(out_bytes) = unsafe { out_negotiated_alpn.as_mut() } {
                *out_bytes = build_alpn_bytes(negotiated_alpn);
            }

            true
        }
        Err(message) => {
            set_error(err, -1, message);
            false
        }
    }
}

fn endpoint_accept_common(
    endpoint_handle: &EndpointHandle,
    expected_alpn: Option<&[u8]>,
) -> Result<(IrohConnection, Option<Vec<u8>>), String> {
    let incoming =
        block_on(endpoint_handle.inner.accept()).ok_or_else(|| "endpoint closed".to_string())?;

    let connecting = incoming.accept().map_err(|e| e.to_string())?;

    let conn = block_on(connecting).map_err(|e| e.to_string())?;

    let negotiated_alpn = conn.alpn();

    if let Some(expected) = expected_alpn {
        match &negotiated_alpn {
            Some(alpn) if alpn.as_slice() == expected => {}
            _ => {
                return Err("ALPN mismatch".to_string());
            }
        }
    }

    Ok((conn, negotiated_alpn))
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_connect(
    endpoint: *mut oro_iroh_endpoint_t,
    addr: *const oro_iroh_node_addr_t,
    alpn_data: *const u8,
    alpn_len: usize,
    out_conn: *mut *mut oro_iroh_connection_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_conn) = (unsafe { out_conn.as_mut() }) else {
        set_error(err, -1, "out_conn must not be null");
        return false;
    };

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    let alpn = if alpn_data.is_null() || alpn_len == 0 {
        Vec::new()
    } else {
        unsafe { slice::from_raw_parts(alpn_data, alpn_len) }.to_vec()
    };

    let addr = (*addr_handle.inner).clone();
    match block_on(endpoint_handle.inner.connect(addr, &alpn)) {
        Ok(conn) => {
            let handle = Box::new(ConnectionHandle {
                inner: Arc::new(conn),
            });
            *out_conn = Box::into_raw(handle) as *mut oro_iroh_connection_t;
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_free(conn: *mut oro_iroh_connection_t) {
    if conn.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(conn as *mut ConnectionHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_close(
    endpoint: *mut oro_iroh_endpoint_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    block_on(endpoint_handle.inner.close());
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_network_change(
    endpoint: *mut oro_iroh_endpoint_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    block_on(endpoint_handle.inner.network_change());
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_close(
    conn: *mut oro_iroh_connection_t,
    error_code: u64,
    reason_data: *const u8,
    reason_len: usize,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    let reason = if reason_data.is_null() || reason_len == 0 {
        Vec::new()
    } else {
        unsafe { slice::from_raw_parts(reason_data, reason_len) }.to_vec()
    };

    let error_code = match VarInt::try_from(error_code) {
        Ok(code) => code,
        Err(e) => {
            set_error(err, -1, format!("invalid error code: {e}"));
            return false;
        }
    };

    conn_handle.inner.close(error_code, &reason);
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_closed(
    conn: *mut oro_iroh_connection_t,
    out_reason: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_reason) = (unsafe { out_reason.as_mut() }) else {
        set_error(err, -1, "out_reason must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    let reason = block_on(conn_handle.inner.closed());
    match CString::new(reason.to_string()) {
        Ok(cstr) => {
            *out_reason = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid close reason: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_wait_closed(
    conn: *mut oro_iroh_connection_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match block_on(conn_handle.inner.closed()) {
        reason => {
            // Swallow the reason; caller can query closedReason separately.
            let _ = reason;
            true
        }
    }
}

#[repr(C)]
pub struct oro_iroh_conn_type_watcher_t {
    cancel: StdMutex<Option<oneshot::Sender<()>>>,
}

const ENDPOINT_RESULT_OK: i32 = 0;

fn map_connection_type(value: &EndpointConnectionType) -> (i32, Option<String>, Option<String>) {
    match value {
        EndpointConnectionType::Direct(addr) => (0, Some(addr.to_string()), None),
        EndpointConnectionType::Relay(url) => (1, None, Some(url.to_string())),
        EndpointConnectionType::Mixed(addr, url) => {
            (2, Some(addr.to_string()), Some(url.to_string()))
        }
        EndpointConnectionType::None => (3, None, None),
    }
}

fn emit_connection_type_callback(
    callback: extern "C" fn(*mut c_void, i32, i32, *const c_char, *const c_char),
    user_data: *mut c_void,
    result_code: i32,
    type_code: i32,
    direct: Option<String>,
    relay: Option<String>,
) {
    let direct_c = direct.and_then(|s| CString::new(s).ok());
    let relay_c = relay.and_then(|s| CString::new(s).ok());
    let direct_ptr = direct_c.as_ref().map_or(ptr::null(), |c| c.as_ptr());
    let relay_ptr = relay_c.as_ref().map_or(ptr::null(), |c| c.as_ptr());
    callback(user_data, result_code, type_code, direct_ptr, relay_ptr);
}

#[no_mangle]
pub extern "C" fn oro_iroh_endpoint_watch_connection_type(
    endpoint: *mut oro_iroh_endpoint_t,
    node_id: *const c_char,
    callback: Option<extern "C" fn(*mut c_void, i32, i32, *const c_char, *const c_char)>,
    user_data: *mut c_void,
    out_watcher: *mut *mut oro_iroh_conn_type_watcher_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(cb) = callback else {
        set_error(err, -1, "callback must not be null");
        return false;
    };

    let Some(out_ptr) = (unsafe { out_watcher.as_mut() }) else {
        set_error(err, -1, "out_watcher must not be null");
        return false;
    };

    let Some(endpoint_handle) = (unsafe { (endpoint as *mut EndpointHandle).as_ref() }) else {
        set_error(err, -1, "endpoint must not be null");
        return false;
    };

    let node_id_cstr = match unsafe { CStr::from_ptr(node_id) }.to_str() {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid node id: {e}"));
            return false;
        }
    };

    let node_id = match NodeId::from_str(node_id_cstr) {
        Ok(id) => id,
        Err(e) => {
            set_error(err, -1, format!("invalid node id: {e}"));
            return false;
        }
    };

    let endpoint_arc = endpoint_handle.inner.clone();
    let mut direct_current = endpoint_arc.conn_type(node_id.clone());
    let initial_value = direct_current
        .as_mut()
        .map(|direct| direct.get())
        .unwrap_or(EndpointConnectionType::None);

    let (initial_type_code, initial_direct, initial_relay) = map_connection_type(&initial_value);
    emit_connection_type_callback(
        cb,
        user_data,
        ENDPOINT_RESULT_OK,
        initial_type_code,
        initial_direct,
        initial_relay,
    );

    let (tx, mut rx) = oneshot::channel();
    let watcher = Box::new(oro_iroh_conn_type_watcher_t {
        cancel: StdMutex::new(Some(tx)),
    });
    let watcher_ptr = Box::into_raw(watcher);
    *out_ptr = watcher_ptr;

    let cb_ptr = cb;
    let user_data_ptr = user_data as usize;

    let mut stream_opt = direct_current.map(|direct| direct.stream());
    RUNTIME.spawn(async move {
        let cb = cb_ptr;
        let user_data_ptr = user_data_ptr;
        let mut last_sent = initial_value;
        if let Some(mut stream) = stream_opt.take() {
            loop {
                tokio::select! {
                    _ = &mut rx => break,
                    value = stream.next() => {
                        let Some(current) = value else { break; };
                        if current != last_sent {
                            let (type_code, direct, relay) = map_connection_type(&current);
                            emit_connection_type_callback(cb, user_data_ptr as *mut c_void, ENDPOINT_RESULT_OK, type_code, direct, relay);
                            last_sent = current;
                        }
                    }
                }
            }
        } else {
            loop {
                tokio::select! {
                    _ = &mut rx => break,
                    _ = sleep(Duration::from_millis(500)) => {
                        let mut current_watcher = endpoint_arc.conn_type(node_id.clone());
                        let current = current_watcher
                            .as_mut()
                            .map(|direct| direct.get())
                            .unwrap_or(EndpointConnectionType::None);

                        if current != last_sent {
                            let (type_code, direct, relay) = map_connection_type(&current);
                            emit_connection_type_callback(cb, user_data_ptr as *mut c_void, ENDPOINT_RESULT_OK, type_code, direct, relay);
                            last_sent = current;
                        }
                    }
                }
            }
        }
    });

    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_conn_type_watcher_cancel(
    watcher: *mut oro_iroh_conn_type_watcher_t,
) {
    if watcher.is_null() {
        return;
    }

    let watcher = unsafe { Box::from_raw(watcher) };
    if let Some(sender) = watcher
        .cancel
        .lock()
        .ok()
        .and_then(|mut guard| guard.take())
    {
        let _ = sender.send(());
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_send_datagram(
    conn: *mut oro_iroh_connection_t,
    data: *const u8,
    len: usize,
    timeout_ms: u64,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    let bytes = if data.is_null() || len == 0 {
        Bytes::new()
    } else {
        let slice = unsafe { slice::from_raw_parts(data, len) };
        Bytes::copy_from_slice(slice)
    };

    let timeout = duration_from_timeout_ms(timeout_ms);
    match block_on(send_datagram_with_timeout(
        conn_handle.inner.clone(),
        bytes,
        timeout,
    )) {
        Ok(()) => true,
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_read_datagram(
    conn: *mut oro_iroh_connection_t,
    timeout_ms: u64,
    out_bytes: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_bytes) = (unsafe { out_bytes.as_mut() }) else {
        set_error(err, -1, "out_bytes must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    let timeout = duration_from_timeout_ms(timeout_ms);
    match block_on_future_with_timeout(timeout, || conn_handle.inner.read_datagram()) {
        Ok(bytes) => {
            *out_bytes = alloc_bytes(bytes.to_vec());
            true
        }
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_max_datagram_size(
    conn: *mut oro_iroh_connection_t,
    out_size: *mut usize,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_size) = (unsafe { out_size.as_mut() }) else {
        set_error(err, -1, "out_size must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    *out_size = conn_handle.inner.max_datagram_size().unwrap_or(0) as usize;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_open_bi(
    conn: *mut oro_iroh_connection_t,
    out_send: *mut *mut oro_iroh_send_stream_t,
    out_recv: *mut *mut oro_iroh_recv_stream_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_send) = (unsafe { out_send.as_mut() }) else {
        set_error(err, -1, "out_send must not be null");
        return false;
    };

    let Some(out_recv) = (unsafe { out_recv.as_mut() }) else {
        set_error(err, -1, "out_recv must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match block_on(conn_handle.inner.open_bi()) {
        Ok((send, recv)) => {
            *out_send = new_send_stream_handle(send);
            *out_recv = new_recv_stream_handle(recv);
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_open_uni(
    conn: *mut oro_iroh_connection_t,
    out_send: *mut *mut oro_iroh_send_stream_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_send) = (unsafe { out_send.as_mut() }) else {
        set_error(err, -1, "out_send must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match block_on(conn_handle.inner.open_uni()) {
        Ok(stream) => {
            *out_send = new_send_stream_handle(stream);
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_accept_bi(
    conn: *mut oro_iroh_connection_t,
    out_send: *mut *mut oro_iroh_send_stream_t,
    out_recv: *mut *mut oro_iroh_recv_stream_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_send) = (unsafe { out_send.as_mut() }) else {
        set_error(err, -1, "out_send must not be null");
        return false;
    };

    let Some(out_recv) = (unsafe { out_recv.as_mut() }) else {
        set_error(err, -1, "out_recv must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match block_on(conn_handle.inner.accept_bi()) {
        Ok((send, recv)) => {
            *out_send = new_send_stream_handle(send);
            *out_recv = new_recv_stream_handle(recv);
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_accept_uni(
    conn: *mut oro_iroh_connection_t,
    out_recv: *mut *mut oro_iroh_recv_stream_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_recv) = (unsafe { out_recv.as_mut() }) else {
        set_error(err, -1, "out_recv must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match block_on(conn_handle.inner.accept_uni()) {
        Ok(stream) => {
            *out_recv = new_recv_stream_handle(stream);
            true
        }
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_alpn(
    conn: *mut oro_iroh_connection_t,
    out_present: *mut bool,
    out_alpn: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_present) = (unsafe { out_present.as_mut() }) else {
        set_error(err, -1, "out_present must not be null");
        return false;
    };

    let Some(out_alpn) = (unsafe { out_alpn.as_mut() }) else {
        set_error(err, -1, "out_alpn must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    match conn_handle.inner.alpn() {
        Some(alpn) => {
            *out_present = true;
            *out_alpn = alloc_bytes(alpn);
            true
        }
        None => {
            *out_present = false;
            out_alpn.data = ptr::null_mut();
            out_alpn.len = 0;
            true
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_rtt(
    conn: *mut oro_iroh_connection_t,
    out_rtt: *mut u64,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_rtt) = (unsafe { out_rtt.as_mut() }) else {
        set_error(err, -1, "out_rtt must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    *out_rtt = conn_handle.inner.rtt().as_micros() as u64;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_connection_stats(
    conn: *mut oro_iroh_connection_t,
    out_stats: *mut oro_iroh_connection_stats_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_stats) = (unsafe { out_stats.as_mut() }) else {
        set_error(err, -1, "out_stats must not be null");
        return false;
    };

    let Some(conn_handle) = (unsafe { (conn as *mut ConnectionHandle).as_ref() }) else {
        set_error(err, -1, "connection must not be null");
        return false;
    };

    let stats = conn_handle.inner.stats();
    let path = stats.path;
    let rtt_micros_raw = path.rtt.as_micros();
    let rtt_micros = if rtt_micros_raw > u64::MAX as u128 {
        u64::MAX
    } else {
        rtt_micros_raw as u64
    };

    let max_datagram_size = conn_handle.inner.max_datagram_size().unwrap_or(0) as u64;

    *out_stats = oro_iroh_connection_stats_t {
        max_datagram_size,
        rtt_micros,
        lost_packets: path.lost_packets,
        sent_packets: path.sent_packets,
    };

    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_send_stream_free(stream: *mut oro_iroh_send_stream_t) {
    if stream.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(stream as *mut SendStreamHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_send_stream_write(
    stream: *mut oro_iroh_send_stream_t,
    data: *const u8,
    len: usize,
    timeout_ms: u64,
    out_written: *mut u64,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_written) = (unsafe { out_written.as_mut() }) else {
        set_error(err, -1, "out_written must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut SendStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let data = if data.is_null() || len == 0 {
        Vec::new()
    } else {
        unsafe { slice::from_raw_parts(data, len) }.to_vec()
    };

    let data = Arc::new(data);
    let timeout = duration_from_timeout_ms(timeout_ms);
    let stream = Arc::clone(&stream_handle.inner);
    match block_on_future_with_timeout(timeout, move || {
        let stream = Arc::clone(&stream);
        let data = Arc::clone(&data);
        async move {
            let mut guard = stream.lock().await;
            guard.write(&data[..]).await
        }
    }) {
        Ok(written) => {
            *out_written = written as u64;
            true
        }
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_send_stream_write_all(
    stream: *mut oro_iroh_send_stream_t,
    data: *const u8,
    len: usize,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(stream_handle) = (unsafe { (stream as *mut SendStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let data = if data.is_null() || len == 0 {
        Vec::new()
    } else {
        unsafe { slice::from_raw_parts(data, len) }.to_vec()
    };

    let data = Arc::new(data);
    let stream = Arc::clone(&stream_handle.inner);

    match block_on(async move {
        let mut guard = stream.lock().await;
        guard.write_all(&data[..]).await
    }) {
        Ok(()) => true,
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_send_stream_finish(
    stream: *mut oro_iroh_send_stream_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(stream_handle) = (unsafe { (stream as *mut SendStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let mut guard = stream_handle.inner.blocking_lock();
    match guard.finish() {
        Ok(()) => true,
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_send_stream_id(
    stream: *mut oro_iroh_send_stream_t,
    out_id: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_id) = (unsafe { out_id.as_mut() }) else {
        set_error(err, -1, "out_id must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut SendStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let stream = Arc::clone(&stream_handle.inner);
    let id = block_on(async move {
        let guard = stream.lock().await;
        guard.id()
    });

    match CString::new(id.to_string()) {
        Ok(cstr) => {
            *out_id = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid stream id: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_free(stream: *mut oro_iroh_recv_stream_t) {
    if stream.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(stream as *mut RecvStreamHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_read(
    stream: *mut oro_iroh_recv_stream_t,
    size_limit: u32,
    timeout_ms: u64,
    out_bytes: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_bytes) = (unsafe { out_bytes.as_mut() }) else {
        set_error(err, -1, "out_bytes must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let timeout = duration_from_timeout_ms(timeout_ms);
    let stream = Arc::clone(&stream_handle.inner);
    let limit = size_limit as usize;
    match block_on_future_with_timeout(timeout, move || {
        let stream = Arc::clone(&stream);
        async move {
            let mut guard = stream.lock().await;
            guard.read_chunk(limit, true).await
        }
    }) {
        Ok(Some(chunk)) => {
            *out_bytes = alloc_bytes(chunk.bytes.to_vec());
            true
        }
        Ok(None) => {
            *out_bytes = alloc_bytes(Vec::new());
            true
        }
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_read_to_end(
    stream: *mut oro_iroh_recv_stream_t,
    size_limit: u32,
    timeout_ms: u64,
    out_bytes: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_bytes) = (unsafe { out_bytes.as_mut() }) else {
        set_error(err, -1, "out_bytes must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let timeout = duration_from_timeout_ms(timeout_ms);
    let stream = Arc::clone(&stream_handle.inner);
    let limit = size_limit as usize;
    match block_on_future_with_timeout(timeout, move || {
        let stream = Arc::clone(&stream);
        async move {
            let mut guard = stream.lock().await;
            guard.read_to_end(limit).await
        }
    }) {
        Ok(bytes) => {
            *out_bytes = alloc_bytes(bytes.to_vec());
            true
        }
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_read_exact(
    stream: *mut oro_iroh_recv_stream_t,
    size: u32,
    timeout_ms: u64,
    out_bytes: *mut oro_iroh_bytes_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_bytes) = (unsafe { out_bytes.as_mut() }) else {
        set_error(err, -1, "out_bytes must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let timeout = duration_from_timeout_ms(timeout_ms);
    let stream = Arc::clone(&stream_handle.inner);
    let size_usize = size as usize;
    match block_on_future_with_timeout(timeout, move || {
        let stream = Arc::clone(&stream);
        let mut buffer = vec![0u8; size_usize];
        async move {
            let mut guard = stream.lock().await;
            guard.read_exact(&mut buffer).await?;
            Ok::<Vec<u8>, ReadExactError>(buffer)
        }
    }) {
        Ok(bytes) => {
            *out_bytes = alloc_bytes(bytes);
            true
        }
        Err(error) => {
            set_operation_error(err, error);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_stop(
    stream: *mut oro_iroh_recv_stream_t,
    error_code: u64,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let varint = match VarInt::try_from(error_code) {
        Ok(value) => value,
        Err(e) => {
            set_error(err, -1, format!("invalid error code: {e}"));
            return false;
        }
    };

    let mut guard = stream_handle.inner.blocking_lock();
    match guard.stop(varint) {
        Ok(()) => true,
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_received_reset(
    stream: *mut oro_iroh_recv_stream_t,
    out_present: *mut bool,
    out_code: *mut u64,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_present) = (unsafe { out_present.as_mut() }) else {
        set_error(err, -1, "out_present must not be null");
        return false;
    };

    let Some(out_code) = (unsafe { out_code.as_mut() }) else {
        set_error(err, -1, "out_code must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let stream = Arc::clone(&stream_handle.inner);
    match block_on_future_with_timeout(None, move || {
        let stream = Arc::clone(&stream);
        async move {
            let mut guard = stream.lock().await;
            guard.received_reset().await
        }
    }) {
        Ok(Some(code)) => {
            *out_present = true;
            *out_code = u64::from(code);
            true
        }
        Ok(None) => {
            *out_present = false;
            *out_code = 0;
            true
        }
        Err(e) => {
            set_operation_error(err, e);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_recv_stream_id(
    stream: *mut oro_iroh_recv_stream_t,
    out_id: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_id) = (unsafe { out_id.as_mut() }) else {
        set_error(err, -1, "out_id must not be null");
        return false;
    };

    let Some(stream_handle) = (unsafe { (stream as *mut RecvStreamHandle).as_ref() }) else {
        set_error(err, -1, "stream must not be null");
        return false;
    };

    let stream = Arc::clone(&stream_handle.inner);
    let id = block_on(async move {
        let guard = stream.lock().await;
        guard.id()
    });

    match CString::new(id.to_string()) {
        Ok(cstr) => {
            *out_id = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid stream id: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_net_free(net: *mut oro_iroh_net_t) {
    if net.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(net as *mut NetHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_net_node_id(
    net: *mut oro_iroh_net_t,
    out_str: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_str) = (unsafe { out_str.as_mut() }) else {
        set_error(err, -1, "out_str must not be null");
        return false;
    };

    let Some(net_handle) = (unsafe { (net as *mut NetHandle).as_ref() }) else {
        set_error(err, -1, "net must not be null");
        return false;
    };

    let node_id = net_handle.inner.node_id().to_string();
    match CString::new(node_id) {
        Ok(cstr) => {
            *out_str = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid node id string: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_net_node_addr(
    net: *mut oro_iroh_net_t,
    out_addr: *mut *mut oro_iroh_node_addr_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_addr) = (unsafe { out_addr.as_mut() }) else {
        set_error(err, -1, "out_addr must not be null");
        return false;
    };

    let Some(net_handle) = (unsafe { (net as *mut NetHandle).as_ref() }) else {
        set_error(err, -1, "net must not be null");
        return false;
    };

    let addr = net_handle.inner.node_addr();
    let handle = Box::new(NodeAddrHandle {
        inner: Arc::new(addr),
    });
    *out_addr = Box::into_raw(handle) as *mut oro_iroh_node_addr_t;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_net_add_node_addr(
    net: *mut oro_iroh_net_t,
    addr: *const oro_iroh_node_addr_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(net_handle) = (unsafe { (net as *mut NetHandle).as_ref() }) else {
        set_error(err, -1, "net must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    let addr = (*addr_handle.inner).clone();
    match net_handle.inner.add_node_addr_with_source(addr, "ffi") {
        Ok(()) => true,
        Err(e) => {
            set_error(err, -1, e.to_string());
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_net_home_relay(
    net: *mut oro_iroh_net_t,
    out_relay: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_relay) = (unsafe { out_relay.as_mut() }) else {
        set_error(err, -1, "out_relay must not be null");
        return false;
    };

    let Some(net_handle) = (unsafe { (net as *mut NetHandle).as_ref() }) else {
        set_error(err, -1, "net must not be null");
        return false;
    };

    let node_addr = net_handle.inner.node_addr();
    if let Some(relay) = node_addr.relay_url() {
        match CString::new(relay.to_string()) {
            Ok(cstr) => {
                *out_relay = cstr.into_raw();
                true
            }
            Err(e) => {
                set_error(err, -1, format!("invalid relay string: {e}"));
                false
            }
        }
    } else {
        *out_relay = ptr::null_mut();
        true
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_free(addr: *mut oro_iroh_node_addr_t) {
    if addr.is_null() {
        return;
    }
    unsafe {
        drop(Box::from_raw(addr as *mut NodeAddrHandle));
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_from_string(
    value: *const c_char,
    out_addr: *mut *mut oro_iroh_node_addr_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_addr) = (unsafe { out_addr.as_mut() }) else {
        set_error(err, -1, "out_addr must not be null");
        return false;
    };

    if value.is_null() {
        set_error(err, -1, "value must not be null");
        return false;
    }

    let string = match unsafe { CStr::from_ptr(value) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            set_error(err, -1, format!("invalid string: {e}"));
            return false;
        }
    };

    let ticket = match NodeTicket::from_str(string) {
        Ok(ticket) => ticket,
        Err(e) => {
            set_error(err, -1, e.to_string());
            return false;
        }
    };

    let ffi_addr: NodeAddr = ticket.into();
    let handle = Box::new(NodeAddrHandle {
        inner: Arc::new(ffi_addr),
    });
    *out_addr = Box::into_raw(handle) as *mut oro_iroh_node_addr_t;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_from_public_key(
    node_id: *const c_char,
    out_addr: *mut *mut oro_iroh_node_addr_t,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_addr) = (unsafe { out_addr.as_mut() }) else {
        set_error(err, -1, "out_addr must not be null");
        return false;
    };

    if node_id.is_null() {
        set_error(err, -1, "node_id must not be null");
        return false;
    }

    let node_id_str = match unsafe { CStr::from_ptr(node_id) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            set_error(err, -1, format!("invalid public key: {e}"));
            return false;
        }
    };

    let public_key = match PublicKey::from_str(node_id_str) {
        Ok(key) => key,
        Err(e) => {
            set_error(err, -1, e.to_string());
            return false;
        }
    };

    let ffi_addr = NodeAddr::new(public_key);
    let handle = Box::new(NodeAddrHandle {
        inner: Arc::new(ffi_addr),
    });
    *out_addr = Box::into_raw(handle) as *mut oro_iroh_node_addr_t;
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_node_id(
    addr: *const oro_iroh_node_addr_t,
    out_str: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_str) = (unsafe { out_str.as_mut() }) else {
        set_error(err, -1, "out_str must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    match CString::new(addr_handle.inner.node_id.to_string()) {
        Ok(cstr) => {
            *out_str = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid node id string: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_to_string(
    addr: *const oro_iroh_node_addr_t,
    out_str: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_str) = (unsafe { out_str.as_mut() }) else {
        set_error(err, -1, "out_str must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    let ticket: NodeTicket = (*addr_handle.inner).clone().into();

    match CString::new(ticket.to_string()) {
        Ok(cstr) => {
            *out_str = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid address string: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_direct_addresses_len(
    addr: *const oro_iroh_node_addr_t,
    out_len: *mut usize,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_len) = (unsafe { out_len.as_mut() }) else {
        set_error(err, -1, "out_len must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    *out_len = addr_handle.inner.direct_addresses.len();
    true
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_direct_address_at(
    addr: *const oro_iroh_node_addr_t,
    index: usize,
    out_str: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_str) = (unsafe { out_str.as_mut() }) else {
        set_error(err, -1, "out_str must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    let direct_addresses = &addr_handle.inner.direct_addresses;

    if index >= direct_addresses.len() {
        set_error(err, -1, "index out of range");
        return false;
    }

    let addr_str = match direct_addresses.iter().nth(index) {
        Some(addr) => addr.to_string(),
        None => {
            set_error(err, -1, "index out of range");
            return false;
        }
    };

    match CString::new(addr_str) {
        Ok(cstr) => {
            *out_str = cstr.into_raw();
            true
        }
        Err(e) => {
            set_error(err, -1, format!("invalid address string: {e}"));
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn oro_iroh_node_addr_relay_url(
    addr: *const oro_iroh_node_addr_t,
    out_str: *mut *mut c_char,
    err: *mut oro_iroh_error_t,
) -> bool {
    clear_or_init_error(err);

    let Some(out_str) = (unsafe { out_str.as_mut() }) else {
        set_error(err, -1, "out_str must not be null");
        return false;
    };

    let Some(addr_handle) = (unsafe { (addr as *const NodeAddrHandle).as_ref() }) else {
        set_error(err, -1, "addr must not be null");
        return false;
    };

    if let Some(relay) = addr_handle.inner.relay_url.clone() {
        match CString::new(relay.to_string()) {
            Ok(cstr) => {
                *out_str = cstr.into_raw();
                true
            }
            Err(e) => {
                set_error(err, -1, format!("invalid relay string: {e}"));
                false
            }
        }
    } else {
        *out_str = ptr::null_mut();
        true
    }
}


