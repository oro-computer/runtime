/* global Event, ErrorEvent */
/* eslint-disable no-unused-vars */
/**
 * @module extension
 *
 * @example
 * import extension from 'oro:extension'
 * const ex = await extension.load('native_extension')
 * const result = ex.binding.method('argument')
 */
import { isBufferLike } from './util.js'
import { createFile } from './fs/web.js'
import application from './application.js'
import process from './process.js'
import crypto from './crypto.js'
import path from './path.js'
import ipc from './ipc.js'
import fs from './fs/promises.js'

/**
 * @typedef {number} Pointer
 */

const $loaded = Symbol('loaded')
const $stats = Symbol('stats')
const $type = Symbol('type')

const NULL = 0x0
const EOF = -1

// eslint-disable-next-line
const STDIN = 0x0 // STDIN_FILENO
const STDOUT = 0x1 // STDOUT_FILENO
const STDERR = 0x2 // STDERR_FILNO

const EPSILON = 1.1920928955078125e-7

const EXIT_FAILURE = 1
const EXIT_SUCCESS = 0
const RAND_MAX = 0x7fffffff
const PATH_MAX = 4096

const LONG_MIN = -0x80000000
const LONG_MAX = 0x7fffffff
const ULONG_MAX = 0xffffffff
const LONG_MIN_BIG = -0x80000000n
const LONG_MAX_BIG = 0x7fffffffn
const ULONG_MAX_BIG = 0xffffffffn
const LLONG_MIN = -(1n << 63n)
const LLONG_MAX = (1n << 63n) - 1n
const ULLONG_MAX = (1n << 64n) - 1n

const CLOCKS_PER_SEC = 1000

const TIME_UTC = 1
const TIMER_ABSTIME = 1

const CLOCK_REALTIME = 0
const CLOCK_MONOTONIC = 1
const CLOCK_PROCESS_CPUTIME_ID = 2
const CLOCK_THREAD_CPUTIME_ID = 3
const CLOCK_MONOTONIC_RAW = 4
const CLOCK_REALTIME_COARSE = 5
const CLOCK_MONOTONIC_COARSE = 6
const CLOCK_BOOTTIME = 7
const CLOCK_REALTIME_ALARM = 8
const CLOCK_BOOTTIME_ALARM = 9
const CLOCK_SGI_CYCLE = 10
const CLOCK_TAI = 11

const EPERM = 1
const ENOENT = 2
const ESRCH = 3
const EINTR = 4
const EIO = 5
const ENXIO = 6
const E2BIG = 7
const ENOEXEC = 8
const EBADF = 9
const ECHILD = 10
const EAGAIN = 11
const ENOMEM = 12
const EACCES = 13
const EFAULT = 14
const ENOTBLK = 15
const EBUSY = 16
const EEXIST = 17
const EXDEV = 18
const ENODEV = 19
const ENOTDIR = 20
const EISDIR = 21
const EINVAL = 22
const ENFILE = 23
const EMFILE = 24
const ENOTTY = 25
const ETXTBSY = 26
const EFBIG = 27
const ENOSPC = 28
const ESPIPE = 29
const EROFS = 30
const EMLINK = 31
const EPIPE = 32
const EDOM = 33
const ERANGE = 34
const EDEADLK = 35
const ENAMETOOLONG = 36
const ENOLCK = 37
const ENOSYS = 38
const ENOTEMPTY = 39
const ELOOP = 40
const EWOULDBLOCK = EAGAIN
const ENOMSG = 42
const EIDRM = 43
const ECHRNG = 44
const EL2NSYNC = 45
const EL3HLT = 46
const EL3RST = 47
const ELNRNG = 48
const EUNATCH = 49
const ENOCSI = 50
const EL2HLT = 51
const EBADE = 52
const EBADR = 53
const EXFULL = 54
const ENOANO = 55
const EBADRQC = 56
const EBADSLT = 57
const EDEADLOCK = EDEADLK
const EBFONT = 59
const ENOSTR = 60
const ENODATA = 61
const ETIME = 62
const ENOSR = 63
const ENONET = 64
const ENOPKG = 65
const EREMOTE = 66
const ENOLINK = 67
const EADV = 68
const ESRMNT = 69
const ECOMM = 70
const EPROTO = 71
const EMULTIHOP = 72
const EDOTDOT = 73
const EBADMSG = 74
const EOVERFLOW = 75
const ENOTUNIQ = 76
const EBADFD = 77
const EREMCHG = 78
const ELIBACC = 79
const ELIBBAD = 80
const ELIBSCN = 81
const ELIBMAX = 82
const ELIBEXEC = 83
const EILSEQ = 84
const ERESTART = 85
const ESTRPIPE = 86
const EUSERS = 87
const ENOTSOCK = 88
const EDESTADDRREQ = 89
const EMSGSIZE = 90
const EPROTOTYPE = 91
const ENOPROTOOPT = 92
const EPROTONOSUPPORT = 93
const ESOCKTNOSUPPORT = 94
const EOPNOTSUPP = 95
const ENOTSUP = EOPNOTSUPP
const EPFNOSUPPORT = 96
const EAFNOSUPPORT = 97
const EADDRINUSE = 98
const EADDRNOTAVAIL = 99
const ENETDOWN = 100
const ENETUNREACH = 101
const ENETRESET = 102
const ECONNABORTED = 103
const ECONNRESET = 104
const ENOBUFS = 105
const EISCONN = 106
const ENOTCONN = 107
const ESHUTDOWN = 108
const ETOOMANYREFS = 109
const ETIMEDOUT = 110
const ECONNREFUSED = 111
const EHOSTDOWN = 112
const EHOSTUNREACH = 113
const EALREADY = 114
const EINPROGRESS = 115
const ESTALE = 116
const EUCLEAN = 117
const ENOTNAM = 118
const ENAVAIL = 119
const EISNAM = 120
const EREMOTEIO = 121
const EDQUOT = 122
const ENOMEDIUM = 123
const EMEDIUMTYPE = 124
const ECANCELED = 125
const ENOKEY = 126
const EKEYEXPIRED = 127
const EKEYREVOKED = 128
const EKEYREJECTED = 129
const EOWNERDEAD = 130
const ENOTRECOVERABLE = 131
const ERFKILL = 132
const EHWPOISON = 133

const OAPI_JSON_TYPE_EMPTY = -1
const OAPI_JSON_TYPE_ANY = 0
const OAPI_JSON_TYPE_NULL = 1
const OAPI_JSON_TYPE_OBJECT = 2
const OAPI_JSON_TYPE_ARRAY = 3
const OAPI_JSON_TYPE_BOOLEAN = 4
const OAPI_JSON_TYPE_NUMBER = 5
const OAPI_JSON_TYPE_STRING = 6
const OAPI_JSON_TYPE_RAW = 7

const REG_EXTENDED = 0x1
const REG_ICASE = 0x2
const REG_NOSUB = 0x4
const REG_NEWLINE = 0x8
const REG_NOMATCH = 1
const REG_BADPAT = 2
const REG_ECOLLATE = 3
const REG_ECTYPE = 4
const REG_EESCAPE = 5
const REG_ESUBREG = 6
const REG_EBRACK = 7
const REG_EPAREN = 8
const REG_EBRACE = 9
const REG_BADBR = 10
const REG_ERANGE = 11
const REG_ESPACE = 12
const REG_BADRPT = 13
const REG_ENOSYS = 14
const REG_NOTBOL = 0x1
const REG_NOTEOL = 0x2

const regexErrorMessages = {
  [REG_NOMATCH]: 'regexec() failed to match',
  [REG_BADPAT]: 'Invalid regular expression',
  [REG_ECOLLATE]: 'Invalid collating element',
  [REG_ECTYPE]: 'Invalid character class',
  [REG_EESCAPE]: 'Trailing backslash () in pattern',
  [REG_ESUBREG]: 'Invalid back reference',
  [REG_EBRACK]: 'Unmatched [ or ]',
  [REG_EPAREN]: 'Unmatched ( or )',
  [REG_EBRACE]: 'Unmatched { or }',
  [REG_BADBR]: 'Invalid contents of { }',
  [REG_ERANGE]: 'Invalid character range',
  [REG_ESPACE]: 'Ran out of memory',
  [REG_BADRPT]: 'Nothing to repeat',
  [REG_ENOSYS]: 'Function not implemented'
}

const errorMessages = {
  0: 'Undefined error: 0',
  [EPERM]: 'Operation not permitted',
  [ENOENT]: 'No such file or directory',
  [ESRCH]: 'No such process',
  [EINTR]: 'Interrupted system call',
  [EIO]: 'Input/output error',
  [ENXIO]: 'Device not configured',
  [E2BIG]: 'Argument list too long',
  [ENOEXEC]: 'Exec format error',
  [EBADF]: 'Bad file descriptor',
  [ECHILD]: 'No child processes',
  [EAGAIN]: 'Try again',
  [ENOMEM]: 'Out of memory',
  [EACCES]: 'Permission denied',
  [EFAULT]: 'Bad address',
  [ENOTBLK]: 'Block device required',
  [EBUSY]: 'Device or resource busy',
  [EEXIST]: 'File exists',
  [EXDEV]: 'Cross-device link',
  [ENODEV]: 'No such device',
  [ENOTDIR]: 'Not a directory',
  [EISDIR]: 'Is a directory',
  [EINVAL]: 'Invalid argument',
  [ENFILE]: 'File table overflow',
  [EMFILE]: 'Too many open files',
  [ENOTTY]: 'Not a typewriter',
  [ETXTBSY]: 'Text file busy',
  [EFBIG]: 'File too large',
  [ENOSPC]: 'No space left on device',
  [ESPIPE]: 'Illegal seek',
  [EROFS]: 'Read-only file system ',
  [EMLINK]: 'Too many links',
  [EPIPE]: 'Broken pipe',
  [EDOM]: ' Numerical argument out of domain',
  [ERANGE]: 'umerical result out of range',
  [EDEADLK]: 'Resource deadlock avoided',
  [ENAMETOOLONG]: 'File name too long',
  [ENOLCK]: 'No locks available',
  [ENOSYS]: 'Function not implemented',
  [ENOTEMPTY]: 'Directory not empty',
  [ELOOP]: 'Too many levels of symbolic links',
  [EWOULDBLOCK]: 'Try again',
  [ENOMSG]: 'No message of desired type',
  [EIDRM]: 'Identifier removed',
  [ECHRNG]: 'Character range error',
  [EL2NSYNC]: 'Level 2 not synchronized',
  [EL3HLT]: 'Level 3 halted',
  [EL3RST]: 'EL3RST',
  [ELNRNG]: 'ELNRNG',
  [EUNATCH]: 'No such device',
  [ENOCSI]: 'No CSI structure available',
  [EL2HLT]: 'Level 2 halted',
  [EBADE]: 'Invalid exchange',
  [EBADR]: 'Invalid request descriptor',
  [EXFULL]: 'Exchange full',
  [ENOANO]: 'No anode',
  [EBADRQC]: 'Invalid request code',
  [EBADSLT]: 'Invalid slot',
  [EDEADLOCK]: 'Resource deadlock avoided',
  [EBFONT]: 'Bad font file format',
  [ENOSTR]: 'Device not a stream',
  [ENODATA]: 'No data available',
  [ETIME]: 'Timer expired',
  [ENOSR]: 'Out of streams resources',
  [ENONET]: 'Machine is not on the network',
  [ENOPKG]: 'Package not installed',
  [EREMOTE]: 'Object is remote',
  [ENOLINK]: 'Link has been severed',
  [EADV]: 'Advertise error',
  [ESRMNT]: 'Srmount error',
  [ECOMM]: 'Communication error on send',
  [EPROTO]: 'Protocol error',
  [EMULTIHOP]: 'Multihop attempted',
  [EDOTDOT]: 'RFS specific error',
  [EBADMSG]: 'Not a data message',
  [EOVERFLOW]: 'Value too large for defined data type',
  [ENOTUNIQ]: 'Name not unique on network',
  [EBADFD]: 'File descriptor in bad state',
  [EREMCHG]: 'Remote address changed',
  [ELIBACC]: 'Can not access a needed shared library',
  [ELIBBAD]: 'Accessing a corrupted shared library',
  [ELIBSCN]: '.lib section in a.out corrupted',
  [ELIBMAX]: 'Attempting to link in too many shared libraries',
  [ELIBEXEC]: 'Cannot exec a shared library directly',
  [EILSEQ]: 'Illegal byte sequence',
  [ERESTART]: 'Interrupted system call should be restarted',
  [ESTRPIPE]: 'Streams pipe error',
  [EUSERS]: 'Too many users',
  [ENOTSOCK]: 'Socket operation on non-socket',
  [EDESTADDRREQ]: 'Destination address required',
  [EMSGSIZE]: 'Message too long',
  [EPROTOTYPE]: 'Protocol wrong type for socket',
  [ENOPROTOOPT]: 'Protocol not available',
  [EPROTONOSUPPORT]: 'Protocol not supported',
  [ESOCKTNOSUPPORT]: 'Socket type not supported',
  [EOPNOTSUPP]: 'Operation not supported',
  [ENOTSUP]: 'Operation not supported',
  [EPFNOSUPPORT]: 'Protocol family not supported',
  [EAFNOSUPPORT]: 'Address family not supported by protocol',
  [EADDRINUSE]: 'Address already in use',
  [EADDRNOTAVAIL]: 'Cannot assign requested address',
  [ENETDOWN]: 'Network is down',
  [ENETUNREACH]: 'Network is unreachable',
  [ENETRESET]: 'Network dropped connection because of reset',
  [ECONNABORTED]: 'Software caused connection abort',
  [ECONNRESET]: 'Connection reset by peer',
  [ENOBUFS]: 'No buffer space available',
  [EISCONN]: 'Transport endpoint is already connected',
  [ENOTCONN]: 'Transport endpoint is not connected',
  [ESHUTDOWN]: 'Cannot send after transport endpoint shutdown',
  [ETOOMANYREFS]: 'Too many references: cannot splice',
  [ETIMEDOUT]: 'Connection timed out',
  [ECONNREFUSED]: 'Connection refused',
  [EHOSTDOWN]: 'Host is down',
  [EHOSTUNREACH]: 'No route to host',
  [EALREADY]: 'Operation already in progress',
  [EINPROGRESS]: 'Operation now in progress',
  [ESTALE]: 'Stale NFS file handle',
  [EUCLEAN]: 'Structure needs cleaning',
  [ENOTNAM]: 'Not a XENIX named type file',
  [ENAVAIL]: 'No XENIX semaphores available',
  [EISNAM]: 'Is a named type file',
  [EREMOTEIO]: 'Remote I/O error',
  [EDQUOT]: 'Quota exceeded',
  [ENOMEDIUM]: 'No medium found',
  [EMEDIUMTYPE]: 'Wrong medium type',
  [ECANCELED]: 'Operation canceled',
  [ENOKEY]: 'Required key not available',
  [EKEYEXPIRED]: 'Key has expired',
  [EKEYREVOKED]: 'Key has been revoked',
  [EKEYREJECTED]: 'Key was rejected by service',
  [EOWNERDEAD]: 'Owner died',
  [ENOTRECOVERABLE]: 'State not recoverable',
  [ERFKILL]: 'Operation not possible due to RF-kill',
  [EHWPOISON]: 'Memory page has hardware error'
}

class WebAssemblyExtensionRuntimeObject {
  static get pointerSize () {
    return 4
  }

  adapter = null

  constructor (adapter) {
    this.adapter = adapter
  }
}

class WebAssemblyExtensionRuntimeBuffer extends WebAssemblyExtensionRuntimeObject {
  context = null
  pointer = NULL
  #buffer = new Uint8Array(0)
  #bufferPointer = NULL

  constructor (adapter, context) {
    super(adapter)
    this.context = context
    this.pointer = context.createExternalReferenceValue(this)
  }

  get buffer () {
    return this.#buffer
  }

  get bufferPointer () {
    return this.#bufferPointer
  }

  set (pointer, size) {
    this.#buffer = new Uint8Array(size)
    this.#buffer.fill(0)
    this.#buffer.set(this.adapter.get(pointer, size))
    this.#bufferPointer = pointer
  }
}

class WebAssemblyExtensionRuntimeJSON extends WebAssemblyExtensionRuntimeObject {
  context = null
  pointer = NULL
  #value = null

  constructor (adapter, context, value) {
    super(adapter)
    this.context = context
    this.pointer = context.createExternalReferenceValue(this)
    this.value = value
  }

  set value (value) {
    this.#value = value
  }

  get value () {
    return this.#value
  }
}

class WebAssemblyExtensionRuntimeIPCResultJSON extends WebAssemblyExtensionRuntimeJSON {
  #data = null
  #err = null

  constructor (adapter, context, value) {
    super(adapter, context, value)
    this.#data = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.value?.data ?? null
    )
    this.#err = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.value?.err ?? null
    )
  }

  set value (value) {
    if (value?.data) {
      this.#data.value = value.data
    }

    if (value?.err) {
      this.#err.value = value.err
    }

    super.value = value
  }

  get value () {
    return super.value
  }

  get data () {
    return this.#data
  }

  get err () {
    return this.#err
  }
}

class WebAssemblyExtensionRuntimeIPCResultHeaders extends WebAssemblyExtensionRuntimeObject {
  #headers = new ipc.Headers()
  #pointerMap = new Map()
  context = null
  pointer = NULL
  constructor (adapter, context) {
    super(adapter)
    this.context = context
    this.pointer = context.createExternalReferenceValue(this)
  }

  get value () {
    return this.#headers
  }

  get pointerMap () {
    return this.#pointerMap
  }

  set (name, value) {
    this.#headers.set(name, value)
    this.#pointerMap.set(name, this.context.createExternalReferenceValue(value))
  }

  get (name) {
    return this.#headers.get(name)
  }

  getValuePointer (name) {
    return this.#pointerMap.get(name)
  }
}

class WebAssemblyExtensionRuntimeIPCMessage extends WebAssemblyExtensionRuntimeObject {
  context = null
  pointer = NULL

  #id = null
  #name = null
  #seq = null
  #uri = null
  #value = null
  #values = {}
  #bytes = null
  #message = null

  constructor (adapter, context, url, ...args) {
    super(adapter)
    this.context = context
    this.pointer = context.createExternalReferenceValue(this)
    this.#message = ipc.Message.from(url, ...args)

    this.#id = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.#message.id
    )
    this.#name = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.#message.name
    )
    this.#uri = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.#message.toString()
    )
    this.#seq = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.#message.seq
    )
    this.#value = new WebAssemblyExtensionRuntimeJSON(
      adapter,
      context,
      this.#message.value
    )
    this.#bytes = new WebAssemblyExtensionRuntimeBuffer(
      adapter,
      context,
      this.#message.bytes
    )

    for (const key of this.#message.searchParams.keys()) {
      const value = this.#message.get(key)
      this.#values[key] = new WebAssemblyExtensionRuntimeJSON(
        adapter,
        context,
        value
      )
    }
  }

  get id () {
    return this.#id
  }

  get name () {
    return this.#name
  }

  get uri () {
    return this.#uri
  }

  get seq () {
    return this.#seq
  }

  get index () {
    return this.#message.index
  }

  get value () {
    return this.#value
  }

  get bytes () {
    return this.#bytes
  }

  get (key) {
    return this.#values[key]
  }
}

class WebAssemblyExtensionRuntimeIPCResult extends WebAssemblyExtensionRuntimeObject {
  #message = null
  #bytes = null
  #headers = null
  #json = null

  context = null
  pointer = NULL
  result = null
  source = null
  seq = null
  id = null

  constructor (adapter, context, message, existingResult = null) {
    super(adapter)
    this.#json = new WebAssemblyExtensionRuntimeIPCResultJSON(
      adapter,
      context,
      {}
    )
    this.#bytes = new WebAssemblyExtensionRuntimeBuffer(adapter, context)
    this.#headers = new WebAssemblyExtensionRuntimeIPCResultHeaders(
      adapter,
      context
    )

    this.pointer = context.createExternalReferenceValue(this)
    this.context = context
    this.message = message
    this.source = message.name

    if (this.result && existingResult) {
      this.id = existingResult.id
      this.seq = existingResult.seq
      this.err = existingResult.err
      this.data = existingResult.data
      this.source = existingResult.source

      for (const entry of existingResult.pointerMap.entries()) {
        this.#headers.pointerMap.set(entry[0], entry[1])
      }

      for (const entry of existingResult.headers.entries()) {
        this.#headers.value.set(entry[0], entry[1])
      }
    }
  }

  set message (message) {
    this.#message = message
    this.seq = message.seq
    this.source = message.name
    this.result = ipc.Result.from(
      { id: message.id },
      null,
      message.name,
      message.headers
    )
  }

  get message () {
    return this.#message
  }

  get headers () {
    return this.#headers
  }

  get bytes () {
    return this.#bytes
  }

  set data (data) {
    this.#json.data.value = data ?? null
  }

  get data () {
    return this.#json.data
  }

  // eslint-disable-next-line
  set err(err) {
    this.#json.err.value = err ?? null
  }

  get err () {
    return this.#json.err
  }

  set json (json) {
    this.#json.value = json

    this.source = json?.source ?? this.source
    this.data = json?.data ?? this.data
    this.err = json?.err ?? this.err
    this.id = json?.id ?? this.id
  }

  get json () {
    return this.#json
  }

  toJSON () {
    return this.#json.value
  }

  reply () {
    if (!this.context) {
      this.result = null
      return false
    }

    const callback = this.context.internal?.callback
    this.result.headers = this.headers.value
    this.result.source = this.source.value
    this.result.err = this.err.value ?? null
    this.result.id = this.id || ''

    if (this.bytes.buffer.byteLength > 0) {
      this.result.data = this.bytes.buffer
    } else {
      this.result.data = this.data.value ?? this.json.value
    }

    if (typeof callback === 'function') {
      callback(this.result)
    }

    this.context.release()

    this.result = null
    this.context = null
    return typeof callback === 'function'
  }
}

class WebAssemblyExtensionRuntimeIPCRouter extends WebAssemblyExtensionRuntimeObject {
  routes = new Map()
  pointer = NULL

  constructor (adapter) {
    super(adapter)
    this.pointer = adapter.createExternalReferenceValue(this)
  }

  map (route, callback) {
    this.routes.set(route, callback)
  }

  unmap (route) {
    this.routes.delete(route)
  }

  invoke (context, url, size, bytes, callback) {
    if (typeof size === 'function') {
      callback = size
      bytes = null
      size = 0
    }

    if (!url.startsWith('ipc://')) {
      url = `ipc://${url}`
    }

    const { adapter } = this
    const { name } = ipc.Message.from(url)

    if (this.routes.has(name)) {
      const routeContext = new WebAssemblyExtensionRuntimeContext(adapter, {
        context
      })

      const message = new WebAssemblyExtensionRuntimeIPCMessage(
        adapter,
        routeContext,
        url,
        null,
        bytes
      )

      routeContext.internal.callback = callback
      const route = this.routes.get(name)
      route(routeContext, message, this)
      return true
    }

    return false
  }
}

class WebAssemblyExtensionRuntimePolicy extends WebAssemblyExtensionRuntimeObject {
  name = null
  allowed = false
  constructor (adapter, name, allowed) {
    super(adapter)
    this.name = name
    this.allowed = Boolean(allowed)
  }
}

class WebAssemblyExtensionRuntimeContext extends WebAssemblyExtensionRuntimeObject {
  // internal pointers
  data = NULL
  pointer = NULL

  pool = []
  error = null
  router = null
  internal = { callback: null }
  config = Object.create(application.config)
  context = null
  retained = false
  policies = new Map()
  retainedCount = 0

  constructor (adapter, options) {
    super(adapter)

    this.context = options?.context ?? null
    const policies = options?.policies ?? options?.context?.policies ?? null

    if (Array.isArray(policies)) {
      for (const policy of policies) {
        this.setPolicy(policy, true)
      }
    }

    this.router =
      this.context?.router ?? new WebAssemblyExtensionRuntimeIPCRouter(adapter)
    this.config = this.context?.config
      ? Object.create(this.context.config)
      : this.config
    this.retained = Boolean(options?.retained)
    this.policies = this.context?.policies ?? this.policies
    this.pointer = this.context
      ? this.context.alloc(WebAssemblyExtensionRuntimeObject.pointerSize)
      : this.adapter.heap.alloc(WebAssemblyExtensionRuntimeObject.pointerSize)

    this.adapter.setExternalReferenceValue(this.pointer, this)
  }

  alloc (size) {
    const pointer = this.adapter.heap.alloc(size)

    if (pointer) {
      this.pool.push(pointer)
    }

    return pointer
  }

  free (pointer) {
    const index = this.pool.indexOf(pointer)
    if (index > -1) {
      this.pool.splice(index, 1)
      this.adapter.heap.free(pointer)
    }
  }

  createExternalReferenceValue (value) {
    const pointer = this.adapter.createExternalReferenceValue(value)
    if (pointer) {
      this.pool.push(pointer)
    }
    return pointer
  }

  retain () {
    this.retained = true
    this.retainedCount++
  }

  release () {
    if (this.retainedCount === 0) {
      return false
    }

    if (--this.retainedCount === 0) {
      for (const pointer of this.pool) {
        const object = this.adapter.getExternalReferenceValue(pointer)

        if (typeof object?.release === 'function') {
          object.release()
        }

        this.adapter.heap.free(pointer)
      }

      this.adapter.heap.free(this.pointer)
      this.pool.splice(0, this.pool.length)
      this.pointer = NULL
      this.retained = false
      this.context = null
      return true
    }

    return false
  }

  isAllowed (name) {
    const names = name.split(',')
    for (const name of names) {
      const parts = name.trim().split('_')
      let key = ''
      for (const part of parts) {
        key += part
        if (this.hasPolicy(key) && this.getPolicy(key).allowed) {
          return true
        }

        key += '_'
      }
    }

    return false
  }

  setPolicy (name, allowed) {
    if (this.hasPolicy(name)) {
      const policy = this.getPolicy(name)
      policy.name = name
      policy.allowed = Boolean(allowed)
    } else {
      this.policies.set(
        name,
        new WebAssemblyExtensionRuntimePolicy(this.adapter, name, allowed)
      )
    }
  }

  getPolicy (name) {
    return this.policies.get(name) ?? null
  }

  hasPolicy (name) {
    return this.policies.has(name)
  }
}

/**
 * A constructed `Response` suitable for WASM binaries.
 * @ignore
 */
class WebAssemblyResponse extends Response {
  constructor (stream) {
    super(stream, {
      headers: {
        'content-type': 'application/wasm'
      }
    })
  }
}

class WebAssemblyExtensionExternalReference {
  adapter = null
  pointer = NULL
  value = null

  constructor (adapter, value, pointer) {
    // box primitive values for proper equality comparisons
    if (typeof value === 'string') {
      // eslint-disable-next-line
      value = new String(value)
    } else if (typeof value === 'number') {
      // eslint-disable-next-line
      value = new Number(value)
    } else if (typeof value === 'boolean') {
      // eslint-disable-next-line
      value = new Boolean(value)
    }

    this.adapter = adapter
    this.pointer = pointer || adapter.heap.alloc(4)
    this.value = value
  }

  valueOf () {
    return this.value?.valueOf?.() ?? undefined
  }

  toString () {
    return this.value?.toString?.() ?? ''
  }
}

class WebAssemblyExtensionMemoryBlock {
  memory = null
  start = 0
  end = 0

  constructor (memory, start = null, end = null) {
    this.memory = memory
    this.start = start || 0
    this.end = end || memory.byteLength
  }
}

class WebAssemblyExtensionMemory {
  adapter = null
  offset = 0
  current = []
  limits = { min: 0, max: 0 }
  freeList = []

  constructor (adapter, min, max) {
    this.adapter = adapter
    this.limits.min = min
    this.limits.max = max
    this.offset = min

    this.freeList.push(new WebAssemblyExtensionMemoryBlock(this))
  }

  get buffer () {
    return this.adapter.buffer
  }

  get byteLength () {
    return this.buffer.byteLength
  }

  get pointer () {
    return this.offset
  }

  get (pointer, size = -1) {
    if (size > -1) {
      return this.buffer.subarray(pointer, pointer + size)
    } else {
      return this.buffer.subarray(pointer)
    }
  }

  set (pointer, byteOrBytes, size) {
    if (!pointer || !size) {
      return false
    }

    if (typeof byteOrBytes === 'number') {
      const byte = byteOrBytes
      byteOrBytes = new Uint8Array(size)
      byteOrBytes.fill(byte)
    }

    this.buffer.set(byteOrBytes.slice(0, size), pointer)
    return true
  }

  push (value) {
    let pointer = this.offset

    if (value === null) {
      value = 0
    }

    if (typeof value === 'string') {
      value = this.adapter.textEncoder.encode(value)
    } else if (isBufferLike(value)) {
      value = new Uint8Array(value)
    }

    if (!isBufferLike(value) && typeof value !== 'number') {
      throw new TypeError(
        'WebAssemblyExtensionMemory: ' +
          'Expected value to be TypedArray or a number: ' +
          `Got ${typeof value}`
      )
    }

    let byteLength = 0
    const isFloat =
      typeof value === 'number' && parseInt(String(value)) !== value

    if (isBufferLike(value)) {
      byteLength = value.byteLength
      pointer = this.alloc(byteLength)
    } else {
      const zeroes = Math.clz32(value)
      if (zeroes <= 8) {
        byteLength = 4
      } else if (zeroes <= 16) {
        byteLength = 2
      } else if (zeroes <= 32) {
        byteLength = 1
      }
    }

    this.current.push(byteLength)
    this.offset += byteLength

    if (isBufferLike(value)) {
      this.adapter.set(pointer, value)
    } else if (byteLength === 4) {
      if (isFloat) {
        this.adapter.setFloat32(pointer, value)
      } else {
        this.adapter.setInt32(pointer, value)
      }
    } else if (byteLength === 2) {
      if (isFloat) {
        this.adapter.setFloat16(pointer, value)
      } else {
        this.adapter.setInt16(pointer, value)
      }
    } else if (byteLength === 1) {
      if (isFloat) {
        this.adapter.setFloat8(pointer, value)
      } else {
        this.adapter.setInt8(pointer, value)
      }
    }

    return pointer
  }

  pop () {
    if (this.current.length === 0) {
      return 0
    }

    const byteLength = this.current.pop()
    const pointer = this.offset - byteLength
    this.offset -= byteLength
    return pointer
  }

  restore (offset) {
    const pointers = []
    while (this.offset > offset) {
      pointers.push(this.pop())
    }
    return pointers
  }

  alloc (size) {
    if (!size) {
      return NULL
    }

    // find a free block that is large enough
    for (let i = 0; i < this.freeList.length; ++i) {
      const block = this.freeList[i]
      if (block.end - block.start >= size) {
        // allocate memory from the free block
        const allocatedBlock = new WebAssemblyExtensionMemoryBlock(
          this,
          block.start,
          block.start + size
        )

        // update the free block (if any remaining)
        if (block.end - allocatedBlock.end > 0) {
          block.start = allocatedBlock.end
        } else {
          // remove the block from the free list if no remaining space
          this.freeList.splice(i, 1)
        }

        this.offset = allocatedBlock.end
        const pointer = this.limits.min + allocatedBlock.start
        return pointer
      }
    }

    return NULL
  }

  free (pointer) {
    if (!pointer) {
      return
    }

    if (this.adapter.externalReferences.has(pointer)) {
      this.adapter.externalReferences.delete(pointer)
    }

    pointer = pointer - this.limits.min

    // find the block to free and merge adjacent free blocks
    for (let i = 0; i < this.freeList.length; ++i) {
      const block = this.freeList[i]
      if (pointer >= block.start && pointer < block.end) {
        // add the freed block back to the free list
        this.freeList.splice(
          i,
          0,
          new WebAssemblyExtensionMemoryBlock(
            this,
            pointer,
            pointer + (block.end - block.start)
          )
        )

        // merge adjacent free blocks
        if (
          i < this.freeList.length - 1 &&
          this.freeList[i + 1].start === block.end
        ) {
          this.freeList[i].end = this.freeList[i + 1].end
          this.freeList.splice(i + 1, 1)
        }

        if (i > 0 && this.freeList[i - 1].end === block.start) {
          this.freeList[i - 1].end = block.end
          this.freeList.splice(i, 1)
        }

        this.offset = this.freeList[this.freeList.length - 1].start
        return
      }
    }
  }
}

class WebAssemblyExtensionStack extends WebAssemblyExtensionMemory {
  constructor (adapter) {
    super(
      adapter,
      adapter.instance.exports.__stack_low.value,
      adapter.instance.exports.__stack_high.value
    )
  }
}

class WebAssemblyExtensionHeap extends WebAssemblyExtensionMemory {
  constructor (adapter) {
    super(
      adapter,
      adapter.instance.exports.__heap_base.value,
      adapter.instance.exports.__heap_end.value
    )
  }

  realloc (pointer, size) {
    // if the pointer is NULL, it's equivalent to `alloc()`
    if (pointer === NULL) {
      return this.alloc(size)
    }

    // find the block corresponding to the given pointer
    for (let i = 0; i < this.freeList.length; ++i) {
      const block = this.freeList[i]
      if (pointer >= block.start && pointer < block.end) {
        const currentSize = block.end - block.start

        // if the new size is smaller or equal, return the existing pointer
        if (size <= currentSize) {
          return pointer
        }

        // try to extend the current block if there is enough space
        if (
          i < this.freeList.length - 1 &&
          this.freeList[i + 1].start - block.end >= size - currentSize
        ) {
          block.end += size - currentSize
          return pointer
        }

        // if extension is not possible, allocate a new block and copy the data
        const newPointer = this.alloc(size)
        const sourceArray = new Uint8Array(
          this.buffer.buffer,
          block.start,
          currentSize
        )
        const destinationArray = new Uint8Array(
          this.buffer.buffer,
          newPointer,
          currentSize
        )
        destinationArray.set(sourceArray)

        // free the original block
        this.free(pointer)

        return newPointer
      }
    }

    // return NULL if the pointer is not found (not allocated)
    return NULL
  }

  calloc (count, size) {
    const totalSize = count * size
    const pointer = this.alloc(totalSize)

    if (pointer === NULL) {
      // allocation failed
      return NULL
    }

    // initialize the allocated memory to zero
    const array = new Uint8Array(this.buffer.buffer, pointer, totalSize)
    array.fill(0)
    return pointer
  }
}

class WebAssemblyExtensionIndirectFunctionTable {
  constructor (adapter, options = null) {
    this.adapter = adapter
    this.table =
      options?.table ??
      adapter.instance?.exports?.__indirect_function_table ??
      new WebAssembly.Table({ initial: 0, element: 'anyfunc' })
  }

  call (index, ...args) {
    if (this.table.length === 0 || index >= this.table.length) {
      return null
    }

    const callable = this.table.get(index)
    const values = []
    const mark = this.adapter.stack.pointer

    for (const arg of args) {
      if (typeof arg === 'number') {
        values.push(arg)
      } else {
        values.push(this.adapter.stack.push(arg))
      }
    }

    const value = callable(...values)
    this.adapter.stack.restore(mark)

    return value
  }
}

/**
 * An adapter for reading and writing various values from a WebAssembly instance's
 * memory buffer.
 * @ignore
 */
class WebAssemblyExtensionAdapter {
  view = null
  heap = null
  table = null
  stack = null
  buffer = null
  module = null
  memory = null
  context = null
  policies = []
  externalReferences = new Map() // mapping pointer addresses to javascript objects
  instance = null
  exitStatus = null
  textDecoder = new TextDecoder()
  textEncoder = new TextEncoder()
  errorMessagePointers = {}
  indirectFunctionTable = null

  constructor ({ instance, module, table, memory, policies }) {
    this.view = new DataView(memory.buffer)
    this.table = table
    this.buffer = new Uint8Array(memory.buffer)
    this.module = module
    this.memory = memory
    this.policies = policies || []
    this.instance = instance
    this.indirectFunctionTable = new WebAssemblyExtensionIndirectFunctionTable(
      this
    )
  }

  get globalBaseOffset () {
    return this.instance.exports.__global_base.value
  }

  destroy () {
    this.view = null
    this.table = null
    this.buffer = null
    this.module = null
    this.memory = null
    this.instance = null
    this.textDecoder = null
    this.textEncoder = null
    this.indirectFunctionTable = null
    this.stack = null
    this.heap = null
    this.context = null
  }

  init () {
    this.stack = new WebAssemblyExtensionStack(this)
    this.heap = new WebAssemblyExtensionHeap(this)

    this.instance.exports.__wasm_call_ctors()
    const initializer = this.getExtensionExport(
      '__oapi_extension_initializer',
      '__sapi_extension_initializer'
    )

    if (typeof initializer !== 'function') {
      throw new Error('Extension is missing an initializer export')
    }

    const offset = initializer()
    this.context = new WebAssemblyExtensionRuntimeContext(this, {
      policies: this.policies || []
    })

    // preallocate error message pointertr
    for (const errorCode in errorMessages) {
      const errorMessage = errorMessages[errorCode]
      const pointer = this.heap.alloc(errorMessage.length)
      this.errorMessagePointers[errorCode] = pointer
      this.setString(pointer, errorMessage)
    }

    return Boolean(
      this.indirectFunctionTable.call(offset, this.context.pointer)
    )
  }

  getExtensionExport (...names) {
    if (!this.instance || !this.instance.exports) return null
    for (const name of names) {
      const value = this.instance.exports[name]
      if (typeof value !== 'undefined') {
        return value
      }
    }
    return null
  }

  get (pointer, size = -1) {
    if (!pointer) {
      return null
    }

    if (size > -1) {
      return this.buffer.subarray(pointer, pointer + size)
    }

    return this.buffer.subarray(pointer)
  }

  set (pointer, value) {
    if (pointer) {
      if (typeof value === 'string') {
        value = this.textEncoder.encode(value)
      }

      this.buffer.set(value, pointer)
    }
  }

  createExternalReferenceValue (value) {
    const pointer = this.heap.alloc(4)
    this.setExternalReferenceValue(pointer, value)
    return pointer
  }

  getExternalReferenceValue (pointer) {
    const ref = this.externalReferences.get(pointer)
    if (ref) {
      if (ref.value === null) {
        return NULL
      }

      if (typeof ref.value === 'object') {
        return ref.value.valueOf()
      }

      return ref.value
    }
  }

  setExternalReferenceValue (pointer, value) {
    return this.externalReferences.set(
      pointer,
      new WebAssemblyExtensionExternalReference(this, value, pointer)
    )
  }

  removeExternalReferenceValue (pointer) {
    this.externalReferences.delete(pointer)
  }

  getExternalReferencePointer (value) {
    for (const ref of this.externalReferences.values()) {
      if (ref.value === value) {
        return ref.pointer
      }
    }

    return NULL
  }

  getFloat32 (pointer) {
    return pointer ? this.view.getFloat32(pointer, true) : 0
  }

  setFloat32 (pointer, value) {
    return pointer ? (this.view.setFloat32(pointer, value, true), true) : false
  }

  getFloat64 (pointer) {
    return pointer ? this.view.getFloat64(pointer, true) : 0
  }

  setFloat64 (pointer, value) {
    return pointer ? (this.view.setFloat64(pointer, value, true), true) : false
  }

  getInt8 (pointer) {
    return pointer ? this.view.getInt8(pointer, true) : 0
  }

  setInt8 (pointer, value) {
    return pointer ? (this.view.setInt8(pointer, value, true), true) : false
  }

  getInt16 (pointer) {
    return pointer ? this.view.getInt16(pointer, true) : 0
  }

  setInt16 (pointer, value) {
    return pointer ? (this.view.setInt16(pointer, value, true), true) : false
  }

  getInt32 (pointer) {
    return pointer ? this.view.getInt32(pointer, true) : 0
  }

  setInt32 (pointer, value) {
    return pointer ? (this.view.setInt32(pointer, value, true), true) : false
  }

  getUint8 (pointer) {
    return pointer ? this.view.getUint8(pointer, true) : 0
  }

  setUint8 (pointer, value) {
    return pointer ? (this.view.setUint8(pointer, value, true), true) : false
  }

  getUint16 (pointer) {
    return pointer ? this.view.getUint16(pointer, true) : 0
  }

  setUint16 (pointer, value) {
    return pointer ? (this.view.setUint16(pointer, value, true), true) : false
  }

  getUint32 (pointer) {
    return pointer ? this.view.getUint32(pointer, true) : 0
  }

  setUint32 (pointer, value) {
    return pointer ? (this.view.setUint32(pointer, value, true), true) : false
  }

  getString (pointer, buffer, size) {
    if (!pointer) {
      return null
    }

    if (typeof buffer === 'number') {
      size = buffer
      buffer = null
    }

    const start = buffer ? buffer.slice(pointer) : this.buffer.subarray(pointer)

    const end = size || start.indexOf(0) // NULL byte

    if (end === -1) {
      return this.textDecoder.decode(start)
    }

    return this.textDecoder.decode(start.slice(0, end))
  }

  setString (pointer, string, buffer = null) {
    if (pointer) {
      const encoded = this.textEncoder.encode(string)
      ;(buffer || this.buffer).set(encoded, pointer)
      return true
    }

    return false
  }
}

/**
 * A container for a native WebAssembly extension info.
 * @ignore
 */
class WebAssemblyExtensionInfo {
  adapter = null

  constructor (adapter) {
    this.adapter = adapter
  }

  get instance () {
    return this.adapter.instance
  }

  get abi () {
    const fn = this.adapter.getExtensionExport(
      '__oapi_extension_abi',
      '__sapi_extension_abi'
    )
    return typeof fn === 'function' ? fn() : 0
  }

  get name () {
    const fn = this.adapter.getExtensionExport(
      '__oapi_extension_name',
      '__sapi_extension_name'
    )
    return this.adapter.getString(fn && fn())
  }

  get version () {
    const fn = this.adapter.getExtensionExport(
      '__oapi_extension_version',
      '__sapi_extension_version'
    )
    return this.adapter.getString(fn && fn())
  }

  get description () {
    const fn = this.adapter.getExtensionExport(
      '__oapi_extension_description',
      '__sapi_extension_description'
    )
    return this.adapter.getString(fn && fn())
  }
}

function createWebAssemblyExtensionBinding (extension, adapter) {
  return new Proxy(adapter, {
    get (_, key) {
      if (typeof adapter.instance.exports[key] === 'function') {
        return adapter.instance.exports[key].bind(adapter.instance.exports)
      }

      if (!adapter.context.router.routes.has(key)) {
        key = `${extension.name}.${String(key)}`
      }

      if (adapter.context.router.routes.has(key)) {
        return (options, ...args) => {
          return new Promise((resolve, reject) => {
            let params = null
            if (typeof options === 'object') {
              params = String(new URLSearchParams(options))
            } else if (typeof options === 'string') {
              params = options
            }

            const rest = Array.from(args).concat([resolve])
            const url = `ipc://${key}?${params}`
            try {
              adapter.context.router.invoke(adapter.context, url, ...rest)
            } catch (err) {
              reject(err)
            }
          })
        }
      }

      return null
    }
  })
}

function variadicFormattedestinationPointerringFromPointers (
  env,
  formatPointer,
  variadicArguments = 0x0,
  encoded = true
) {
  const format = env.adapter.getString(formatPointer)
  if (!variadicArguments) {
    if (encoded) {
      return env.adapter.textEncoder.encode(format)
    }

    return format
  }

  const view = new DataView(env.adapter.buffer.buffer, variadicArguments)

  let index = 0

  const regex = /%(|l|ll|j|t|z|\.\*)+(l|d|f|i|n|o|p|s|S|u|x|X|%)+/gi
  const output = format.replace(regex, (x) => {
    if (x === '%%') {
      return '%'
    }

    switch (x.toLowerCase()) {
      case '%.*lu':
      case '%.*llu':
      case '%.*ls':
      case '%.*lls':
      case '%.*u':
      case '%.*s': {
        const size = view.getInt32(index++ * 4, true)
        const pointer = view.getInt32(index++ * 4, true)
        const string =
          env.adapter.getString(pointer) ||
          env.adapter.getExternalReferenceValue(pointer)?.value

        if (!string || typeof string !== 'string') {
          return '(null)'
        }

        return string.slice(0, size)
      }

      case '%s': {
        const pointer = view.getInt32(index++ * 4, true)
        const string =
          env.adapter.getString(pointer) ||
          env.adapter.getExternalReferenceValue(pointer)?.value

        if (!string || typeof string !== 'string') {
          return '(null)'
        }

        return string
      }

      case '%zu':
      case '%u':
      case '%llu':
      case '%lu': {
        return view.getUint32(index * 4, true)
      }

      case '%lld':
      case '%ld':
      case '%i':
      case '%x':
      case '%d': {
        return view.getInt32(index++ * 4, true)
      }

      case '%lp':
      case '%p': {
        return `0x${view.getUint32(index++ * 4, true).toString(16)}`
      }

      case '%f':
      case '%lf':
      case '%llf': {
        return view.getFloat64(index++ * 8, true)
      }
    }

    return x
  })

  if (encoded) {
    return env.adapter.textEncoder.encode(output)
  }

  return output
}

function createWebAssemblyExtensionImports (env) {
  const imports = {
    env: {
      memoryBase: 5 * 1024 * 1024,
      tableBase: 0,
      memory: env.memory,
      table:
        env.table ||
        new WebAssembly.Table({
          initial: 0,
          element: 'anyfunc'
        })
    }
  }

  let atExitCallbackPointer = 0x0
  let srandSeed = 1

  // internal pointers
  let basenamePointer = 0
  let dirnamePointer = 0
  let errnoPointer = 0
  let strtokStatePointer = 0

  const toBeFlushed = {
    stdout: [],
    stderr: []
  }

  const WHITESPACE_CODES = new Set([9, 10, 11, 12, 13, 32])
  const FLOAT_REGEX =
    /^[+-]?(?:nan(?:\([^)]*\))?|inf(?:inity)?|(?:(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)|(?:0[xX](?:[0-9A-Fa-f]+(?:\.[0-9A-Fa-f]*)?|\.[0-9A-Fa-f]+)(?:[pP][+-]?\d+)?))/i
  const HEX_FLOAT_REGEX =
    /^[+-]?0[xX](?:[0-9A-Fa-f]+(?:\.[0-9A-Fa-f]*)?|\.[0-9A-Fa-f]+)(?:[pP][+-]?\d+)?$/
  const INFINITY_LITERALS = new Set([
    'inf',
    '+inf',
    '-inf',
    'infinity',
    '+infinity',
    '-infinity'
  ])

  const isWhitespace = (code) => WHITESPACE_CODES.has(code)

  const digitValue = (code) => {
    if (code >= 0x30 && code <= 0x39) {
      return code - 0x30
    }
    if (code >= 0x41 && code <= 0x5a) {
      return code - 0x41 + 10
    }
    if (code >= 0x61 && code <= 0x7a) {
      return code - 0x61 + 10
    }
    return -1
  }

  const isInfinityLiteral = (token = '') =>
    INFINITY_LITERALS.has(token.toLowerCase())

  const parseHexFloatLiteral = (token) => {
    if (!token) {
      return 0
    }

    let sign = 1
    let literal = token

    if (literal[0] === '+' || literal[0] === '-') {
      if (literal[0] === '-') {
        sign = -1
      }
      literal = literal.slice(1)
    }

    literal = literal.slice(2) // drop leading 0x / 0X

    let exponent = 0
    const pIndex = literal.search(/[pP]/)
    if (pIndex !== -1) {
      exponent = Number.parseInt(literal.slice(pIndex + 1), 10) || 0
      literal = literal.slice(0, pIndex)
    }

    const [wholePart = '', fracPart = ''] = literal.split('.')

    const hexDigit = (character) => digitValue(character.charCodeAt(0))

    let value = 0
    if (wholePart.length > 0) {
      for (const character of wholePart) {
        const digit = hexDigit(character)
        value = value * 16 + (digit >= 0 ? digit : 0)
      }
    }

    if (fracPart.length > 0) {
      let scale = 1 / 16
      for (const character of fracPart) {
        const digit = hexDigit(character)
        if (digit >= 0) {
          value += digit * scale
        }
        scale /= 16
      }
    }

    if (value === 0) {
      return sign * value * Math.pow(2, exponent)
    }

    return sign * value * Math.pow(2, exponent)
  }

  const hasSignificantDigits = (token = '') => {
    const lower = token.toLowerCase()
    if (
      lower.length === 0 ||
      lower.startsWith('nan') ||
      isInfinityLiteral(token)
    ) {
      return false
    }

    let body = lower.replace(/^[+-]/, '')

    if (body.startsWith('0x')) {
      body = body.slice(2)
      const pIndex = body.indexOf('p')
      if (pIndex !== -1) {
        body = body.slice(0, pIndex)
      }
      body = body.replace(/\./g, '')
      return /[1-9a-f]/.test(body)
    }

    const eIndex = body.indexOf('e')
    if (eIndex !== -1) {
      body = body.slice(0, eIndex)
    }
    body = body.replace(/\./g, '')
    return /[1-9]/.test(body)
  }

  const TM_STRUCT_SIZE = 44
  const TM_OFFSETS = {
    sec: 0,
    min: 4,
    hour: 8,
    mday: 12,
    mon: 16,
    year: 20,
    wday: 24,
    yday: 28,
    isdst: 32,
    gmtoff: 36,
    zone: 40
  }

  const WEEKDAY_SHORT = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
  const WEEKDAY_LONG = [
    'Sunday',
    'Monday',
    'Tuesday',
    'Wednesday',
    'Thursday',
    'Friday',
    'Saturday'
  ]
  const MONTH_SHORT = [
    'Jan',
    'Feb',
    'Mar',
    'Apr',
    'May',
    'Jun',
    'Jul',
    'Aug',
    'Sep',
    'Oct',
    'Nov',
    'Dec'
  ]
  const MONTH_LONG = [
    'January',
    'February',
    'March',
    'April',
    'May',
    'June',
    'July',
    'August',
    'September',
    'October',
    'November',
    'December'
  ]

  const staticCStringPointers = new Map()
  let gmtimeStaticPointer = 0
  let localtimeStaticPointer = 0
  let asctimeStaticPointer = 0
  let getdateStaticPointer = 0
  const ASCTIME_BUFFER_CAPACITY = 32

  const writeCString = (pointer, string) => {
    if (!pointer) return false
    env.adapter.setString(pointer, string)
    env.adapter.setUint8(pointer + string.length, 0)
    return true
  }

  const ensureCString = (string) => {
    if (!string || string.length === 0) return 0
    if (staticCStringPointers.has(string)) {
      return staticCStringPointers.get(string)
    }
    const pointer = env.adapter.heap.alloc(string.length + 1)
    writeCString(pointer, string)
    staticCStringPointers.set(string, pointer)
    return pointer
  }

  const readTimeValue = (pointer) => {
    if (!pointer) {
      return Math.floor(Date.now() / 1000)
    }

    const low = env.adapter.getUint32(pointer) >>> 0
    const high = env.adapter.getInt32(pointer + 4)
    return Number((BigInt(high) << 32n) + BigInt(low))
  }

  const writeTimeValue = (pointer, seconds) => {
    if (!pointer) return
    const big = BigInt(Math.trunc(seconds))
    const low = Number(big & 0xffffffffn)
    const high = Number(big >> 32n)
    env.adapter.setUint32(pointer, low >>> 0)
    env.adapter.setInt32(pointer + 4, high)
  }

  const readTimespec = (pointer) => {
    return {
      seconds: env.adapter.getInt32(pointer),
      nanoseconds: env.adapter.getInt32(pointer + 4)
    }
  }

  const writeTimespec = (pointer, seconds, nanoseconds) => {
    env.adapter.setInt32(pointer, seconds)
    env.adapter.setInt32(pointer + 4, nanoseconds)
  }

  const computeYDay = (date, useUTC) => {
    const year = useUTC ? date.getUTCFullYear() : date.getFullYear()
    const month = useUTC ? date.getUTCMonth() : date.getMonth()
    const day = useUTC ? date.getUTCDate() : date.getDate()

    if (useUTC) {
      const base = Date.UTC(year, 0, 1)
      const current = Date.UTC(year, month, day)
      return Math.floor((current - base) / 86400000)
    }

    const baseDate = new Date(year, 0, 1)
    const currentDate = new Date(year, month, day)
    const offsetDiff =
      (currentDate.getTimezoneOffset() - baseDate.getTimezoneOffset()) * 60000
    return Math.floor(
      (currentDate.getTime() - baseDate.getTime() + offsetDiff) / 86400000
    )
  }

  const isDstActive = (date) => {
    const january = new Date(date.getFullYear(), 0, 1)
    const july = new Date(date.getFullYear(), 6, 1)
    const standardOffset = Math.max(
      january.getTimezoneOffset(),
      july.getTimezoneOffset()
    )
    return date.getTimezoneOffset() < standardOffset ? 1 : 0
  }

  const getTimezoneName = (date) => {
    try {
      if (typeof Intl !== 'undefined' && Intl.DateTimeFormat) {
        const parts = new Intl.DateTimeFormat('en-US', {
          timeZoneName: 'short'
        }).formatToParts(date)
        const tzPart = parts.find((part) => part.type === 'timeZoneName')
        if (tzPart?.value) {
          return tzPart.value
        }
      }
    } catch {}

    const match = date.toTimeString().match(/\(([^)]+)\)$/)
    if (match && match[1]) {
      return match[1]
    }

    const offsetMinutes = -date.getTimezoneOffset()
    const sign = offsetMinutes >= 0 ? '+' : '-'
    const absolute = Math.abs(offsetMinutes)
    const hours = Math.floor(absolute / 60)
      .toString()
      .padStart(2, '0')
    const minutes = (absolute % 60).toString().padStart(2, '0')
    return `UTC${sign}${hours}${minutes}`
  }

  const buildTmComponents = (date, useUTC) => {
    const getter = useUTC ? 'getUTC' : 'get'
    const tmYear = date[`${getter}FullYear`]() - 1900
    const tmMon = date[`${getter}Month`]()
    const tmMday = date[`${getter}Date`]()
    const tmHour = date[`${getter}Hours`]()
    const tmMin = date[`${getter}Minutes`]()
    const tmSec = date[`${getter}Seconds`]()
    const tmWday = date[`${getter}Day`]()
    const tmYday = computeYDay(date, useUTC)
    const tmIsdst = useUTC ? 0 : isDstActive(date)
    const gmtoff = useUTC ? 0 : -date.getTimezoneOffset() * 60
    const zoneName = useUTC ? 'UTC' : getTimezoneName(date)

    return {
      tm_sec: tmSec,
      tm_min: tmMin,
      tm_hour: tmHour,
      tm_mday: tmMday,
      tm_mon: tmMon,
      tm_year: tmYear,
      tm_wday: tmWday,
      tm_yday: tmYday,
      tm_isdst: tmIsdst,
      tm_gmtoff: gmtoff,
      tm_zoneName: zoneName
    }
  }

  const writeTmStruct = (tmPointer, tm) => {
    if (!tmPointer) {
      return 0
    }

    env.adapter.setInt32(tmPointer + TM_OFFSETS.sec, tm.tm_sec)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.min, tm.tm_min)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.hour, tm.tm_hour)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.mday, tm.tm_mday)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.mon, tm.tm_mon)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.year, tm.tm_year)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.wday, tm.tm_wday)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.yday, tm.tm_yday)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.isdst, tm.tm_isdst ?? -1)
    env.adapter.setInt32(tmPointer + TM_OFFSETS.gmtoff, tm.tm_gmtoff ?? 0)
    const zonePointer = tm.tm_zoneName ? ensureCString(tm.tm_zoneName) : 0
    env.adapter.setUint32(tmPointer + TM_OFFSETS.zone, zonePointer)
    return tmPointer
  }

  const readTmStruct = (tmPointer) => {
    if (!tmPointer) {
      return null
    }

    const zonePointer = env.adapter.getUint32(tmPointer + TM_OFFSETS.zone)
    return {
      tm_sec: env.adapter.getInt32(tmPointer + TM_OFFSETS.sec),
      tm_min: env.adapter.getInt32(tmPointer + TM_OFFSETS.min),
      tm_hour: env.adapter.getInt32(tmPointer + TM_OFFSETS.hour),
      tm_mday: env.adapter.getInt32(tmPointer + TM_OFFSETS.mday),
      tm_mon: env.adapter.getInt32(tmPointer + TM_OFFSETS.mon),
      tm_year: env.adapter.getInt32(tmPointer + TM_OFFSETS.year),
      tm_wday: env.adapter.getInt32(tmPointer + TM_OFFSETS.wday),
      tm_yday: env.adapter.getInt32(tmPointer + TM_OFFSETS.yday),
      tm_isdst: env.adapter.getInt32(tmPointer + TM_OFFSETS.isdst),
      tm_gmtoff: env.adapter.getInt32(tmPointer + TM_OFFSETS.gmtoff),
      tm_zonePointer: zonePointer,
      tm_zoneName: zonePointer ? env.adapter.getString(zonePointer) : null
    }
  }

  const formatAsctimeString = (tm) => {
    const dayName = WEEKDAY_SHORT[(tm.tm_wday + 7) % 7]
    const monthName = MONTH_SHORT[((tm.tm_mon % 12) + 12) % 12]
    const day = tm.tm_mday
    const hour = tm.tm_hour
    const minute = tm.tm_min
    const second = tm.tm_sec
    const year = tm.tm_year + 1900

    const paddedDay = day < 10 ? ` ${day}` : day.toString().padStart(2, '0')
    const paddedHour = hour.toString().padStart(2, '0')
    const paddedMinute = minute.toString().padStart(2, '0')
    const paddedSecond = second.toString().padStart(2, '0')

    return `${dayName} ${monthName} ${paddedDay} ${paddedHour}:${paddedMinute}:${paddedSecond} ${year}\n`
  }

  const writeAsctimeToPointer = (pointer, tm) => {
    if (!pointer) return 0
    const asctimeString = formatAsctimeString(tm)
    writeCString(pointer, asctimeString)
    return pointer
  }

  const roundTiesToEven = (value) => {
    if (!Number.isFinite(value) || value === 0) {
      return value
    }

    const floorValue = Math.floor(value)
    const diff = value - floorValue

    if (diff < 0.5 - Number.EPSILON) {
      return floorValue
    }

    if (diff > 0.5 + Number.EPSILON) {
      return floorValue + 1
    }

    if (Math.abs(floorValue % 2) === 0) {
      return floorValue
    }

    return floorValue + 1
  }

  const roundHalfAwayFromZero = (value) => {
    if (!Number.isFinite(value) || value === 0) {
      return value
    }

    const absValue = Math.abs(value)
    const rounded = Math.floor(absValue + 0.5)
    return Math.sign(value) * rounded
  }

  const STRPTIME_EXPANSIONS = [
    ['%h', '%b'],
    ['%D', '%m/%d/%y'],
    ['%F', '%Y-%m-%d'],
    ['%R', '%H:%M'],
    ['%T', '%H:%M:%S'],
    ['%r', '%I:%M:%S %p'],
    ['%X', '%H:%M:%S'],
    ['%x', '%m/%d/%y'],
    ['%c', '%a %b %e %H:%M:%S %Y']
  ]

  const expandStrptimeFormat = (format) => {
    let previous
    do {
      previous = format
      for (const [token, replacement] of STRPTIME_EXPANSIONS) {
        format = format.split(token).join(replacement)
      }
    } while (format !== previous)
    return format
  }

  const MONTH_NAME_TO_INDEX = (() => {
    const map = new Map()
    MONTH_SHORT.forEach((name, index) => {
      map.set(name.toLowerCase(), index)
    })
    MONTH_LONG.forEach((name, index) => {
      map.set(name.toLowerCase(), index)
    })
    return map
  })()

  const WEEKDAY_NAME_TO_INDEX = (() => {
    const map = new Map()
    WEEKDAY_SHORT.forEach((name, index) => {
      map.set(name.toLowerCase(), index)
    })
    WEEKDAY_LONG.forEach((name, index) => {
      map.set(name.toLowerCase(), index)
    })
    return map
  })()

  const escapeRegex = (value) => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')

  const padNumber = (value, width, padding = '0') => {
    const string = Math.abs(value).toString()
    return (value < 0 ? '-' : '') + string.padStart(width, padding)
  }

  const parseStrptime = (input, format) => {
    if (typeof input !== 'string' || typeof format !== 'string') {
      return null
    }

    const tm = {
      tm_sec: 0,
      tm_min: 0,
      tm_hour: 0,
      tm_mday: 1,
      tm_mon: 0,
      tm_year: 70,
      tm_wday: 0,
      tm_yday: 0,
      tm_isdst: -1,
      tm_gmtoff: 0,
      tm_zoneName: null
    }

    const state = {
      hour12: null,
      ampm: null,
      yearTwoDigit: null,
      century: null,
      secondsEpoch: null,
      ydayExplicit: false,
      wdayExplicit: false,
      yearSet: false
    }

    const expandedFormat = expandStrptimeFormat(format)
    const tokens = []
    let pattern = ''

    const addToken = (regexPart, apply) => {
      pattern += '(' + regexPart + ')'
      tokens.push(apply)
    }

    const addNonCapturing = (regexPart) => {
      pattern += '(?:' + regexPart + ')'
    }

    for (let i = 0; i < expandedFormat.length; ++i) {
      const ch = expandedFormat[i]
      if (ch === '%') {
        i += 1
        const spec = expandedFormat[i]
        if (spec === undefined) {
          return null
        }

        switch (spec) {
          case '%':
            pattern += '%'
            break
          case 'n':
          case 't':
            addNonCapturing('\\s+')
            break
          case 'Y':
            addToken('\\d{4}', (value) => {
              tm.tm_year = Number.parseInt(value, 10) - 1900
              state.yearSet = true
              return true
            })
            break
          case 'y':
            addToken('\\d{2}', (value) => {
              state.yearTwoDigit = Number.parseInt(value, 10)
              return true
            })
            break
          case 'C':
            addToken('\\d{1,2}', (value) => {
              state.century = Number.parseInt(value, 10)
              return true
            })
            break
          case 'm':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 1 || number > 12) return false
              tm.tm_mon = number - 1
              return true
            })
            break
          case 'd':
          case 'e':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 1 || number > 31) return false
              tm.tm_mday = number
              return true
            })
            break
          case 'H':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 0 || number > 23) return false
              tm.tm_hour = number
              return true
            })
            break
          case 'M':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 0 || number > 59) return false
              tm.tm_min = number
              return true
            })
            break
          case 'S':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 0 || number > 61) return false
              tm.tm_sec = number
              return true
            })
            break
          case 'I':
            addToken('\\d{1,2}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 1 || number > 12) return false
              state.hour12 = number % 12
              return true
            })
            break
          case 'p':
            addToken('(AM|PM|am|pm)', (value) => {
              state.ampm = value.toLowerCase()
              return true
            })
            break
          case 'a':
          case 'A': {
            const names = Array.from(WEEKDAY_NAME_TO_INDEX.keys())
              .sort((a, b) => b.length - a.length)
              .map(escapeRegex)
              .join('|')
            addToken(names, (value) => {
              const index = WEEKDAY_NAME_TO_INDEX.get(value.toLowerCase())
              if (index === undefined) return false
              tm.tm_wday = index % 7
              state.wdayExplicit = true
              return true
            })
            break
          }
          case 'b':
          case 'B': {
            const names = Array.from(MONTH_NAME_TO_INDEX.keys())
              .sort((a, b) => b.length - a.length)
              .map(escapeRegex)
              .join('|')
            addToken(names, (value) => {
              const index = MONTH_NAME_TO_INDEX.get(value.toLowerCase())
              if (index === undefined) return false
              tm.tm_mon = index
              return true
            })
            break
          }
          case 'j':
            addToken('\\d{1,3}', (value) => {
              const number = Number.parseInt(value, 10)
              if (number < 1 || number > 366) return false
              tm.tm_yday = number - 1
              state.ydayExplicit = true
              return true
            })
            break
          case 'u':
            addToken('[1-7]', (value) => {
              const number = Number.parseInt(value, 10)
              tm.tm_wday = number % 7
              state.wdayExplicit = true
              return true
            })
            break
          case 'w':
            addToken('[0-6]', (value) => {
              tm.tm_wday = Number.parseInt(value, 10)
              state.wdayExplicit = true
              return true
            })
            break
          case 's':
            addToken('-?\\d+', (value) => {
              const number = Number.parseInt(value, 10)
              if (!Number.isFinite(number)) return false
              state.secondsEpoch = number
              return true
            })
            break
          case 'Z':
            addToken('[A-Za-z]{1,8}', (value) => {
              tm.tm_zoneName = value
              return true
            })
            break
          case 'z':
            addToken('[+-]\\d{2}:?\\d{2}', (value) => {
              const normalized = value.replace(':', '')
              const sign = normalized[0] === '-' ? -1 : 1
              const hours = Number.parseInt(normalized.slice(1, 3), 10)
              const minutes = Number.parseInt(normalized.slice(3, 5) || '0', 10)
              tm.tm_gmtoff = sign * (hours * 3600 + minutes * 60)
              return true
            })
            break
          default:
            return null
        }
      } else if (/\s/.test(ch)) {
        pattern += '\\s*'
      } else {
        pattern += escapeRegex(ch)
      }
    }

    const regex = new RegExp('^' + pattern, 'i')
    const match = regex.exec(input)
    if (!match) {
      return null
    }

    let groupIndex = 1
    for (const apply of tokens) {
      const value = match[groupIndex++] || ''
      if (apply && apply(value, tm, state) === false) {
        return null
      }
    }

    if (state.yearTwoDigit != null) {
      const century =
        state.century != null
          ? state.century
          : state.yearTwoDigit >= 69
            ? 19
            : 20
      tm.tm_year = century * 100 + state.yearTwoDigit - 1900
    } else if (state.century != null && !state.yearSet) {
      tm.tm_year = state.century * 100 - 1900
    }

    if (state.hour12 != null) {
      let hour = state.hour12 % 12
      if (state.ampm === 'pm' && hour !== 12) {
        hour += 12
      } else if (state.ampm === 'am' && hour === 12) {
        hour = 0
      }
      tm.tm_hour = hour
    }

    if (state.secondsEpoch != null) {
      const date = new Date(state.secondsEpoch * 1000)
      if (!Number.isFinite(date.getTime())) {
        return null
      }
      const normalized = buildTmComponents(date, true)
      tm.tm_sec = normalized.tm_sec
      tm.tm_min = normalized.tm_min
      tm.tm_hour = normalized.tm_hour
      tm.tm_mday = normalized.tm_mday
      tm.tm_mon = normalized.tm_mon
      tm.tm_year = normalized.tm_year
      tm.tm_wday = normalized.tm_wday
      tm.tm_yday = normalized.tm_yday
      tm.tm_gmtoff = normalized.tm_gmtoff
      tm.tm_zoneName = normalized.tm_zoneName
    }

    tm.tm_gmtoff = tm.tm_gmtoff ?? 0

    const year = tm.tm_year + 1900
    const month = tm.tm_mon
    const day = tm.tm_mday
    const calendarDateUTC = new Date(Date.UTC(year, month, day))
    if (!Number.isFinite(calendarDateUTC.getTime())) {
      return null
    }

    if (!state.ydayExplicit) {
      tm.tm_yday = computeYDay(calendarDateUTC, true)
    }

    if (!state.wdayExplicit) {
      tm.tm_wday = calendarDateUTC.getUTCDay()
    }

    tm.tm_isdst = tm.tm_isdst ?? -1
    return { tm, consumed: match[0].length }
  }

  const formatOffset = (seconds) => {
    const sign = seconds >= 0 ? '+' : '-'
    const absolute = Math.abs(seconds)
    const hours = Math.floor(absolute / 3600)
    const minutes = Math.floor((absolute % 3600) / 60)
    return `${sign}${hours.toString().padStart(2, '0')}${minutes.toString().padStart(2, '0')}`
  }

  const dateFromTm = (tm) => {
    const year = tm.tm_year + 1900
    const base = Date.UTC(
      year,
      tm.tm_mon,
      tm.tm_mday,
      tm.tm_hour,
      tm.tm_min,
      tm.tm_sec
    )
    const offset = tm.tm_gmtoff ? tm.tm_gmtoff * 1000 : 0
    return new Date(base - offset)
  }

  const calculateWeekNumber = (tm, mondayFirst, dayOfYearOverride = null) => {
    const date = dateFromTm(tm)
    const year = date.getUTCFullYear()
    const firstDay = new Date(Date.UTC(year, 0, 1))
    const firstWeekday = mondayFirst
      ? (firstDay.getUTCDay() + 6) % 7
      : firstDay.getUTCDay()
    const offset = (7 - firstWeekday) % 7
    const dayOfYear = dayOfYearOverride ?? tm.tm_yday
    if (dayOfYear < offset) {
      return 0
    }
    return Math.floor((dayOfYear - offset) / 7) + 1
  }

  const formatStrftime = (format, tm) => {
    if (!format) return ''

    const result = []
    const date = dateFromTm(tm)
    const dayOfYear = tm.tm_yday >= 0 ? tm.tm_yday : computeYDay(date, true)

    for (let i = 0; i < format.length; ++i) {
      const char = format[i]
      if (char !== '%') {
        result.push(char)
        continue
      }

      if (i + 1 >= format.length) {
        result.push('%')
        break
      }

      const spec = format[++i]
      switch (spec) {
        case 'a':
          result.push(WEEKDAY_SHORT[(tm.tm_wday + 7) % 7])
          break
        case 'A':
          result.push(WEEKDAY_LONG[(tm.tm_wday + 7) % 7])
          break
        case 'b':
          result.push(MONTH_SHORT[((tm.tm_mon % 12) + 12) % 12])
          break
        case 'B':
          result.push(MONTH_LONG[((tm.tm_mon % 12) + 12) % 12])
          break
        case 'D':
          result.push(formatStrftime('%m/%d/%y', tm))
          break
        case 'c':
          result.push(formatStrftime('%a %b %e %H:%M:%S %Y', tm))
          break
        case 'C':
          result.push(padNumber(Math.floor((tm.tm_year + 1900) / 100), 2))
          break
        case 'd':
          result.push(padNumber(tm.tm_mday, 2))
          break
        case 'e':
          result.push(padNumber(tm.tm_mday, 2, ' '))
          break
        case 'F':
          result.push(formatStrftime('%Y-%m-%d', tm))
          break
        case 'H':
          result.push(padNumber(tm.tm_hour, 2))
          break
        case 'I': {
          const hour12 = ((tm.tm_hour + 11) % 12) + 1
          result.push(padNumber(hour12, 2))
          break
        }
        case 'j':
          result.push((dayOfYear + 1).toString().padStart(3, '0'))
          break
        case 'h':
          result.push(MONTH_SHORT[((tm.tm_mon % 12) + 12) % 12])
          break
        case 'k':
          result.push(padNumber(tm.tm_hour, 2, ' '))
          break
        case 'l': {
          const hour12 = ((tm.tm_hour + 11) % 12) + 1
          result.push(padNumber(hour12, 2, ' '))
          break
        }
        case 'm':
          result.push(padNumber(tm.tm_mon + 1, 2))
          break
        case 'M':
          result.push(padNumber(tm.tm_min, 2))
          break
        case 'n':
          result.push('\n')
          break
        case 'p':
          result.push(tm.tm_hour < 12 ? 'AM' : 'PM')
          break
        case 'P':
          result.push(tm.tm_hour < 12 ? 'am' : 'pm')
          break
        case 'r':
          result.push(formatStrftime('%I:%M:%S %p', tm))
          break
        case 'R':
          result.push(formatStrftime('%H:%M', tm))
          break
        case 's': {
          const seconds = Math.floor(date.getTime() / 1000)
          result.push(seconds.toString())
          break
        }
        case 'S':
          result.push(padNumber(tm.tm_sec, 2))
          break
        case 't':
          result.push('\t')
          break
        case 'T':
          result.push(formatStrftime('%H:%M:%S', tm))
          break
        case 'u': {
          const weekday = ((tm.tm_wday + 6) % 7) + 1
          result.push(weekday.toString())
          break
        }
        case 'U':
          result.push(
            calculateWeekNumber(tm, false, dayOfYear)
              .toString()
              .padStart(2, '0')
          )
          break
        case 'w':
          result.push(((tm.tm_wday + 7) % 7).toString())
          break
        case 'W':
          result.push(
            calculateWeekNumber(tm, true, dayOfYear).toString().padStart(2, '0')
          )
          break
        case 'x':
          result.push(formatStrftime('%m/%d/%y', tm))
          break
        case 'X':
          result.push(formatStrftime('%H:%M:%S', tm))
          break
        case 'y':
          result.push((tm.tm_year % 100).toString().padStart(2, '0'))
          break
        case 'Y':
          result.push((tm.tm_year + 1900).toString())
          break
        case 'z':
          result.push(formatOffset(tm.tm_gmtoff ?? 0))
          break
        case 'Z':
          if (tm.tm_zoneName) {
            result.push(tm.tm_zoneName)
          }
          break
        case '%':
          result.push('%')
          break
        default:
          result.push('%' + spec)
          break
      }
    }

    return result.join('')
  }

  const setErrno = (code) => {
    const locator = imports.env.__errno_location
    if (typeof locator === 'function') {
      env.adapter.setInt32(locator(), code)
    }
  }

  const setEndPointer = (endPointer, value) => {
    if (endPointer) {
      env.adapter.setUint32(endPointer, value >>> 0)
    }
  }

  const parseFloatingValue = (stringPointer, endPointer) => {
    const originalPointer = stringPointer >>> 0

    if (!stringPointer) {
      setEndPointer(endPointer, 0)
      return { converted: false, value: 0, token: '' }
    }

    let ptr = originalPointer
    let code = env.adapter.getUint8(ptr)

    while (code !== 0 && isWhitespace(code)) {
      ptr += 1
      code = env.adapter.getUint8(ptr)
    }

    const remaining = env.adapter.getString(ptr) ?? ''

    if (remaining.length === 0) {
      setEndPointer(endPointer, originalPointer)
      return { converted: false, value: 0, token: '' }
    }

    const match = remaining.match(FLOAT_REGEX)

    if (!match) {
      setEndPointer(endPointer, originalPointer)
      return { converted: false, value: 0, token: '' }
    }

    const token = match[0]
    const endPtr = ptr + token.length

    setEndPointer(endPointer, endPtr)

    const lower = token.toLowerCase()

    if (lower.startsWith('nan')) {
      return { converted: true, value: Number.NaN, token }
    }

    if (
      lower === 'inf' ||
      lower === '+inf' ||
      lower === 'infinity' ||
      lower === '+infinity'
    ) {
      return { converted: true, value: Number.POSITIVE_INFINITY, token }
    }

    if (lower === '-inf' || lower === '-infinity') {
      return { converted: true, value: Number.NEGATIVE_INFINITY, token }
    }

    let parsed = Number.NaN

    if (HEX_FLOAT_REGEX.test(token)) {
      parsed = parseHexFloatLiteral(token)
    } else {
      parsed = Number.parseFloat(token)
    }

    return { converted: true, value: parsed, token }
  }

  const parseIntegerValue = (
    stringPointer,
    endPointer,
    base,
    bits,
    unsigned
  ) => {
    if (typeof base !== 'number' || Number.isNaN(base)) {
      base = 10
    } else {
      base = Math.trunc(base)
    }

    const originalPointer = stringPointer >>> 0

    if (!stringPointer) {
      setEndPointer(endPointer, 0)
      return unsigned && bits === 64 ? 0n : 0
    }

    let ptr = originalPointer
    let code = env.adapter.getUint8(ptr)

    while (code !== 0 && isWhitespace(code)) {
      ptr += 1
      code = env.adapter.getUint8(ptr)
    }

    let sign = 1

    if (code === 0x2d || code === 0x2b) {
      if (code === 0x2d) {
        sign = -1
      }
      ptr += 1
      code = env.adapter.getUint8(ptr)
    }

    let detectedBase = base

    if (detectedBase === 0) {
      if (code === 0x30) {
        const next = env.adapter.getUint8(ptr + 1)
        if (next === 0x78 || next === 0x58) {
          detectedBase = 16
          ptr += 2
          code = env.adapter.getUint8(ptr)
        } else {
          detectedBase = 8
        }
      } else {
        detectedBase = 10
      }
    } else if (detectedBase === 16) {
      if (code === 0x30) {
        const next = env.adapter.getUint8(ptr + 1)
        if (next === 0x78 || next === 0x58) {
          ptr += 2
          code = env.adapter.getUint8(ptr)
        }
      }
    }

    if (detectedBase < 2 || detectedBase > 36) {
      setErrno(EINVAL)
      setEndPointer(endPointer, originalPointer)
      return unsigned && bits === 64 ? 0n : 0
    }

    const baseBig = BigInt(detectedBase)
    let bigValue = 0n
    let digits = 0
    let currentPtr = ptr

    while (true) {
      const ch = env.adapter.getUint8(currentPtr)
      if (ch === 0) {
        break
      }

      const digit = digitValue(ch)

      if (digit < 0 || digit >= detectedBase) {
        break
      }

      digits += 1
      bigValue = bigValue * baseBig + BigInt(digit)
      currentPtr += 1
    }

    if (digits === 0) {
      setEndPointer(endPointer, originalPointer)
      return unsigned && bits === 64 ? 0n : 0
    }

    setEndPointer(endPointer, currentPtr)

    if (unsigned) {
      let resultBig = bigValue
      if (sign === -1) {
        resultBig = -resultBig
      }

      if (bits === 64) {
        if (sign === 1 && bigValue > ULLONG_MAX) {
          setErrno(ERANGE)
          return ULLONG_MAX
        }

        if (sign === -1 && bigValue !== 0n) {
          setErrno(ERANGE)
          return ULLONG_MAX
        }

        return BigInt.asUintN(64, resultBig)
      }

      if (sign === 1 && bigValue > ULONG_MAX_BIG) {
        setErrno(ERANGE)
        return Number(ULONG_MAX_BIG)
      }

      if (sign === -1 && bigValue !== 0n) {
        setErrno(ERANGE)
        return Number(ULONG_MAX_BIG)
      }

      return Number(BigInt.asUintN(32, resultBig))
    }

    const resultBig = sign === -1 ? -bigValue : bigValue

    if (bits === 64) {
      if (resultBig < LLONG_MIN) {
        setErrno(ERANGE)
        return LLONG_MIN
      }

      if (resultBig > LLONG_MAX) {
        setErrno(ERANGE)
        return LLONG_MAX
      }

      return resultBig
    }

    if (resultBig < LONG_MIN_BIG) {
      setErrno(ERANGE)
      return LONG_MIN
    }

    if (resultBig > LONG_MAX_BIG) {
      setErrno(ERANGE)
      return LONG_MAX
    }

    return Number(resultBig)
  }

  // <assert.h>
  Object.assign(imports.env, {
    __assert_fail (
      conditionStringPointer,
      filenameStringPointer,
      lineNumber,
      functionStringPointer
    ) {
      console.error(
        'Assertion failed: (%s), function %s, file %s, line %d.',
        env.adapter.getString(conditionStringPointer),
        env.adapter.getString(functionStringPointer),
        env.adapter.getString(filenameStringPointer),
        lineNumber
      )

      imports.env.abort()
    }
  })

  // <ctype.h>
  Object.assign(imports.env, {
    isalnum (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return (byte >= 0x30 && byte <= 0x39) ||
        (byte >= 0x41 && byte <= 0x5a) ||
        (byte >= 0x61 && byte <= 0x7a)
        ? 1
        : 0
    },

    isalpha (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return (byte >= 0x41 && byte <= 0x5a) || (byte >= 0x61 && byte <= 0x7a)
        ? 1
        : 0
    },

    isascii (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      return code >= 0 && code <= 0x7f ? 1 : 0
    },

    isblank (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte === 0x09 || byte === 0x20 ? 1 : 0
    },

    iscntrl (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte <= 0x1f || byte === 0x7f ? 1 : 0
    },

    isdigit (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte >= 0x30 && byte <= 0x39 ? 1 : 0
    },

    isgraph (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte >= 0x21 && byte <= 0x7e ? 1 : 0
    },

    islower (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte >= 0x61 && byte <= 0x7a ? 1 : 0
    },

    isprint (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte >= 0x20 && byte <= 0x7e ? 1 : 0
    },

    ispunct (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff

      if (byte < 0x21 || byte > 0x7e) {
        return 0
      }

      if (
        (byte >= 0x30 && byte <= 0x39) ||
        (byte >= 0x41 && byte <= 0x5a) ||
        (byte >= 0x61 && byte <= 0x7a)
      ) {
        return 0
      }

      return 1
    },

    isspace (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      switch (byte) {
        case 0x09: // '\t'
        case 0x0a: // '\n'
        case 0x0b: // '\v'
        case 0x0c: // '\f'
        case 0x0d: // '\r'
        case 0x20: // ' '
          return 1
        default:
          return 0
      }
    },

    isupper (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return byte >= 0x41 && byte <= 0x5a ? 1 : 0
    },

    isxdigit (value) {
      const code = Number(value)
      if (!Number.isFinite(code) || code === EOF) {
        return 0
      }

      const byte = code & 0xff
      return (byte >= 0x30 && byte <= 0x39) ||
        (byte >= 0x41 && byte <= 0x46) ||
        (byte >= 0x61 && byte <= 0x66)
        ? 1
        : 0
    },

    toascii (value) {
      const code = Number(value)
      if (!Number.isFinite(code)) {
        return 0
      }

      if (code === EOF) {
        return EOF
      }

      return code & 0x7f
    },

    tolower (value) {
      const code = Number(value)
      if (!Number.isFinite(code)) {
        return 0
      }

      if (code === EOF) {
        return EOF
      }

      const byte = code & 0xff
      if (byte >= 0x41 && byte <= 0x5a) {
        return byte + 0x20
      }

      return byte
    },

    toupper (value) {
      const code = Number(value)
      if (!Number.isFinite(code)) {
        return 0
      }

      if (code === EOF) {
        return EOF
      }

      const byte = code & 0xff
      if (byte >= 0x61 && byte <= 0x7a) {
        return byte - 0x20
      }

      return byte
    }
  })

  // <errno.h>
  Object.assign(imports.env, {
    __errno_location () {
      if (!errnoPointer) {
        errnoPointer = env.adapter.heap.alloc(4)
      }

      return errnoPointer
    }
  })

  // <libgen.h>
  Object.assign(imports.env, {
    basename (pathStringPointer) {
      if (!basenamePointer) {
        basenamePointer = env.adapter.heap.alloc(PATH_MAX + 1)
      }

      env.adapter.heap.set(basenamePointer, 0, PATH_MAX)

      if (!pathStringPointer) {
        env.adapter.setString(basenamePointer, '.')
      } else {
        const string = env.adapter.getString(pathStringPointer)

        if (string) {
          if (string === '/') {
            env.adapter.setString(basenamePointer, '/')
          } else if (string === '.') {
            env.adapter.setString(basenamePointer, '.')
          } else {
            const basename = path.basename(string)
            if (basename.length > PATH_MAX) {
              setErrno(ENAMETOOLONG)
              return NULL
            }

            env.adapter.setString(basenamePointer, basename)
          }
        } else {
          env.adapter.setString(basenamePointer, '.')
        }
      }

      return basenamePointer
    },

    dirname (pathStringPointer) {
      if (!dirnamePointer) {
        dirnamePointer = env.adapter.heap.alloc(PATH_MAX + 1)
      }

      env.adapter.heap.set(dirnamePointer, 0, PATH_MAX)

      if (!pathStringPointer) {
        env.adapter.setString(dirnamePointer, '.')
      } else {
        const string = env.adapter.getString(pathStringPointer)

        if (string) {
          if (string === '/') {
            env.adapter.setString(dirnamePointer, '/')
          } else if (string === '.') {
            env.adapter.setString(dirnamePointer, '.')
          } else {
            const dirname = path.dirname(string)
            if (dirname.length > PATH_MAX) {
              setErrno(ENAMETOOLONG)
              return NULL
            }

            env.adapter.setString(dirnamePointer, dirname)
          }
        } else {
          env.adapter.setString(dirnamePointer, '.')
        }
      }

      return dirnamePointer
    }
  })

  // <math.h>
  Object.assign(imports.env, {
    __eqtf2 (x, y) {
      imports.env.abort()
    },
    __trunctfsf2 (low, high) {
      imports.env.abort()
    },
    __trunctfdf2 (low, high) {
      imports.env.abort()
    },
    acos (value) {
      return Math.acos(value)
    },
    acosl (value) {
      return Math.acos(value)
    },
    acosf (value) {
      return Math.acos(value)
    },
    agcosl (value) {
      return Math.acos(value)
    },
    acosh (value) {
      return Math.acosh(value)
    },
    acoshf (value) {
      return Math.acosh(value)
    },
    acoshl (value) {
      return Math.acosh(value)
    },

    asin (value) {
      return Math.asin(value)
    },
    asinf (value) {
      return Math.asin(value)
    },
    asinl (value) {
      return Math.asin(value)
    },
    asinh (value) {
      return Math.asinh(value)
    },
    asinhf (value) {
      return Math.asinh(value)
    },
    asinhl (value) {
      return Math.asinh(value)
    },

    atan (value) {
      return Math.atan(value)
    },
    atanf (value) {
      return Math.atan(value)
    },
    atanl (value) {
      return Math.atan(value)
    },
    atanh (value) {
      return Math.atanh(value)
    },
    atanhf (value) {
      return Math.atanh(value)
    },
    atanhl (value) {
      return Math.atanh(value)
    },
    atan2 (x, y) {
      return Math.atan2(x, y)
    },
    atan2f (x, y) {
      return Math.atan2(x, y)
    },
    atan2l (x, y) {
      return Math.atan2(x, y)
    },

    cbrt (value) {
      return Math.cbrt(value)
    },
    cbrtf (value) {
      return Math.cbrt(value)
    },
    cbrtl (value) {
      return Math.cbrt(value)
    },

    ceil (value) {
      return Math.ceil(value)
    },
    ceilf (value) {
      return Math.ceil(value)
    },
    ceill (value) {
      return Math.ceil(value)
    },

    copysign (x, y) {
      if (y === 0) {
        return Math.abs(x)
      }

      if (x === 0) {
        return 0
      }

      return Math.abs(x) * Math.sign(y)
    },

    copysignf (x, y) {
      return imports.env.copysign(x, y)
    },
    copysignl (x, y) {
      return imports.env.copysign(x, y)
    },

    cos (value) {
      return Math.cos(value)
    },
    cosf (value) {
      return Math.cos(value)
    },
    cosl (value) {
      return Math.cos(value)
    },
    cosh (value) {
      return Math.cosh(value)
    },
    coshf (value) {
      return Math.cosh(value)
    },
    coshl (value) {
      return Math.cosh(value)
    },

    erf (value) {
      // constants for the rational approximations
      const a1 = 0.254829592
      const a2 = -0.284496736
      const a3 = 1.421413741
      const a4 = -1.453152027
      const a5 = 1.061405429

      // approximation formula
      const x = value
      const t = 1.0 / (1.0 + 0.3275911 * x)
      const result =
        (a1 * t +
          a2 * t * t +
          a3 * t * t * t +
          a4 * t * t * t * t +
          a5 * t * t * t * t * t) *
        Math.exp(-x * x)

      return x >= 0 ? 1.0 - result : result - 1.0
    },

    erff (value) {
      return imports.env.erf(value)
    },
    erfl (value) {
      return imports.env.erf(value)
    },
    erfcf (value) {
      return 1 - imports.env.erf(value)
    },
    erfcl (value) {
      return 1 - imports.env.erf(value)
    },

    exp (value) {
      return Math.exp(value)
    },
    expf (value) {
      return Math.exp(value)
    },
    expl (value) {
      return Math.exp(value)
    },

    exp2 (value) {
      const x = value * 1.4426950408889634 // approximation of log2(2)
      const clamped = Math.max(-1022, Math.min(1023, x))
      const bias = clamped + 1023
      return new Float64Array(Float64Array.of(bias << 52).buffer)[0]
    },

    exp2f (value) {
      return imports.env.exp2(value)
    },
    exp2l (value) {
      return imports.env.exp2(value)
    },

    expm1 (value) {
      return Math.expm1(value)
    },
    expm1f (value) {
      return Math.expm1(value)
    },
    expm1l (value) {
      return Math.expm1(value)
    },

    fabs (value) {
      return Math.abs(value)
    },
    fabsf (value) {
      return Math.abs(value)
    },
    fabsl (value) {
      return Math.abs(value)
    },

    fdim (x, y) {
      return x > y ? x - y : 0.0
    },
    fdimf (x, y) {
      return x > y ? x - y : 0.0
    },
    fdiml (x, y) {
      return x > y ? x - y : 0.0
    },

    floor (value) {
      return Math.floor(value)
    },
    floorf (value) {
      return Math.floor(value)
    },
    floorl (value) {
      return Math.floor(value)
    },

    fma (x, y, z) {
      return x * y + z
    },
    fmaf (x, y, z) {
      return x * y + z
    },
    fmal (x, y, z) {
      return x * y + z
    },

    fmax (x, y) {
      return Math.max(x, y)
    },
    fmaxf (x, y) {
      return Math.max(x, y)
    },
    fmaxl (x, y) {
      return Math.max(x, y)
    },

    fmin (x, y) {
      return Math.min(x, y)
    },
    fminf (x, y) {
      return Math.min(x, y)
    },
    fminl (x, y) {
      return Math.min(x, y)
    },

    fmod (x, y) {
      return y === 0 ? 0 : x - y * Math.floor(x / y)
    },
    fmodf (x, y) {
      return y === 0 ? 0 : x - y * Math.floor(x / y)
    },
    fmodl (x, y) {
      return y === 0 ? 0 : x - y * Math.floor(x / y)
    },

    frexp (value, expPointer) {
      if (value === 0) {
        env.adapter.setInt32(expPointer, 0)
        return 0
      }

      let exponent = Math.floor(Math.log2(Math.abs(value)))
      // normalize the exponent for subnormal numbers
      if (Math.abs(value) < 1) {
        exponent -= 1
      }

      env.adapter.setInt32(expPointer, exponent)
      return value / Math.pow(2, exponent)
    },

    frexpf (value, expPointer) {
      return imports.env.frexp(value, expPointer)
    },
    frexpl (value, expPointer) {
      return imports.env.frexp(value, expPointer)
    },

    hypot (x, y) {
      return Math.hypot(x, y)
    },
    hypotf (x, y) {
      return Math.hypot(x, y)
    },
    hypotl (x, y) {
      return Math.hypot(x, y)
    },

    ilogb (value) {
      if (Number.isNaN(value)) {
        return Number.NaN
      }

      if (!Number.isFinite(value)) {
        return Infinity
      }

      if (value === 0) {
        return -Infinity
      }

      // calculate the unbiased evalueponent
      return Math.floor(Math.log2(Math.abs(value)))
    },

    ilogbf (value) {
      return imports.env.ilogb(value)
    },
    ilogbl (value) {
      return imports.env.ilogb(value)
    },

    ldexp (x, y) {
      if (Number.isNaN(x) || Number.isNaN(y)) {
        return Number.NaN
      }

      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        // returns nan or +/-infinity
        return x * y
      }

      if (x === 0) {
        return x
      }

      return x * Math.pow(2, y)
    },

    ldexpf (x, y) {
      return imports.env.ldexp(x, y)
    },
    ldexpl (x, y) {
      return imports.env.ldexp(x, y)
    },

    // @see {@link https://en.wikipedia.org/wiki/Gamma_function#Log-gamma_function}
    // @see {@link https://en.cppreference.com/w/c/numeric/math/lgamma}
    // @see {@link https://pubs.opengroup.org/onlinepubs/009696799/functions/lgamma.html}
    // @see {@link https://opensource.apple.com/source/Libm/Libm-230/ppc.subproj/lgamma.c.auto.html}
    // @see {@link https://apc.u-paris.fr/~franco/g4doxy/html/classG4VGaussianQuadrature.html#d88f4c34f9215434708f08ac9d5b68a7}
    lgamma (value) {
      if (Number.isNaN(value)) {
        return Number.NaN
      }

      if (!Number.isFinite(value)) {
        return Infinity
      }

      if (value === 0) {
        return -Infinity
      }

      let x = value

      const g = 7
      const p = [
        // coefficients
        0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
      ]

      if (x < 0.5) {
        // recursive computation
        return Math.log(
          Math.PI / (Math.sin(Math.PI * x) * imports.env.lgamma(1 - x))
        )
      }

      x -= 1

      let a = p[0]
      const t = x + g + 0.5

      for (let i = 1; i < p.length; ++i) {
        a += p[i] / (x + i)
      }

      const C = 2.5066282746310007 // 2.5066282746310005
      return Math.log((C * a) / x) + (x + 0.5) * Math.log(t) - t
    },

    lgammaf (value) {
      return imports.env.lgamma(value)
    },
    lgammal (value) {
      return imports.env.lgamma(value)
    },

    llrint (value) {
      if (Number.isNaN(value)) {
        return 0n
      }

      if (!Number.isFinite(value)) {
        return value
      }

      const rounded = roundTiesToEven(value)
      return BigInt(Math.trunc(rounded))
    },

    llrintf (value) {
      return imports.env.llrint(Math.fround(value))
    },

    llrintl (value) {
      return imports.env.llrint(value)
    },
    lrint (value) {
      return roundTiesToEven(value)
    },
    lrintf (value) {
      return roundTiesToEven(Math.fround(value))
    },
    lrintl (value) {
      return roundTiesToEven(value)
    },

    llround (value) {
      if (Number.isNaN(value)) {
        return 0n
      }

      if (!Number.isFinite(value)) {
        return 0n
      }

      const rounded = roundHalfAwayFromZero(value)
      return BigInt(Math.trunc(rounded))
    },
    llroundf (value) {
      return imports.env.llround(Math.fround(value))
    },
    llroundl (value) {
      return imports.env.llround(value)
    },

    rint (value) {
      return roundTiesToEven(value)
    },

    rintf (value) {
      const floatValue = Math.fround(value)
      return Math.fround(roundTiesToEven(floatValue))
    },

    rintl (value) {
      return roundTiesToEven(value)
    },

    log (value) {
      return Math.log(value)
    },
    logf (value) {
      return Math.log(value)
    },
    logl (value) {
      return Math.log(value)
    },

    log10 (value) {
      return Math.log10(value)
    },
    log10f (value) {
      return Math.log10(value)
    },
    log10l (value) {
      return Math.log10(value)
    },

    log1p (value) {
      return Math.log1p(value)
    },
    log1pf (value) {
      return Math.log1p(value)
    },
    log1pl (value) {
      return Math.log1p(value)
    },

    log2 (value) {
      return Math.log2(value)
    },
    log2f (value) {
      return Math.log2(value)
    },
    log2l (value) {
      return Math.log2(value)
    },

    logb (value) {
      if (Number.isNaN(value)) {
        return Number.NaN
      }

      if (!Number.isFinite(value)) {
        return Infinity
      }

      if (value === 0) {
        return -Infinity
      }

      return Math.floor(Math.log(Math.abs(value)) / Math.log(2))
    },

    logbf (value) {
      return imports.env.logb(value)
    },
    logbl (value) {
      return imports.env.logb(value)
    },

    lround (value) {
      return roundHalfAwayFromZero(value)
    },
    lroundf (value) {
      return roundHalfAwayFromZero(Math.fround(value))
    },
    lroundl (value) {
      return roundHalfAwayFromZero(value)
    },

    modf (value, integralPointer) {
      if (Number.isNaN(value)) {
        if (integralPointer) env.adapter.setFloat64(integralPointer, Number.NaN)
        return Number.NaN
      }

      if (!Number.isFinite(value)) {
        if (integralPointer) env.adapter.setFloat64(integralPointer, value)
        return 0
      }

      const integralPart = value < 0 ? Math.ceil(value) : Math.floor(value)
      if (integralPointer) {
        env.adapter.setFloat64(integralPointer, integralPart)
      }
      return value - integralPart
    },

    modff (value, integralPointer) {
      return imports.env.modf(value, integralPointer)
    },
    modfl (value, integralPointer) {
      return imports.env.modf(value, integralPointer)
    },

    nearbyint (value) {
      return roundTiesToEven(value)
    },

    nearbyintf (value) {
      const floatValue = Math.fround(value)
      return Math.fround(roundTiesToEven(floatValue))
    },
    nearbyintl (value) {
      return roundTiesToEven(value)
    },

    nextafter (x, y) {
      if (Number.isNaN(x) || Number.isNaN(y)) {
        return Number.NaN
      }

      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        return x
      }

      if (x === y) {
        return y
      }

      // calculate the next representable floating-point number in the direction of y
      return x + Math.sign(y - x) * EPSILON
    },

    nextafterf (x, y) {
      return imports.env.nextafter(x, y)
    },
    nextafterl (x, y) {
      return imports.env.nextafter(x, y)
    },

    nexttoward (x, y) {
      if (Number.isNaN(x) || Number.isNaN(y)) {
        return Number.NaN
      }

      if (!isFinite(x) || !isFinite(y)) {
        return x
      }

      if (x === y) {
        return y
      }

      // Calculate the next representable floating-point number toward y
      const diff = y - x
      if (diff === 0) {
        return y
      }

      // adjust the floating-point number based on the magnitude of y - x
      const scale = Math.abs(y) + Math.abs(x)
      return x + diff * EPSILON * (scale + 1)
    },

    nexttowardf (x, y) {
      return imports.env.nexttoward(x, y)
    },
    nexttowardl (x, y) {
      return imports.env.nexttoward(x, y)
    },

    pow (x, y) {
      return Math.pow(x, y)
    },
    powf (x, y) {
      return Math.pow(x, y)
    },
    powl (x, y) {
      return Math.pow(x, y)
    },

    remainder (x, y) {
      if (Number.isNaN(x) || Number.isNaN(y)) {
        return Number.NaN
      }

      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        return NaN
      }

      return x % y
    },

    remainderf (x, y) {
      return imports.env.remainder(x, y)
    },
    remainderl (x, y) {
      return imports.env.remainder(x, y)
    },

    remquo (x, y, quotPointer) {
      if (
        !Number.isFinite(x) ||
        !Number.isFinite(y) ||
        Number.isNaN(x) ||
        Number.isNaN(y)
      ) {
        if (quotPointer) env.adapter.setInt32(quotPointer, 0)
        return Number.NaN
      }

      const rem = x % y
      if (quotPointer) {
        env.adapter.setInt32(quotPointer, Math.trunc((x - rem) / y))
      }
      return rem
    },

    remquof (x, y, quotPointer) {
      return imports.env.remquo(x, y, quotPointer)
    },
    remquol (x, y, quotPointer) {
      return imports.env.remquo(x, y, quotPointer)
    },

    round (value) {
      return roundHalfAwayFromZero(value)
    },
    roundf (value) {
      const floatValue = Math.fround(value)
      return Math.fround(roundHalfAwayFromZero(floatValue))
    },
    roundl (value) {
      return roundHalfAwayFromZero(value)
    },

    scalbn (x, y) {
      if (
        Number.isNaN(x) ||
        !Number.isFinite(x) ||
        Number.isNaN(y) ||
        !Number.isFinite(y)
      ) {
        return Number.NaN
      }

      return x * Math.pow(2, y)
    },

    scalblnf (x, y) {
      return imports.env.scalbn(x, y)
    },
    scalblnl (x, y) {
      return imports.env.scalbn(x, y)
    },
    scalbln (x, y) {
      return imports.env.scalbn(x, y)
    },
    scalbnf (x, y) {
      return imports.env.scalbn(x, y)
    },
    scalbnl (x, y) {
      return imports.env.scalbn(x, y)
    },

    sin (value) {
      return Math.sin(value)
    },
    sinf (value) {
      return Math.sin(value)
    },
    sinl (value) {
      return Math.sin(value)
    },
    sinh (value) {
      return Math.sinh(value)
    },
    sinhf (value) {
      return Math.sinh(value)
    },
    sinhl (value) {
      return Math.sinh(value)
    },

    sqrt (value) {
      return Math.sqrt(value)
    },
    sqrtf (value) {
      return Math.sqrt(value)
    },
    sqrtl (value) {
      return Math.sqrt(value)
    },

    tan (value) {
      return Math.tan(value)
    },
    tanf (value) {
      return Math.tan(value)
    },
    tanl (value) {
      return Math.tan(value)
    },
    tanh (value) {
      return Math.tanh(value)
    },
    tanhf (value) {
      return Math.tanh(value)
    },
    tanhl (value) {
      return Math.tanh(value)
    },

    tgamma (value) {
      if (Number.isNaN(value)) {
        return NaN
      }

      if (!Number.isFinite(value)) {
        return value
      }

      if (value === 0) {
        // The gamma function is undefined such that x = 0
        return Infinity
      }

      let x = value

      // lanczos approximation parameters
      // see https://en.wikipedia.org/wiki/Lanczos_approximation#Simple_implementation
      const g = 7
      const p = [
        // best coefficients we can do in javascript
        0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
      ]

      // lanczos approximation
      // see https://en.wikipedia.org/wiki/Lanczos_approximation
      if (x < 0.5) {
        // recursive computation
        return Math.PI / (Math.sin(Math.PI * x) * imports.env.tgamma(1 - x))
      }

      const t = x + g + 0.5
      let a = p[0]
      x -= 1

      for (let i = 1; i < g + 2; i++) {
        a += p[i] / (x + i)
      }

      return Math.sqrt(2 * Math.PI) * Math.pow(t, x + 0.5) * Math.exp(-t) * a
    },

    tgammaf (value) {
      return imports.env.tgamma(value)
    },
    tgammal (value) {
      return imports.env.tgamma(value)
    },

    trunc (value) {
      if (Number.isNaN(value)) {
        return Number.NaN
      }

      if (!Number.isFinite(value)) {
        return value
      }

      // truncate towards zero
      return value < 0 ? Math.ceil(value) : Math.floor(value)
    },

    truncf (value) {
      return imports.env.trunc(value)
    },
    truncl (value) {
      return imports.env.trunc(value)
    }
  })

  // <regex.h>
  Object.assign(imports.env, {
    regcomp (regexPointer, patternStringPointer, cflags) {
      if (!regexPointer || !patternStringPointer) {
        return -EINVAL
      }

      const string = env.adapter.getString(patternStringPointer)
      const flags = ['g']

      if ((cflags & REG_ICASE) === REG_ICASE) {
        flags.push('i')
      }

      const container = { regex: null, error: null }
      const view = new DataView(env.adapter.get(regexPointer).buffer)
      const pointer = env.adapter.createExternalReferenceValue(container)

      view.setInt32(0, 0, true)
      view.setInt32(1, pointer, true)

      try {
        container.regex = new RegExp(string, flags.join(''))
      } catch (err) {
        container.error = err
        return -EINVAL
      }

      return 0
    },

    regerror (errorCode, regexPointer, bufStringPointer, bufCount) {
      let message = ''

      if (regexPointer) {
        const view = new DataView(env.adapter.get(regexPointer).buffer)
        const containerPointer = view.getInt32(1, true)
        const container =
          env.adapter.getExternalReferenceValue(containerPointer)
        if (container?.error?.message) {
          message = container.error.message
        }
      }

      if (!message) {
        if (regexErrorMessages[errorCode]) {
          message = regexErrorMessages[errorCode]
        } else if (errorMessages[errorCode]) {
          message = errorMessages[errorCode]
        } else {
          message = 'Unknown regular expression error'
        }
      }

      const required = message.length + 1

      if (bufStringPointer && bufCount > 0) {
        const slice = message.slice(0, Math.max(bufCount - 1, 0))
        if (slice.length > 0) {
          env.adapter.setString(bufStringPointer, slice)
        }
        env.adapter.setUint8(bufStringPointer + slice.length, 0)
      }

      return required
    },

    regexec (
      regexPointer,
      stringPointer,
      matchCount,
      matchesPointer,
      eflags,
      ...args
    ) {
      if (!regexPointer || !stringPointer) {
        return -EINVAL
      }

      const view = new DataView(env.adapter.get(regexPointer).buffer)
      const pointer = view.getInt32(1, true)
      const container = env.adapter.getExternalReferenceValue(pointer)
      const string = env.adapter.getString(stringPointer)

      if (!string || !container) {
        return -EINVAL
      }

      if (!matchesPointer) {
        if (container.regex.test(string)) {
          return 0
        }
      } else {
        const matches = string.match(container.regex)
        const offsets = []

        let buffer = string

        for (let i = 0; i < Math.min(matchCount, matches.length); ++i) {
          const match = matches[i]

          if (!match) {
            break
          }

          const index = buffer.indexOf(match)

          if (index === -1) {
            break
          }

          offsets.push(
            index + (offsets[i - 1] || 0) + (matches[i - 1]?.length || 0)
          )

          // seek
          buffer = buffer.slice(index + match.length)
        }

        for (let i = 0; i < offsets.length; ++i) {
          const start = offsets[i]
          const end = start + matches[i].length
          env.adapter.setInt32(matchesPointer + i * 8, start)
          env.adapter.setInt32(matchesPointer + i * 8 + 4, end)
        }

        if (matches.length) {
          return 0
        }
      }

      return -REG_NOMATCH
    },

    regfree (regexPointer) {
      if (regexPointer) {
        const view = new DataView(env.adapter.get(regexPointer).buffer)
        const pointer = view.getInt32(1, true)
        env.adapter.removeExternalReferenceValue(pointer)
        env.adapter.heap.free(pointer)
      }
    }
  })

  // <stdio.h>
  Object.assign(imports.env, {
    printf (formatPointer, variadicArguments) {
      return imports.env.fprintf(STDOUT, formatPointer, variadicArguments)
    },

    fprintf (descriptorPointer, formatPointer, variadicArguments) {
      if (!formatPointer || !descriptorPointer) {
        return -EINVAL
      }

      const output = variadicFormattedestinationPointerringFromPointers(
        env,
        formatPointer,
        variadicArguments,
        /* encoded = */ false
      )

      if (descriptorPointer === STDOUT) {
        toBeFlushed.stdout.push(output)
      } else if (descriptorPointer === STDERR) {
        toBeFlushed.stderr.push(output)
      } else {
        return -EBADF
      }

      if (output.includes('\n')) {
        imports.env.fflush(descriptorPointer)
      }

      return output.length
    },

    sprintf (stringPointer, formatPointer, variadicArguments) {
      const output = variadicFormattedestinationPointerringFromPointers(
        env,
        formatPointer,
        variadicArguments
      )

      if (stringPointer !== 0) {
        const buffer = env.adapter.get(stringPointer)
        buffer.set(output)
        buffer[output.length] = 0
      }

      return output.length
    },

    snprintf (stringPointer, size, formatPointer, variadicArguments) {
      const output = variadicFormattedestinationPointerringFromPointers(
        env,
        formatPointer,
        variadicArguments
      )

      if (stringPointer !== 0 && size !== 0) {
        const buffer = env.adapter.get(stringPointer)
        const limit = Math.max(size - 1, 0)
        const written = Math.min(output.length, limit)

        if (written > 0) {
          buffer.set(output.subarray(0, written))
        }

        buffer[written] = 0
      }

      return output.length
    },

    fputs (stringPointer, descriptorPointer) {
      if (!stringPointer || !descriptorPointer) {
        return EOF
      }

      const string = env.adapter.getString(stringPointer)
      if (string == null) {
        return EOF
      }

      if (descriptorPointer === STDOUT) {
        console.log(string)
      } else if (descriptorPointer === STDERR) {
        console.error(string)
      } else {
        setErrno(EBADFD)
        return EOF
      }

      return 0
    },

    puts (stringPointer) {
      return imports.env.fputs(stringPointer, STDOUT)
    },

    fputc (byte, descriptorPointer) {
      const string = String.fromCharCode(byte)

      if (descriptorPointer === STDOUT) {
        toBeFlushed.stdout.push(string)
      } else if (descriptorPointer === STDERR) {
        toBeFlushed.stderr.push(string)
      } else {
        setErrno(EBADFD)
        return EOF
      }

      if (string.includes('\n')) {
        return imports.env.fflush(descriptorPointer)
      }

      return 0
    },

    putc (byte, descriptorPointer) {
      return imports.env.fputc(byte, descriptorPointer)
    },

    putchar (byte) {
      return imports.env.fputc(byte, STDOUT)
    },

    fwrite (stringPointer, size, nitems, descriptorPointer) {
      if (!stringPointer || !descriptorPointer) {
        return -1
      }

      if (!size || !nitems) {
        return 0
      }

      const string = env.adapter.getString(stringPointer)

      if (!string) {
        return 0
      }

      let count = 0
      for (let i = 0; i < nitems; ++i) {
        if (imports.env.fprintf(descriptorPointer, string) > 0) {
          count++
        }
      }

      return count
    },

    fflush (descriptorPointer) {
      if (descriptorPointer === STDOUT) {
        console.log(toBeFlushed.stdout.join(''))
      } else if (descriptorPointer === STDERR) {
        console.error(toBeFlushed.stderr.join(''))
      } else if (descriptorPointer === NULL) {
        console.log(toBeFlushed.stdout.join(''))
        console.error(toBeFlushed.stderr.join(''))
      }

      return imports.env.fpurge(descriptorPointer)
    },

    fpurge (descriptorPointer) {
      if (descriptorPointer === STDOUT) {
        toBeFlushed.stdout.splice(0, toBeFlushed.stdout.length)
      } else if (descriptorPointer === STDERR) {
        toBeFlushed.stderr.splice(0, toBeFlushed.stderr.length)
      } else if (descriptorPointer === NULL) {
        toBeFlushed.stdout.splice(0, toBeFlushed.stdout.length)
        toBeFlushed.stderr.splice(0, toBeFlushed.stderr.length)
      }
    }
  })

  // <stdlib.h>
  Object.assign(imports.env, {
    /**
     * @param {number} stringPointer
     * @return {number}
     */
    atoi (stringPointer) {
      if (!stringPointer) {
        return 0
      }

      const string = env.adapter.getString(stringPointer)

      if (!string) {
        return 0
      }

      return parseInt(string.trim()) || 0
    },

    /**
     * @param {number} stringPointer
     * @return {number}
     */
    atol (stringPointer) {
      return imports.env.atoi(stringPointer)
    },

    /**
     * @param {number} stringPointer
     * @return {number}
     */
    atoll (stringPointer) {
      return imports.env.atoi(stringPointer)
    },

    /**
     * @param {number} stringPointer
     * @return {number}
     */
    atof (stringPointer) {
      const result = parseFloatingValue(stringPointer, 0)

      if (!result.converted) {
        return 0
      }

      const isInfLiteral = isInfinityLiteral(result.token)
      if (
        !Number.isFinite(result.value) &&
        !Number.isNaN(result.value) &&
        !isInfLiteral
      ) {
        setErrno(ERANGE)
      }

      if (
        !isInfLiteral &&
        result.value === 0 &&
        hasSignificantDigits(result.token)
      ) {
        setErrno(ERANGE)
      }

      return result.value
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @return {number}
     */
    strtof (stringPointer, endPointer = 0) {
      const result = parseFloatingValue(stringPointer, endPointer)

      if (!result.converted) {
        return 0
      }

      const value = Math.fround(result.value)
      const isInfLiteral = isInfinityLiteral(result.token)

      if (
        !Number.isFinite(result.value) &&
        !Number.isNaN(result.value) &&
        !isInfLiteral
      ) {
        setErrno(ERANGE)
      } else if (
        (value === 0 && result.value !== 0) ||
        (value === 0 && hasSignificantDigits(result.token))
      ) {
        setErrno(ERANGE)
      }

      return value
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @return {number}
     */
    strtod (stringPointer, endPointer = 0) {
      const result = parseFloatingValue(stringPointer, endPointer)

      if (!result.converted) {
        return 0
      }

      if (
        !Number.isFinite(result.value) &&
        !Number.isNaN(result.value) &&
        !isInfinityLiteral(result.token)
      ) {
        setErrno(ERANGE)
      }

      if (
        !isInfinityLiteral(result.token) &&
        result.value === 0 &&
        hasSignificantDigits(result.token)
      ) {
        setErrno(ERANGE)
      }

      return result.value
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @return {number}
     */
    strtold (stringPointer, endPointer = 0) {
      return imports.env.strtod(stringPointer, endPointer)
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @param {number} base
     * @return {number}
     */
    strtol (stringPointer, endPointer, base = 10) {
      return parseIntegerValue(stringPointer, endPointer, base, 32, false)
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @param {number} base
     * @return {number}
     */
    strtoul (stringPointer, endPointer, base = 10) {
      return parseIntegerValue(stringPointer, endPointer, base, 32, true)
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @param {number} base
     * @return {bigint}
     */
    strtoll (stringPointer, endPointer, base = 10) {
      return parseIntegerValue(stringPointer, endPointer, base, 64, false)
    },

    /**
     * @param {number} stringPointer
     * @param {number=} [endPointer]
     * @param {number} base
     * @return {bigint}
     */
    strtoull (stringPointer, endPointer, base = 10) {
      return parseIntegerValue(stringPointer, endPointer, base, 64, true)
    },

    /**
     * @return {number}
     */
    rand () {
      const x = Math.sin(srandSeed++) * 10000
      const y = x - Math.floor(x)
      // map the random number to the interval [0, RAND_MAX]
      return Math.floor(y * (RAND_MAX + 1))
    },

    /**
     * @param {number} seed
     */
    srand (seed) {
      // convert to unsigned integer
      srandSeed = new Uint32Array([seed])[0]
    },

    /**
     * @param {number} size
     * @return {number}
     */
    malloc (size) {
      return env.adapter.heap.alloc(size)
    },

    /**
     * @param {number} count
     * @param {number} size
     * @return {number}
     */
    calloc (count, size) {
      return env.adapter.heap.calloc(count, size)
    },

    /**
     * @param {number} pointer
     * @param {number} size
     * @return {number}
     */
    realloc (pointer, size) {
      return env.adapter.heap.realloc(pointer, size)
    },

    /**
     * @param {number} pointer
     */
    free (pointer) {
      return env.adapter.heap.free(pointer)
    },

    /**
     */
    abort () {
      console.error('Abort trap: 6') // SIGABORT == 6
      throw new WebAssembly.Exception(env.tags.abort, [])
    },

    /**
     * @param {number} callbackPointer
     */
    atexit (callbackPointer) {
      if (!callbackPointer) {
        return -1
      }

      atExitCallbackPointer = callbackPointer
      return 0
    },

    /**
     * @param {number} code
     */
    exit (code) {
      env.adapter.exitStatus = code
      // TODO(@jerle): exit WASM runtime with exit code
      env.adapter.indirectFunctionTable.call(atExitCallbackPointer)
      throw new WebAssembly.Exception(env.tags.exit, [code])
    },

    /**
     * @param {number} code
     */
    _Exit (code) {
      env.imports.exit(code)
    },

    /**
     * @param {number} callbackPointer
     */
    at_quick_exit (callbackPointer) {
      return env.imports.atexit(callbackPointer)
    },

    /**
     * @param {number} code
     */
    quick_exit (code) {
      env.imports.exit(code)
    },

    /**
     * @param {number} namePointer
     * @return {number}
     */
    getenv (namePointer) {
      if (!namePointer) {
        setErrno(EINVAL)
        return NULL
      }

      const name = env.adapter.getString(namePointer)

      if (!name) {
        setErrno(EINVAL)
        return NULL
      }

      if (!process.env[name]) {
        return NULL
      }

      return env.adapter.stack.push(process.env[name])
    },

    /**
     * @param {number} namePointer
     * @param {number} valuePointer
     * @param {number} overwrite
     */
    setenv (namePointer, valuePointer, overwrite) {
      if (!namePointer || !valuePointer) {
        setErrno(EINVAL)
        return -1
      }

      const name = env.adapter.getString(namePointer)
      const value = env.adapter.getString(valuePointer)

      if (!name || !value) {
        setErrno(EINVAL)
        return -1
      }

      if (name in process.env) {
        if (overwrite) {
          process.env[name] = value
        }
      } else {
        process.env[name] = value
      }

      return 0
    },

    /**
     * @param {number} namePointer
     */
    unsetenv (namePointer) {
      if (!namePointer) {
        setErrno(EINVAL)
        return -1
      }

      const name = env.adapter.getString(namePointer)
      if (!name) {
        setErrno(EINVAL)
        return -1
      }

      delete process.env[name]
      return 0
    },

    /**
     * @param {number} stringPointer
     */
    putenv (stringPointer) {
      if (!stringPointer) {
        setErrno(EINVAL)
        return -1
      }

      const string = env.adapter.getString(stringPointer)

      if (!string) {
        setErrno(EINVAL)
        return -1
      }

      const parts = string.split('=')

      if (parts.length !== 2) {
        setErrno(EINVAL)
        return -1
      }

      const [name, value] = parts

      process.env[name] = value

      return 0
    },

    /**
     * @return {number}
     */
    clearenv () {
      console.warn('clearenv: Operation is ignored')
      return -1
    },

    /**
     * @param {number} namePointer
     * @return {number}
     */
    secure_getenv (namePointer) {
      return imports.env.getenv(namePointer)
    },

    /**
     * @param {number} stringPointer
     */
    system (stringPointer) {
      console.warn('system: Operation is ignored')
      return -1
    },

    /**
     * @param {number} value
     * @return {number}
     */
    abs (value) {
      return Math.abs(parseInt(value))
    },

    /**
     * @param {number} value
     * @return {number}
     */
    labs (value) {
      return imports.env.abs(value)
    },

    /**
     * @param {number} value
     * @return {number}
     */
    llabs (value) {
      return imports.env.abs(value)
    },

    /**
     * @param {number} outputPointer
     * @param {number} numerator
     * @param {numerator} denominator
     */
    div (outputPointer, numerator, denominator) {
      const result = {
        quot: Math.floor(numerator / denominator),
        rem: numerator % denominator
      }

      env.adapter.setInt32(outputPointer, result.quot)
      env.adapter.setInt32(outputPointer + 4, result.rem)
    },

    /**
     * @param {number} outputPointer
     * @param {number} numerator
     * @param {numerator} denominator
     */
    ldiv (outputPointer, numerator, denominator) {
      return imports.env.div(outputPointer, numerator, denominator)
    },

    /**
     * @param {number} outputPointer
     * @param {number} numerator
     * @param {numerator} denominator
     */
    lldiv (outputPointer, numerator, denominator) {
      return imports.env.div(outputPointer, numerator, denominator)
    }
  })

  // <string.h>
  Object.assign(imports.env, {
    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @return {number}
     */
    memcpy (destinationPointer, sourcePointer, size) {
      if (!destinationPointer) {
        return NULL
      }

      if (sourcePointer && size) {
        const destination = new Uint8Array(
          env.adapter.buffer.buffer,
          destinationPointer,
          size
        )
        const source = new Uint8Array(
          env.adapter.buffer.buffer,
          sourcePointer,
          size
        )
        destination.set(source)
      }

      return destinationPointer
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @return {number}
     */
    memmove (destinationPointer, sourcePointer, size) {
      if (!destinationPointer) {
        return NULL
      }

      if (sourcePointer && size) {
        const destination = new Uint8Array(
          env.adapter.buffer.buffer,
          destinationPointer,
          size
        )
        const source = new Uint8Array(
          env.adapter.buffer.buffer,
          sourcePointer,
          size
        )

        if (destinationPointer < sourcePointer) {
          // copy from beginning to end
          destination.set(source)
        } else if (destinationPointer > sourcePointer) {
          // copy from end to beginning
          for (let i = size - 1; i >= 0; --i) {
            destination[i] = source[i]
          }
        } // else they overlap
      }

      return destinationPointer
    },

    /**
     * @param {number} destinationPointer
     * @param {number} byte
     * @param {number} size
     * @return {number}
     */
    memset (destinationPointer, byte, size) {
      if (!destinationPointer) {
        return NULL
      }

      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer,
        size
      )

      destination.fill(byte)
      return destinationPointer
    },

    /**
     * @param {number} left
     * @param {number} right
     * @param {number} size
     * @return {number}
     */
    memcmp (left, right, size) {
      const view1 = new Uint8Array(env.adapter.buffer.buffer, left, size)
      const view2 = new Uint8Array(env.adapter.buffer.buffer, right, size)

      for (let i = 0; i < size; ++i) {
        if (view1[i] !== view2[i]) {
          return view1[i] - view2[i]
        }
      }

      // left and right are equal
      return 0
    },

    /**
     * @param {number} stringPointer
     * @param {number} byte
     * @param {number} size
     * @return {number}
     */
    memchr (stringPointer, byte, size) {
      if (!stringPointer || size <= 0) {
        return NULL
      }

      const target = Number(byte) & 0xff

      for (let offset = 0; offset < size; ++offset) {
        const value = env.adapter.getUint8(stringPointer + offset)
        if (value === target) {
          return stringPointer + offset
        }
      }

      return NULL
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @return {number}
     */
    strcpy (destinationPointer, sourcePointer) {
      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer
      )
      const source = new Uint8Array(env.adapter.buffer.buffer, sourcePointer)
      let i = 0

      for (; source[i] !== 0; ++i) {
        destination[i] = source[i]
      }

      // "null-terminate" the destination string
      destination[i] = 0
      return destinationPointer
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @return {number}
     */
    strncpy (destinationPointer, sourcePointer, size) {
      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer
      )
      const source = new Uint8Array(env.adapter.buffer.buffer, sourcePointer)
      let i = 0

      while (i < size && source[i] !== 0) {
        destination[i] = source[i]
        i++
      }

      // Pad the remaining characters with null if necessary
      while (i < size) {
        destination[i] = 0
        i++
      }

      return destinationPointer
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @return {number}
     */
    strcat (destinationPointer, sourcePointer) {
      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer
      )
      const source = new Uint8Array(env.adapter.buffer.buffer, sourcePointer)
      let i = 0
      let j = 0

      while (destination[i] !== 0) {
        i++
      }

      // copy the source string to the end of the destination string
      while (source[j] !== 0) {
        destination[i + j] = source[j]
        j++
      }

      // "null-terminate" the concatenated string
      destination[i + j] = 0

      return destinationPointer
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @return {number}
     */
    strncat (destinationPointer, sourcePointer, size) {
      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer
      )
      const source = new Uint8Array(env.adapter.buffer.buffer, sourcePointer)
      let i = 0
      let j = 0

      while (destination[i] !== 0) {
        i++
      }

      // copy the source string up to `size` characters to
      // the end of the destination string
      while (j < size && source[j] !== 0) {
        destination[i + j] = source[j]
        j++
      }

      // "null-terminate" the concatenated string
      destination[i + j] = 0
      return destinationPointer
    },

    /**
     * @param {number} left
     * @param {number} right
     * @return {number}
     */
    strcmp (left, right) {
      const view1 = new Uint8Array(env.adapter.buffer.buffer, left)
      const view2 = new Uint8Array(env.adapter.buffer.buffer, right)
      let i = 0

      while (view1[i] !== 0 && view1[i] === view2[i]) {
        i++
      }

      return view1[i] - view2[i]
    },

    /**
     * @param {number} left
     * @param {number} right
     * @param {number} size
     * @return {number}
     */
    strncmp (left, right, size) {
      const view1 = new Uint8Array(env.adapter.buffer.buffer, left)
      const view2 = new Uint8Array(env.adapter.buffer.buffer, right)
      let i = 0

      while (i < size && view1[i] !== 0 && view1[i] === view2[i]) {
        i++
      }

      if (i === size || (view1[i] === 0 && view2[i] === 0)) {
        // strings are equal up to the specified size or both terminated
        return 0
      }

      return view1[i] - view2[i]
    },

    /**
     * @param {number} left
     * @param {number} right
     * @return {number}
     */
    strcoll (left, right) {
      const view1 = new Uint8Array(env.adapter.buffer.buffer, left)
      const view2 = new Uint8Array(env.adapter.buffer.buffer, right)
      const string1 = String.fromCharCode(...view1)
      const string2 = String.fromCharCode(...view2)

      return string1.localeCompare(string2)
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @param {number} localeStringPointer
     * @return {number}
     */
    strxfrm_l (destinationPointer, sourcePointer, size, localeStringPointer) {
      const destination = new Uint8Array(
        env.adapter.buffer.buffer,
        destinationPointer
      )
      const collation = {}
      const source = new Uint8Array(env.adapter.buffer.buffer, sourcePointer)
      const string = String.fromCharCode.apply(null, source)

      const locale = localeStringPointer
        ? env.adapter.getString(localeStringPointer)
        : globalThis.navigator.language

      // normalize and remove diacritics
      const transformed = string
        .normalize('NFD')
        .replace(/[\u0300-\u036f]/g, '')

      // convert to plain byte array
      const bytes = Array.from(transformed).map((char) =>
        String(char).charCodeAt(0)
      )
      const length = Math.min(size, bytes.length)

      // generate a collation map for the specified locale
      for (let i = 0; i < length; ++i) {
        const char = String.fromCharCode(bytes[i])

        if (!collation[char]) {
          // arbitrary string for comparison
          const comparisonString = `a${char}b`
          collation[char] = comparisonString.localeCompare('a', locale)
        }
      }

      const collated = bytes
        .slice(0, length)
        .sort((a, b) => collation[a] - collation[b])

      // copy transformed string to destination
      for (let i = 0; i < collated.length; ++i) {
        destination[i] = collated[i]
      }

      // "null-terminate" the transformed string
      destination[collated.length] = 0

      // return the length of the transformed string
      return collated.length
    },

    /**
     * @param {number} destinationPointer
     * @param {number} sourcePointer
     * @param {number} size
     * @return {number}
     */
    strxfrm (destinationPointer, sourcePointer, size) {
      return imports.env.strxfrm_l(
        destinationPointer,
        sourcePointer,
        size,
        NULL
      )
    },

    strchr (stringPointer, byte) {
      if (!stringPointer) {
        return NULL
      }

      const target = Number(byte) & 0xff
      let offset = 0

      while (true) {
        const value = env.adapter.getUint8(stringPointer + offset)

        if (value === target) {
          return stringPointer + offset
        }

        if (value === 0) {
          return NULL
        }

        offset += 1
      }
    },

    strrchr (stringPointer, byte) {
      if (!stringPointer) {
        return NULL
      }

      const target = byte & 0xff
      let offset = 0
      let lastOccurrence = NULL

      while (true) {
        const value = env.adapter.getUint8(stringPointer + offset)

        if (value === target) {
          lastOccurrence = stringPointer + offset
        }

        if (value === 0) {
          break
        }

        offset += 1
      }

      return lastOccurrence
    },

    strcspn (stringPointer, charsetPointer) {
      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      const charset = new Uint8Array(env.adapter.buffer.buffer, charsetPointer)
      let i = 0

      while (string[i] !== 0) {
        let j = 0

        while (charset[j] !== 0 && charset[j] !== string[i]) {
          j++
        }

        if (charset[j] !== 0) {
          return i
        }

        i++
      }

      return i
    },

    strspn (stringPointer, charsetPointer) {
      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      const charset = new Uint8Array(env.adapter.buffer.buffer, charsetPointer)
      let i = 0

      while (string[i] !== 0) {
        let j = 0

        while (charset[j] !== 0 && charset[j] !== string[i]) {
          j++
        }

        if (charset[j] === 0) {
          return i
        }

        i++
      }

      return i
    },

    strpbrk (stringPointer, charsetPointer) {
      if (!stringPointer || !charsetPointer) {
        return NULL
      }

      let stringOffset = 0

      while (true) {
        const value = env.adapter.getUint8(stringPointer + stringOffset)

        if (value === 0) {
          return NULL
        }

        let charsetOffset = 0
        while (true) {
          const candidate = env.adapter.getUint8(charsetPointer + charsetOffset)
          if (candidate === 0) {
            break
          }

          if (candidate === value) {
            return stringPointer + stringOffset
          }

          charsetOffset += 1
        }

        stringOffset += 1
      }
    },

    strstr (stringPointer, needlePointer) {
      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      const needle = new Uint8Array(env.adapter.buffer.buffer, needlePointer)

      for (let i = 0; string[i] !== 0; ++i) {
        let match = true

        for (let j = 0; needle[j] !== 0; ++j) {
          if (string[i + j] !== needle[j]) {
            match = false
            break
          }
        }

        if (match) {
          return stringPointer + i
        }
      }

      // return `NULL` if the substring is not found
      return 0
    },

    strtok (stringPointer, delimitersPointer) {
      if (!delimitersPointer) {
        return NULL
      }

      const isDelimiter = (code) => {
        let offset = 0
        while (true) {
          const value = env.adapter.getUint8(delimitersPointer + offset)
          if (value === 0) {
            return false
          }
          if (value === code) {
            return true
          }
          offset += 1
        }
      }

      if (stringPointer) {
        strtokStatePointer = stringPointer
      } else if (!strtokStatePointer) {
        return NULL
      }

      let currentPointer = strtokStatePointer

      // skip leading delimiters
      while (true) {
        const value = env.adapter.getUint8(currentPointer)
        if (value === 0) {
          strtokStatePointer = 0
          return NULL
        }
        if (!isDelimiter(value)) {
          break
        }
        currentPointer += 1
      }

      const tokenStartPointer = currentPointer

      while (true) {
        const value = env.adapter.getUint8(currentPointer)
        if (value === 0) {
          strtokStatePointer = 0
          return tokenStartPointer
        }

        if (isDelimiter(value)) {
          env.adapter.setUint8(currentPointer, 0)
          strtokStatePointer = currentPointer + 1
          return tokenStartPointer
        }

        currentPointer += 1
      }
    },

    strlen (stringPointer) {
      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      let size = 0

      while (string[size] !== 0) {
        size++
      }

      return size
    },

    strdup (stringPointer) {
      if (!stringPointer) {
        return NULL
      }

      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      let size = 0

      while (string[size] !== 0) {
        size++
      }

      const pointer = env.adapter.heap.alloc(size + 1)
      const duplicate = new Uint8Array(env.adapter.buffer.buffer, pointer)

      for (let i = 0; i < size; ++i) {
        duplicate[i] = string[i]
      }

      duplicate[size] = 0
      return pointer
    },

    strndup (stringPointer, maxSize) {
      if (!stringPointer) {
        return NULL
      }

      const string = new Uint8Array(env.adapter.buffer.buffer, stringPointer)
      let size = 0

      while (string[size] !== 0 && size < maxSize) {
        size++
      }

      const pointer = env.adapter.heap.alloc(size + 1)
      const duplicate = new Uint8Array(env.adapter.buffer.buffer, pointer)

      for (let i = 0; i < size; ++i) {
        duplicate[i] = string[i]
      }

      duplicate[size] = 0
      return pointer
    },

    strerror (errorCode) {
      if (errorCode >= 0 && errorCode in errorMessages) {
        const errorMessagePointer = env.adapter.errorMessagePointers[errorCode]

        if (!errorMessagePointer) {
          return NULL
        }

        const errorMessage = new Uint8Array(
          env.adapter.buffer.buffer,
          errorMessagePointer
        )

        // copy the error message to the allocated memory
        for (let i = 0; i < errorMessages[errorCode].length; ++i) {
          errorMessage[i] = errorMessages[errorCode].charCodeAt(i)
        }

        // null-terminate the string
        errorMessage[errorMessages[errorCode].length] = 0
        return errorMessagePointer
      }

      return NULL
    },

    strsignal (signal) {
      console.warn('strsignal: Operation is not supported')
      return NULL
    }
  })

  // <time.h>
  Object.assign(imports.env, {
    time (timePointer) {
      const now = Math.floor(Date.now() / 1000)
      if (timePointer) {
        writeTimeValue(timePointer, now)
      }
      return now
    },

    difftime (time1, time0) {
      return time1 - time0
    },

    mktime (outputPointer, tmPointer) {
      if (!tmPointer) {
        setErrno(EINVAL)
        return -1
      }

      const tm = readTmStruct(tmPointer)
      if (!tm) {
        setErrno(EINVAL)
        return -1
      }

      const date = new Date(
        tm.tm_year + 1900,
        tm.tm_mon,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
      )

      if (Number.isNaN(date.getTime())) {
        setErrno(EINVAL)
        return -1
      }

      const seconds = Math.floor(date.getTime() / 1000)
      const normalized = buildTmComponents(date, false)
      writeTmStruct(tmPointer, normalized)

      if (outputPointer) {
        writeTimeValue(outputPointer, seconds)
      }

      return seconds
    },

    strftime (stringPointer, maxSize, formatPointer, tmPointer) {
      if (!stringPointer || !formatPointer || !tmPointer) {
        setErrno(EINVAL)
        return 0
      }

      const format = env.adapter.getString(formatPointer) ?? ''
      const tm = readTmStruct(tmPointer)
      if (!tm) {
        setErrno(EINVAL)
        return 0
      }

      const formatted = formatStrftime(format, tm)
      if (formatted.length >= maxSize) {
        setErrno(ERANGE)
        return 0
      }

      writeCString(stringPointer, formatted)
      return formatted.length
    },

    strptime (bufferPointer, formatPointer, tmPointer) {
      if (!bufferPointer || !formatPointer) {
        setErrno(EINVAL)
        return NULL
      }

      const format = env.adapter.getString(formatPointer)
      const buffer = env.adapter.getString(bufferPointer)

      if (!format || !buffer) {
        setErrno(EINVAL)
        return NULL
      }

      const result = parseStrptime(buffer, format)
      if (!result) {
        setErrno(EINVAL)
        return NULL
      }

      if (tmPointer) {
        writeTmStruct(tmPointer, result.tm)
      }

      return bufferPointer + result.consumed
    },

    gmtime (timePointer) {
      const seconds = readTimeValue(timePointer)
      const date = new Date(seconds * 1000)

      if (Number.isNaN(date.getTime())) {
        setErrno(EINVAL)
        return NULL
      }

      if (!gmtimeStaticPointer) {
        gmtimeStaticPointer = env.adapter.heap.alloc(TM_STRUCT_SIZE)
      }

      const tm = buildTmComponents(date, true)
      writeTmStruct(gmtimeStaticPointer, tm)
      return gmtimeStaticPointer
    },

    timegm (tmPointer) {
      const tm = readTmStruct(tmPointer)
      if (!tm) {
        setErrno(EINVAL)
        return -1
      }

      const milliseconds = Date.UTC(
        tm.tm_year + 1900,
        tm.tm_mon,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
      )

      if (Number.isNaN(milliseconds)) {
        setErrno(EINVAL)
        return -1
      }

      return Math.floor(milliseconds / 1000)
    },

    localtime (timePointer) {
      const seconds = readTimeValue(timePointer)
      const date = new Date(seconds * 1000)

      if (Number.isNaN(date.getTime())) {
        setErrno(EINVAL)
        return NULL
      }

      if (!localtimeStaticPointer) {
        localtimeStaticPointer = env.adapter.heap.alloc(TM_STRUCT_SIZE)
      }

      const tm = buildTmComponents(date, false)
      writeTmStruct(localtimeStaticPointer, tm)
      return localtimeStaticPointer
    },

    asctime (tmPointer) {
      if (!tmPointer) {
        setErrno(EINVAL)
        return NULL
      }

      const tm = readTmStruct(tmPointer)
      if (!tm) {
        setErrno(EINVAL)
        return NULL
      }

      if (!asctimeStaticPointer) {
        asctimeStaticPointer = env.adapter.heap.alloc(ASCTIME_BUFFER_CAPACITY)
      }

      writeAsctimeToPointer(asctimeStaticPointer, tm)
      return asctimeStaticPointer
    },

    ctime (timePointer) {
      const tmPointer = imports.env.localtime(timePointer)
      if (!tmPointer) {
        return NULL
      }
      return imports.env.asctime(tmPointer)
    },

    stime (timePointer) {
      setErrno(EPERM)
      return -1
    },

    timespec_get (timeSpecPointer, base) {
      if (!timeSpecPointer) {
        setErrno(EINVAL)
        return 0
      }

      if (base !== TIME_UTC) {
        setErrno(EINVAL)
        return 0
      }

      const now = Date.now()
      const seconds = Math.floor(now / 1000)
      const nanoseconds = Math.floor((now % 1000) * 1e6)
      env.adapter.setInt32(timeSpecPointer, seconds)
      env.adapter.setInt32(timeSpecPointer + 4, nanoseconds)
      return base
    },

    getdate (stringPointer) {
      if (!stringPointer) {
        setErrno(EINVAL)
        return NULL
      }

      const input = env.adapter.getString(stringPointer)
      if (!input) {
        setErrno(EINVAL)
        return NULL
      }

      const date = new Date(input)
      if (!Number.isFinite(date.getTime())) {
        setErrno(EINVAL)
        return NULL
      }

      if (!getdateStaticPointer) {
        getdateStaticPointer = env.adapter.heap.alloc(TM_STRUCT_SIZE)
      }

      const tm = buildTmComponents(date, false)
      writeTmStruct(getdateStaticPointer, tm)
      return getdateStaticPointer
    },

    nanosleep (rqtp, tmtp) {
      if (!rqtp) {
        setErrno(EINVAL)
        return -1
      }

      const { seconds, nanoseconds } = readTimespec(rqtp)

      if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1e9) {
        setErrno(EINVAL)
        return -1
      }

      const result = ipc.sendSync('timers.nanosleep', {
        seconds,
        nanoseconds
      })

      if (result?.err) {
        const errnoValue =
          typeof result.err.errno === 'number'
            ? result.err.errno
            : typeof result.err.code === 'number'
              ? Math.abs(result.err.code)
              : EINVAL
        setErrno(errnoValue)
        return -1
      }

      if (tmtp) {
        writeTimespec(tmtp, 0, 0)
      }

      return 0
    },

    gmtime_r (timePointer, tmPointer) {
      if (!tmPointer) {
        setErrno(EINVAL)
        return NULL
      }

      const seconds = readTimeValue(timePointer)
      const date = new Date(seconds * 1000)

      if (Number.isNaN(date.getTime())) {
        setErrno(EINVAL)
        return NULL
      }

      const tm = buildTmComponents(date, true)
      writeTmStruct(tmPointer, tm)
      return tmPointer
    },

    localtime_r (timePointer, tmPointer) {
      if (!tmPointer) {
        setErrno(EINVAL)
        return NULL
      }

      const seconds = readTimeValue(timePointer)
      const date = new Date(seconds * 1000)

      if (Number.isNaN(date.getTime())) {
        setErrno(EINVAL)
        return NULL
      }

      const tm = buildTmComponents(date, false)
      writeTmStruct(tmPointer, tm)
      return tmPointer
    },

    asctime_r (tmPointer, outputPointer) {
      if (!tmPointer || !outputPointer) {
        setErrno(EINVAL)
        return NULL
      }
      const tm = readTmStruct(tmPointer)
      if (!tm) {
        setErrno(EINVAL)
        return NULL
      }
      writeAsctimeToPointer(outputPointer, tm)
      return outputPointer
    },

    time_r (timePointer, outputPointer) {
      if (!timePointer && !outputPointer) {
        setErrno(EINVAL)
        return 0
      }

      const seconds = Math.floor(Date.now() / 1000)
      if (timePointer) {
        writeTimeValue(timePointer, seconds)
      }
      if (outputPointer) {
        writeTimeValue(outputPointer, seconds)
        return outputPointer
      }
      return timePointer ?? 0
    },

    // clock
    clock () {
      const now =
        typeof performance !== 'undefined' &&
        typeof performance.now === 'function'
          ? performance.now()
          : Date.now()
      return Math.floor(now * (CLOCKS_PER_SEC / 1000))
    },

    clock_getres (clockId, timeSpecPointer) {
      if (!timeSpecPointer) {
        setErrno(EINVAL)
        return -1
      }

      switch (clockId) {
        case CLOCK_REALTIME:
        case CLOCK_MONOTONIC:
        case CLOCK_MONOTONIC_RAW:
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
          env.adapter.setInt32(timeSpecPointer, 0)
          env.adapter.setInt32(timeSpecPointer + 4, 1e6) // 1 millisecond
          return 0
        default:
          setErrno(EINVAL)
          return -1
      }
    },

    clock_gettime (clockId, timeSpecPointer) {
      if (!timeSpecPointer) {
        setErrno(EINVAL)
        return -1
      }

      let milliseconds
      switch (clockId) {
        case CLOCK_REALTIME:
          milliseconds = Date.now()
          break
        case CLOCK_MONOTONIC:
        case CLOCK_MONOTONIC_RAW:
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_PROCESS_CPUTIME_ID:
        case CLOCK_THREAD_CPUTIME_ID:
          if (
            typeof performance !== 'undefined' &&
            typeof performance.now === 'function'
          ) {
            milliseconds = performance.now()
          } else {
            milliseconds = Date.now()
          }
          break
        default:
          setErrno(EINVAL)
          return -1
      }

      const seconds = Math.floor(milliseconds / 1000)
      const nanoseconds = Math.floor((milliseconds - seconds * 1000) * 1e6)
      env.adapter.setInt32(timeSpecPointer, seconds)
      env.adapter.setInt32(timeSpecPointer + 4, nanoseconds)
      return 0
    },

    clock_nanosleep (
      clockId,
      flags,
      requestTimeSpecPointer,
      remainTimeSpecPointer
    ) {
      if (!requestTimeSpecPointer) {
        setErrno(EINVAL)
        return -1
      }

      if (clockId !== CLOCK_REALTIME && clockId !== CLOCK_MONOTONIC) {
        setErrno(EINVAL)
        return -1
      }

      if ((flags & ~TIMER_ABSTIME) !== 0) {
        setErrno(EINVAL)
        return -1
      }

      const { seconds, nanoseconds } = readTimespec(requestTimeSpecPointer)

      if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1e9) {
        setErrno(EINVAL)
        return -1
      }

      const payload = {
        clockId,
        flags,
        seconds,
        nanoseconds
      }

      if ((flags & TIMER_ABSTIME) !== 0) {
        let milliseconds = 0

        switch (clockId) {
          case CLOCK_REALTIME:
            milliseconds = Date.now()
            break
          case CLOCK_MONOTONIC:
            if (
              typeof performance !== 'undefined' &&
              typeof performance.now === 'function'
            ) {
              milliseconds = performance.now()
            } else {
              milliseconds = Date.now()
            }
            break
          default:
            milliseconds = 0
        }

        const currentSeconds = Math.floor(milliseconds / 1000)
        const currentNanoseconds = Math.floor(
          (milliseconds - currentSeconds * 1000) * 1e6
        )
        payload.currentSeconds = currentSeconds
        payload.currentNanoseconds = currentNanoseconds
      }

      const result = ipc.sendSync('timers.clock_nanosleep', payload)

      if (result?.err) {
        const errnoValue =
          typeof result.err.errno === 'number'
            ? result.err.errno
            : typeof result.err.code === 'number'
              ? Math.abs(result.err.code)
              : EINVAL
        setErrno(errnoValue)
        return -1
      }

      if (remainTimeSpecPointer) {
        const remainingSeconds =
          Number.parseInt(result?.data?.seconds ?? 0, 10) || 0
        const remainingNanoseconds =
          Number.parseInt(result?.data?.nanoseconds ?? 0, 10) || 0
        writeTimespec(
          remainTimeSpecPointer,
          remainingSeconds,
          remainingNanoseconds
        )
      }

      return 0
    }
  })

  // <ulimit.h>
  Object.assign(imports.env, {
    ulimit (commandValue, varargs) {
      console.warn('ulimit: Operation is not supported')
      return -1
    }
  })

  // <unistd.h>
  Object.assign(imports.env, {})

  // <uv.h>
  Object.assign(imports.env, {})

  // <oro/extension.h>
  Object.assign(imports.env, {
    // utils
    oapi_log (ctx, message) {
      const string = env.adapter.getString(message)
      console.log(string)
    },

    oapi_debug (ctx, message) {
      const string = env.adapter.getString(message)
      console.debug(string)
    },

    oapi_rand64 () {
      return crypto.rand64()
    },

    // extension
    oapi_extension_register (registrationPointer) {
      console.warn('oapi_extension_register: Operation is not supported')
    },

    oapi_extension_is_allowed (contextPointer, allowedPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.isAllowed === 'function') {
          const string = env.adapter.getString(allowedPointer)
          if (string) {
            return context.isAllowed(string)
          }
        }
      }

      return false
    },

    oapi_extension_load (contextPointer, namePointer, dataPointer) {
      console.warn('oapi_extension_load: Operation is not supported')
    },

    oapi_extension_unload (contextPointer, namePointer) {
      console.warn('oapi_extension_unload: Operation is not supported')
    },

    oapi_extension_get (contextPointer, namePointer) {
      console.warn('oapi_extension_get: Operation is not supported')
      return NULL
    },

    // context
    oapi_context_create (parentPointer, retained) {
      let parent = null

      if (parentPointer) {
        parent = env.adapter.getExternalReferenceValue(parentPointer) ?? null
      }

      const context = new WebAssemblyExtensionRuntimeContext(env.adapter, {
        context: parent,
        retained
      })

      return context.pointer
    },

    oapi_context_retain (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.retain === 'function') {
          context.retain()
          return true
        }
      }

      return false
    },

    oapi_context_retained (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'retained' in context) {
          return context.retained
        }
      }

      return false
    },

    oapi_context_get_loop (contextPointer) {
      return NULL
    },

    oapi_context_release (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.release === 'function') {
          return context.release()
        }
      }

      return false
    },

    oapi_context_set_data (contextPointer, dataPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'data' in context) {
          context.data = dataPointer
          return true
        }
      }

      return false
    },

    oapi_context_get_data (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'data' in context) {
          return context.data ?? NULL
        }
      }

      return NULL
    },

    oapi_context_dispatch (contextPointer, dataPointer, callbackPointer) {
      if (callbackPointer !== NULL) {
        setTimeout(() => {
          try {
            env.adapter.indirectFunctionTable.call(
              callbackPointer,
              contextPointer,
              dataPointer
            )
          } catch (err) {
            if (err.is(env.tags.exit)) {
              env.adapter.exitStatus = err.getArg(env.tags.exit, 0)
              env.adapter.destroy()
            } else if (err.is(env.tags.abort)) {
              env.adapter.exitStatus = 1
              env.adapter.destroy()
            } else {
              throw err
            }
          }
        })
      }
    },

    oapi_context_alloc (contextPointer, size) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.alloc === 'function') {
          return context.alloc(size)
        }
      }

      return NULL
    },

    oapi_context_get_parent (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.context?.pointer) {
          return context.context.pointer
        }
      }

      return NULL
    },

    oapi_context_error_set_code (contextPointer, code) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'error' in context) {
          if (!context.error) {
            context.error = new Error()
          }
          context.error.code = code
        }
      }
    },

    oapi_context_error_get_code (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.error) {
          return context.error.code || 0
        }
      }

      return 0
    },

    oapi_context_error_set_name (contextPointer, namePointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'error' in context) {
          const name = env.adapter.getString(namePointer)
          if (name) {
            if (!context.error) {
              context.error = new Error()
            }
            context.error.name = name
          }
        }
      }
    },

    oapi_context_error_get_name (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.error) {
          return context.error.name || NULL
        }
      }

      return NULL
    },

    oapi_context_error_set_message (contextPointer, messagePointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'error' in context) {
          const message = env.adapter.getString(messagePointer)
          if (message) {
            if (!context.error) {
              context.error = new Error()
            }
            context.error.message = message
          }
        }
      }
    },

    oapi_context_error_get_message (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.error) {
          return context.error.message || NULL
        }
      }

      return NULL
    },

    oapi_context_error_set_location (contextPointer, locationPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'error' in context) {
          const location = env.adapter.getString(locationPointer)
          if (location) {
            if (!context.error) {
              context.error = new Error()
            }
            context.error.location = location
          }
        }
      }
    },

    oapi_context_error_get_location (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.error) {
          return context.error.location || NULL
        }
      }

      return NULL
    },

    oapi_context_error_reset (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context && 'error' in context) {
          context.error = null
        }
      }
    },

    oapi_context_config_get (contextPointer, keyStringPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.config) {
          const string = env.adapter.getString(keyStringPointer)
          if (string) {
            const value = context.config[string]
            if (value) {
              return env.adapter.stack.push(value)
            }
          }
        }
      }

      return NULL
    },

    oapi_context_config_set (
      contextPointer,
      keyStringPointer,
      valueStringPointer
    ) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.config) {
          const string = env.adapter.getString(keyStringPointer)
          if (string) {
            const value = env.adapter.getString(valueStringPointer)
            if (value) {
              context.config[string] = value
            }
          }
        }
      }
    },

    // env
    oapi_env_get (contextPointer, name) {
      if (!contextPointer) {
        return NULL
      }

      return imports.env.getenv(name)
    },

    // javascript
    async oapi_javascript_evaluate (context, name, source) {
      name = env.adapter.getString(name) ?? ''
      source = env.adapter.getString(source)
      if (source) {
        // TODO(@jwerle): implement a 'vm' module and use it
        // eslint-disable-next-line
        const evaluation = new Function(`
          return function () { ${source}; }
          //# sourceURL=${name}
        `)()

        try {
          await evaluation()
        } catch (err) {
          console.error('oapi_javascript_evaluate:', err)
        }
      }
    },

    // JSON
    oapi_json_typeof (valuePointer) {
      const value = env.adapter.getExternalReferenceValue(valuePointer)
      if (value === null) {
        return OAPI_JSON_TYPE_NULL
      }

      if (Array.isArray(value)) {
        return OAPI_JSON_TYPE_ARRAY
      }

      if (typeof value === 'object') {
        return OAPI_JSON_TYPE_OBJECT
      }

      if (typeof value === 'string') {
        return OAPI_JSON_TYPE_STRING
      }

      if (typeof value === 'number') {
        return OAPI_JSON_TYPE_NUMBER
      }

      if (typeof value === 'boolean') {
        return OAPI_JSON_TYPE_BOOLEAN
      }

      return OAPI_JSON_TYPE_EMPTY
    },

    oapi_json_object_create (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.createExternalReferenceValue === 'function') {
          return context.createExternalReferenceValue({})
        }
      }

      return NULL
    },

    oapi_json_array_create (contextPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.createExternalReferenceValue === 'function') {
          return context.createExternalReferenceValue([])
        }
      }

      return NULL
    },

    oapi_json_string_create (contextPointer, stringPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context.createExternalReferenceValue === 'function') {
          const string = env.adapter.getString(stringPointer)
          if (typeof string === 'string') {
            return context.createExternalReferenceValue(string)
          }
        }
      }

      return NULL
    },

    oapi_json_boolean_create (contextPointer, booleanValue) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (context?.createExternalReferenceValue) {
          return context.createExternalReferenceValue(Boolean(booleanValue))
        }
      }

      return NULL
    },

    oapi_json_number_create (contextPointer, numberValue) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.createExternalReferenceValue === 'function') {
          return context.createExternalReferenceValue(numberValue)
        }
      }

      return NULL
    },

    oapi_json_raw_from (contextPointer, stringPointer) {
      if (contextPointer) {
        const context = env.adapter.getExternalReferenceValue(contextPointer)
        if (typeof context?.createExternalReferenceValue === 'function') {
          const string = env.adapter.getString(stringPointer)
          if (string) {
            return context.createExternalReferenceValue(string)
          }
        }
      }

      return NULL
    },

    oapi_json_parse (contextPointer, sourcePointer) {
      if (!contextPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      if (typeof context?.createExternalReferenceValue !== 'function') {
        return NULL
      }

      const source = env.adapter.getString(sourcePointer)

      if (typeof source !== 'string') {
        return NULL
      }

      try {
        const value = JSON.parse(source)
        return context.createExternalReferenceValue(value)
      } catch (err) {
        return NULL
      }
    },

    oapi_json_object_set_value (objectPointer, keyStringPointer, valuePointer) {
      const object = env.adapter.getExternalReferenceValue(objectPointer)
      if (object && typeof object === 'object') {
        const key = env.adapter.getString(keyStringPointer)
        if (key) {
          const value = env.adapter.getExternalReferenceValue(valuePointer)
          if (value !== undefined) {
            object[key] = value
          }
        }
      }
    },

    oapi_json_object_get (objectPointer, keyStringPointer) {
      const object = env.adapter.getExternalReferenceValue(objectPointer)
      if (object) {
        const key = env.adapter.getString(keyStringPointer)
        if (key && key in object) {
          const value = object[key]
          return env.adapter.getExternalReferencePointer(value) ?? NULL
        }
      }

      return NULL
    },

    oapi_json_array_set_value (arrayPointer, index, valuePointer) {
      const array = env.adapter.getExternalReferenceValue(arrayPointer)
      if (array) {
        const value = env.adapter.getExternalReferenceValue(valuePointer)
        if (value !== undefined) {
          array[index] = value
        }
      }
    },

    oapi_json_array_get (arrayPointer, index) {
      const array = env.adapter.getExternalReferenceValue(arrayPointer)
      if (array) {
        const value = array[index]
        return env.adapter.getExternalReferencePointer(value) ?? NULL
      }

      return NULL
    },

    oapi_json_array_push_value (arrayPointer, valuePointer) {
      const array = env.adapter.getExternalReferenceValue(arrayPointer)
      if (array) {
        const index = array.length
        const value = env.adapter.getExternalReferenceValue(valuePointer)
        if (value !== undefined) {
          array[index] = value
          return index
        }
      }

      return -1
    },

    oapi_json_array_pop (arrayPointer) {
      const array = env.adapter.getExternalReferenceValue(arrayPointer)
      if (array) {
        const value = array.pop()
        return env.adapter.getExternalReferencePointer(value) ?? NULL
      }

      return NULL
    },

    oapi_json_stringify_value (valuePointer) {
      const value = env.adapter.getExternalReferenceValue(valuePointer)
      if (value !== undefined) {
        return env.adapter.stack.push(JSON.stringify(value))
      }

      return NULL
    },

    oapi_ipc_message_get_index (messagePointer) {
      if (!messagePointer) {
        return -1
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.index
    },

    oapi_ipc_message_get_value (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.value.pointer
    },

    oapi_ipc_message_get_bytes (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.bytes.bufferPointer
    },

    oapi_ipc_message_get_bytes_size (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.bytes.buffer.byteLength
    },

    oapi_ipc_message_get_name (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.name.pointer
    },

    oapi_ipc_message_get_seq (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.seq.pointer
    },

    oapi_ipc_message_get_uri (messagePointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      return message.uri.pointer
    },

    oapi_ipc_message_get (messagePointer, keyStringPointer) {
      if (!messagePointer) {
        return NULL
      }

      const message = env.adapter.getExternalReferenceValue(messagePointer)
      const key = env.adapter.getString(keyStringPointer)
      const value = message.get(key)
      return value ? value.pointer : NULL
    },

    oapi_ipc_message_clone (contextPointer, messagePointer) {
      if (!contextPointer || !messagePointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const existingMessage =
        env.adapter.getExternalReferenceValue(messagePointer)
      const message = new WebAssemblyExtensionRuntimeIPCMessage(
        env.adapter,
        context,
        existingMessage.uri.value.toString()
      )

      if (
        existingMessage.bytes.pointer &&
        existingMessage.bytes.buffer.byteLength
      ) {
        message.bytes.set(
          existingMessage.bytes.pointer,
          existingMessage.bytes.buffer.byteLength
        )
      }

      return message.pointer
    },

    oapi_ipc_result_create (contextPointer, messagePointer) {
      if (!contextPointer || !messagePointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const message = env.adapter.getExternalReferenceValue(messagePointer)
      const result = new WebAssemblyExtensionRuntimeIPCResult(
        env.adapter,
        context,
        message
      )

      return result.pointer
    },

    oapi_ipc_result_clone (contextPointer, resultPointer) {
      if (!contextPointer || !resultPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const existingResult =
        env.adapter.getExternalReferenceValue(resultPointer)
      const result = new WebAssemblyExtensionRuntimeIPCResult(
        env.adapter,
        context,
        existingResult.message,
        existingResult
      )

      return result.pointer
    },

    oapi_ipc_result_set_seq (resultPointer, seqPointer) {
      if (!resultPointer || !seqPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.seq = env.adapter.getString(seqPointer)
    },

    oapi_ipc_result_get_seq (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      if (result.seq) {
        return env.adapter.stack.push(result.seq)
      }

      return NULL
    },

    oapi_ipc_result_get_context (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result?.context?.pointer ?? NULL
    },

    oapi_ipc_result_set_message (resultPointer, messagePointer) {
      if (!resultPointer || !messagePointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.message = env.adapter.getExternalReferenceValue(messagePointer)
    },

    oapi_ipc_result_get_message (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.message?.pointer ?? NULL
    },

    oapi_ipc_result_set_json (resultPointer, jsonPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.json = env.adapter.getExternalReferenceValue(jsonPointer)
    },

    oapi_ipc_result_get_json (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.json.pointer
    },

    oapi_ipc_result_set_json_data (resultPointer, jsonDataPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.data = env.adapter.getExternalReferenceValue(jsonDataPointer)
    },

    oapi_ipc_result_get_json_data (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.json.data.pointer
    },

    oapi_ipc_result_set_json_error (resultPointer, jsonErrorPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.error = env.adapter.getExternalReferenceValue(jsonErrorPointer)
    },

    oapi_ipc_result_get_json_error (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.json.err.pointer
    },

    oapi_ipc_result_set_bytes (resultPointer, size, bytesPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.bytes.set(bytesPointer, size)
    },

    oapi_ipc_result_get_bytes (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.bytes.bufferPointer
    },

    oapi_ipc_result_get_bytes_size (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      return result.bytes.buffer.byteLength
    },

    oapi_ipc_result_set_header (resultPointer, namePointer, valuePointer) {
      if (!resultPointer || !namePointer) {
        return
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      const name = env.adapter.getString(namePointer)
      const value = env.adapter.getString(valuePointer)

      result.headers.set(name, value)
    },

    oapi_ipc_result_get_header (resultPointer, namePointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      const name = env.adapter.getString(namePointer)
      return result.headers.getValuePointer(name)
    },

    oapi_ipc_result_from_json (contextPointer, messagePointer, jsonPointer) {
      if (!contextPointer || !messagePointer || !jsonPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const message = env.adapter.getExternalReferenceValue(messagePointer)
      const result = new WebAssemblyExtensionRuntimeIPCResult(
        env.adapter,
        context,
        message
      )

      result.json = env.adapter.getExternalReferenceValue(jsonPointer)
      return result.pointer
    },

    oapi_ipc_send_chunk (resultPointer, chunkPointer, chunkSize, finished) {
      console.warn('oapi_ipc_send_chunk: Operation is not supported')
      return false
    },

    oapi_ipc_send_event (resultPointer, namePointer, dataPointer, finished) {
      console.warn('oapi_ipc_send_event: Operation is not supported')
      return false
    },

    oapi_ipc_set_cancellation_handler (
      resultPointer,
      handlerPointer,
      dataPointer
    ) {
      console.warn(
        'oapi_ipc_set_cancellation_handler: Operation is not supported'
      )
      return false
    },

    oapi_ipc_reply (resultPointer) {
      if (!resultPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.reply()
      result.context.free(result.pointer)
    },

    oapi_ipc_reply_with_error (resultPointer, jsonErrorPointer) {
      if (!resultPointer || !jsonErrorPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      result.error = env.adapter.getExternalReferenceValue(jsonErrorPointer)
      result.reply()
      result.context.free(result.pointer)
    },

    oapi_ipc_send_json (contextPointer, messagePointer, jsonPointer) {
      if (!contextPointer || !messagePointer || !jsonPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const message = env.adapter.getExternalReferenceValue(messagePointer)
      const result = new WebAssemblyExtensionRuntimeIPCResult(
        env.adapter,
        context,
        message
      )

      result.json = env.adapter.getExternalReferenceValue(jsonPointer)

      if (result.seq === '-1') {
        if (result.err) {
          globalThis.dispatchEvent(
            new ErrorEvent(result.err?.type || 'error', { error: result.err })
          )
        } else {
          const { data, id } = result
          const headers = result.headers.value
          const params = {}
          const detail = { headers, params, data, id }
          globalThis.dispatchEvent(new CustomEvent('data', { detail }))
        }
      } else {
        const index = application.getCurrentWindowIndex()
        const eventName = `resolve-${index}-${result.seq}`
        const event = new CustomEvent(eventName, { detail: result.json })
        globalThis.dispatchEvent(event)
      }
    },

    oapi_ipc_send_json_with_result (contextPointer, resultPointer, jsonPointer) {
      if (!contextPointer || !resultPointer || !jsonPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const result = env.adapter.getExternalReferenceValue(resultPointer)
      const json = env.adapter.getExternalReferenceValue(jsonPointer)
      if (context && result && json) {
        result.json = json
      }

      if (result.seq === '-1') {
        if (result.err) {
          globalThis.dispatchEvent(
            new ErrorEvent(result.err?.type || 'error', { error: result.err })
          )
        } else {
          const { data, id } = result
          const headers = result.headers.value
          const params = {}
          const detail = { headers, params, data, id }
          globalThis.dispatchEvent(new CustomEvent('data', { detail }))
        }
      } else {
        const index = application.getCurrentWindowIndex()
        const eventName = `resolve-${index}-${result.seq}`
        const event = new CustomEvent(eventName, { detail: result.json })
        globalThis.dispatchEvent(event)
      }
    },

    oapi_ipc_send_bytes (
      contextPointer,
      messagePointer,
      size,
      bytesPointer,
      headersPointer
    ) {
      if (!contextPointer || !messagePointer || !bytesPointer) {
        return NULL
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)
      const message = env.adapter.getExternalReferenceValue(messagePointer)
      const result = new WebAssemblyExtensionRuntimeIPCResult(
        env.adapter,
        context,
        message
      )

      const headers = env.adapter.getString(headersPointer)

      if (headers) {
        const lines = headers.split('\n')
        for (const line of lines) {
          const entry = line.trim().split(':')
          result.headers.set(entry[0], entry[1])
        }
      }

      result.bytes.set(bytesPointer, size)

      if (result.seq === '-1') {
        const { id } = result
        const data = result.bytes.buffer
        const headers = result.headers.value
        const params = {}
        const detail = { headers, params, data, id }
        globalThis.dispatchEvent(new CustomEvent('data', { detail }))
      } else {
        const index = application.getCurrentWindowIndex()
        const eventName = `resolve-${index}-${result.seq}`
        const event = new CustomEvent(eventName, {
          detail: { data: result.bytes.buffer }
        })
        globalThis.dispatchEvent(event)
      }
    },

    oapi_ipc_send_bytes_with_result (
      contextPointer,
      resultPointer,
      size,
      bytesPointer,
      headersPointer
    ) {
      if (!contextPointer || !resultPointer || !bytesPointer) {
        return NULL
      }

      const result = env.adapter.getExternalReferenceValue(resultPointer)
      const headers = env.adapter.getString(headersPointer)

      if (headers) {
        const lines = headers.split('\n')
        for (const line of lines) {
          const entry = line.trim().split(':')
          result.headers.set(entry[0], entry[1])
        }
      }

      result.bytes.set(bytesPointer, size)

      if (result.seq === '-1') {
        const { id } = result
        const data = result.bytes.buffer
        const headers = result.headers.value
        const params = {}
        const detail = { headers, params, data, id }
        globalThis.dispatchEvent(new CustomEvent('data', { detail }))
      } else {
        const index = application.getCurrentWindowIndex()
        const eventName = `resolve-${index}-${result.seq}`
        const event = new CustomEvent(eventName, {
          detail: { data: result.bytes.buffer }
        })
        globalThis.dispatchEvent(event)
      }
    },

    oapi_ipc_emit (contextPointer, namePointer, dataPointer) {
      if (!contextPointer || !namePointer) {
        return NULL
      }

      const eventName = env.adapter.getString(namePointer)
      const data = dataPointer ? env.adapter.getString(dataPointer) : null
      let json = null

      if (data) {
        try {
          json = JSON.parse(data)
        } catch {}
      }

      const event = new CustomEvent(eventName, { detail: json })
      globalThis.dispatchEvent(event)
    },

    oapi_ipc_invoke (
      contextPointer,
      urlPointer,
      size,
      bytesPointer,
      callbackPointer
    ) {
      if (!contextPointer || !urlPointer || !callbackPointer) {
        return false
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)

      if (!context.router) {
        return false
      }

      const url = env.adapter.getString(urlPointer)
      const bytes = env.adapter.get(bytesPointer)
      return context.router.invoke(context, url, size, bytes, (result) => {
        env.adapter.indirectFunctionTable.call(
          callbackPointer,
          result.pointer,
          context.router.pointer
        )
      })
    },

    oapi_ipc_router_map (
      contextPointer,
      routePointer,
      callbackPointer,
      dataPointer
    ) {
      if (!contextPointer || !routePointer || !callbackPointer) {
        return false
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)

      if (!context.router) {
        return false
      }

      const route = env.adapter.getString(routePointer)
      context.router.map(route, (context, message) => {
        env.adapter.indirectFunctionTable.call(
          callbackPointer,
          context.pointer,
          message.pointer,
          context.router.pointer
        )
      })

      return true
    },

    oapi_ipc_router_unmap (contextPointer, routePointer) {
      if (!contextPointer || !routePointer) {
        return false
      }

      const context = env.adapter.getExternalReferenceValue(contextPointer)

      if (!context.router) {
        return false
      }

      const route = env.adapter.getString(routePointer)
      return context.router.unmap(route)
    },

    oapi_ipc_router_listen (
      contextPointer,
      routePointer,
      callbackPointer,
      dataPointer
    ) {
      console.warn('oapi_ipc_router_listen: Operation is not supported')
      return false
    },

    oapi_ipc_router_unlisten (contextPointer, routePointer, token) {
      console.warn('oapi_ipc_router_unlisten: Operation is not supported')
      return false
    },

    oapi_process_exec (contextPointer, commandPointer) {
      console.warn('oapi_process_exec: Operation is not supported')
      return NULL
    },

    oapi_process_exec_get_exit_code (processPointer) {
      console.warn(
        'oapi_process_exec_get_exit_code: Operation is not supported'
      )
      return -1
    },

    oapi_process_exec_get_output (processPointer) {
      console.warn('oapi_process_exec_get_output: Operation is not supported')
      return NULL
    },

    oapi_process_spawn (
      contextPointer,
      commandPointer,
      argvPointer,
      pathPointer,
      onstdoutPointer,
      onstderrPointer,
      onexitPointer
    ) {
      console.warn('oapi_process_spawn: Operation is not supported')
      return NULL
    },

    oapi_process_spawn_get_exit_code (processPointer) {
      console.warn(
        'oapi_process_spawn_get_exit_code: Operation is not supported'
      )
      return -1
    },

    oapi_process_spawn_get_pid (processPointer) {
      console.warn('oapi_process_spawn_get_pid: Operation is not supported')
      return -1
    },

    oapi_process_spawn_get_context (processPointer) {
      console.warn('oapi_process_spawn_get_context: Operation is not supported')
      return NULL
    },

    oapi_process_spawn_wait (processPointer) {
      console.warn('oapi_process_spawn_wait: Operation is not supported')
      return -1
    },

    oapi_process_spawn_write (processPointer, bytesPointer, size) {
      console.warn('oapi_process_spawn_write: Operation is not supported')
      return false
    },

    oapi_process_spawn_close_stdin (processPointer) {
      console.warn('oapi_process_spawn_close_stdin: Operation is not supported')
      return false
    },

    oapi_process_spawn_kill (processPointer, code) {
      console.warn('oapi_process_spawn_kill: Operation is not supported')
      return false
    }
  })

  return imports
}

/**
 * @typedef {{
 *   allow: string[] | string,
 *   imports?: object,
 *   type?: 'shared' | 'wasm32',
 *   path?: string,
 *   stats?: object,
 *   instance?: WebAssembly.Instance,
 *   adapter?: WebAssemblyExtensionAdapter
 * }} ExtensionLoadOptions
 */

/**
 * @typedef {{ abi: number, version: string, description: string }} ExtensionInfo
 */

/**
 * @typedef {{ abi: number, loaded: number }} ExtensionStats
 */

/**
 * A interface for a native extension.
 * @template {Record<string, any> T}
 */
export class Extension extends EventTarget {
  /**
   * Load an extension by name.
   * @template {Record<string, any> T}
   * @param {string} name
   * @param {ExtensionLoadOptions} [options]
   * @return {Promise<Extension<T>>}
   */
  static async load (name, options) {
    options = { name, ...options }

    if (Array.isArray(options.allow)) {
      options.allow = options.allow.join(',')
    }

    options.type = options.type ?? (await this.type(name))
    const stats = options.stats ?? (await this.stats(name))

    let info = null

    if (options.type === 'wasm32') {
      console.warn(
        'oro:extension: wasm32 extensions are highly experimental. ' +
          'The ABI is unstable and APIs may change or be incomplete'
      )

      let adapter = null
      let stream = null
      let path = null

      path = options.path ?? stats.path
      if (path.startsWith('http')) {
        const response = await fetch(path)
        stream = response.stream()
      } else {
        path = path.startsWith('/') ? path.slice(1) : path
        const fd = await fs.open(path)
        const file = await createFile(path, { fd })
        stream = file.stream()
      }
      const response = new WebAssemblyResponse(stream)
      const table = new WebAssembly.Table({
        initial: 0,
        element: 'anyfunc'
      })

      const tags = {
        ...options.tags,
        exit: new WebAssembly.Tag({ parameters: ['i32'] }),
        abort: new WebAssembly.Tag({ parameters: [] })
      }

      const memory = options.memory ?? new WebAssembly.Memory({ initial: 32 })
      const imports = {
        ...createWebAssemblyExtensionImports({
          tags,
          table,
          memory,
          get adapter () {
            return adapter
          }
        }),

        ...options.imports
      }

      const { instance, module } = await WebAssembly.instantiateStreaming(
        response,
        imports
      )

      adapter = new WebAssemblyExtensionAdapter({
        policies: options.allowed || [],
        instance,
        memory,
        module,
        table
      })

      info = new WebAssemblyExtensionInfo(adapter)

      options.adapter = adapter
      options.instance = instance

      try {
        adapter.init()
      } catch (err) {
        if (typeof err?.is === 'function') {
          if (err.is(tags.exit)) {
            adapter.exitStatus = err.getArg(tags.exit, 0)
            adapter.destroy()
          } else if (err.is(tags.abort)) {
            adapter.exitStatus = 1
            adapter.destroy()
          } else {
            throw err
          }
        } else {
          throw err
        }
      }
    } else {
      const result = await ipc.request('extension.load', options)
      if (result.err) {
        throw new Error('Failed to load extensions', { cause: result.err })
      }

      // APK libraries may be absent from the resource filesystem used by type().
      // A successful native load establishes the type required for unloading.
      options.type = 'shared'
      info = result.data
    }

    const extension = new this(name, info, options)
    extension[$stats] = stats
    extension[$loaded] = true
    return extension
  }

  /**
   * Query type of extension by name.
   * @param {string} name
   * @return {Promise<'shared'|'wasm32'|'unknown'|null>}
   */
  static async type (name) {
    const result = await ipc.request('extension.type', { name })

    if (result.err) {
      throw result.err
    }

    return result.data.type ?? null
  }

  /**
   * Provides current stats about the loaded extensions or one by name.
   * @param {?string} name
   * @return {Promise<ExtensionStats|null>}
   */
  static async stats (name) {
    const result = await ipc.request('extension.stats', { name: name || '' })
    if (result.err) {
      throw result.err
    }
    return result.data ?? null
  }

  /**
   * The name of the extension
   * @type {string?}
   */
  name = null

  /**
   * The version of the extension
   * @type {string?}
   */
  version = null

  /**
   * The description of the extension
   * @type {string?}
   */
  description = null

  /**
   * The abi of the extension
   * @type {number}
   */
  abi = 0

  /**
   * @type {object}
   */
  options = {}

  /**
   * @type {T}
   */
  binding = null

  /**
   * Not `null` if extension is of type 'wasm32'
   * @type {?WebAssemblyExtensionAdapter}
   */
  adapter = null

  /**
   * `Extension` class constructor.
   * @param {string} name
   * @param {ExtensionInfo} info
   * @param {ExtensionLoadOptions} [options]
   */
  constructor (name, info, options = null) {
    super()

    this[$type] = options?.type || 'shared'
    this[$loaded] = false

    this.abi = info?.abi || 0
    this.name = name
    this.version = info?.version || null
    this.description = info?.description || null
    this.options = options ?? {}

    if (this.type === 'shared') {
      this.binding = ipc.createBinding(options?.binding?.name || this.name, {
        ...options?.binding,
        extension: this,
        default: 'request'
      })
    } else if (this.type === 'wasm32') {
      this.adapter = options?.adapter
      this.binding = createWebAssemblyExtensionBinding(this, this.adapter)
    }
  }

  /**
   * `true` if the extension was loaded, otherwise `false`
   * @type {boolean}
   */
  get loaded () {
    return this[$loaded]
  }

  /**
   * The extension type: 'shared' or  'wasm32'
   * @type {'shared'|'wasm32'}
   */
  get type () {
    return this[$type]
  }

  /**
   * Unloads the loaded extension.
   * @throws Error
   */
  async unload () {
    if (!this.loaded) {
      throw new Error('Extension is not loaded')
    }

    if (this.type === 'shared') {
      const { name } = this
      const result = await ipc.request('extension.unload', { name })

      if (result.err) {
        throw new Error('Failed to unload extension', { cause: result.err })
      }
    } else if (this.type === 'wasm32') {
      // remove references
      this.instance = null
      this.adapter = null
      this.binding = null
    }

    this[$loaded] = false
    this.dispatchEvent(new Event('unload'))

    return true
  }
}

/**
 * Load an extension by name.
 * @template {Record<string, any> T}
 * @param {string} name
 * @param {ExtensionLoadOptions} [options]
 * @return {Promise<Extension<T>>}
 */
export async function load (name, options = {}) {
  return await Extension.load(name, options)
}

/**
 * Provides current stats about the loaded extensions.
 * @return {Promise<ExtensionStats>}
 */
export async function stats () {
  return await Extension.stats()
}

export default {
  load,
  stats
}
