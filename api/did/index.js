const METHOD_NAME_PATTERN = /^[a-z0-9]+$/
const ID_SEGMENT_PATTERN = '(?:[A-Za-z0-9._-]|%[0-9A-Fa-f]{2})+'
const METHOD_SPECIFIC_ID_PATTERN = new RegExp(
  `^${ID_SEGMENT_PATTERN}(?::${ID_SEGMENT_PATTERN})*$`
)
const DID_PATTERN = new RegExp(
  `^did:([a-z0-9]+):(${ID_SEGMENT_PATTERN}(?::${ID_SEGMENT_PATTERN})*)$`
)
const PARAM_CHAR_PATTERN = /^[A-Za-z0-9._:-]+$/
const PARAM_VALUE_PATTERN = /^(?:[A-Za-z0-9._:-]|%[0-9A-Fa-f]{2})*$/
const CORE_CONTEXT = 'https://www.w3.org/ns/did/v1'
const CORE_VERIFICATION_RELATIONSHIPS = [
  'authentication',
  'assertionMethod',
  'keyAgreement',
  'capabilityInvocation',
  'capabilityDelegation'
]
const STANDARD_PARAMETERS = [
  'service',
  'relativeRef',
  'versionId',
  'versionTime',
  'hl'
]
const CORE_DOCUMENT_PROPERTIES = [
  'id',
  '@context',
  'alsoKnownAs',
  'controller',
  'verificationMethod',
  ...CORE_VERIFICATION_RELATIONSHIPS,
  'service'
]

const TEXT_ENCODER =
  typeof TextEncoder === 'function' ? new TextEncoder() : null
const TEXT_DECODER =
  typeof TextDecoder === 'function' ? new TextDecoder() : null
const ABSOLUTE_URI_PATTERN = /^[A-Za-z][A-Za-z0-9+.-]*:/

function structuredCloneJSON (value) {
  if (typeof globalThis.structuredClone === 'function') {
    return globalThis.structuredClone(value)
  }
  return value == null ? value : JSON.parse(JSON.stringify(value))
}

const READ_ONLY_ERROR = {
  map: 'Cannot modify frozen DIDURL parameters',
  set: 'Cannot modify frozen DIDURL parameter sets'
}

function freezeDeep (value, seen = new WeakSet()) {
  if (!value || typeof value !== 'object') {
    return value
  }
  if (seen.has(value)) {
    return value
  }
  seen.add(value)

  if (value instanceof Map) {
    const updates = []
    for (const [key, entry] of value.entries()) {
      const frozenEntry = freezeDeep(entry, seen)
      if (frozenEntry !== entry) {
        updates.push([key, frozenEntry])
      }
    }
    if (updates.length) {
      for (const [key, frozenEntry] of updates) {
        value.set(key, frozenEntry)
      }
    }
    return freezeMapInstance(value)
  }

  if (value instanceof Set) {
    const frozenEntries = []
    let changed = false
    for (const entry of value.values()) {
      const frozenEntry = freezeDeep(entry, seen)
      frozenEntries.push(frozenEntry)
      if (frozenEntry !== entry) {
        changed = true
      }
    }
    if (changed) {
      value.clear()
      for (const entry of frozenEntries) {
        value.add(entry)
      }
    }
    return freezeSetInstance(value)
  }

  if (Array.isArray(value)) {
    for (let index = 0; index < value.length; index++) {
      const frozenItem = freezeDeep(value[index], seen)
      if (frozenItem !== value[index]) {
        value[index] = frozenItem
      }
    }
    return Object.freeze(value)
  }

  for (const key of Object.keys(value)) {
    const frozenChild = freezeDeep(value[key], seen)
    if (frozenChild !== value[key]) {
      value[key] = frozenChild
    }
  }
  return Object.freeze(value)
}

function freezeMapInstance (map) {
  Object.freeze(map)
  return createReadOnlyProxy(
    map,
    ['set', 'delete', 'clear'],
    READ_ONLY_ERROR.map
  )
}

function freezeSetInstance (set) {
  Object.freeze(set)
  return createReadOnlyProxy(
    set,
    ['add', 'delete', 'clear'],
    READ_ONLY_ERROR.set
  )
}

function createReadOnlyProxy (target, mutatorNames, label = 'collection') {
  const mutators = new Set(mutatorNames)
  return new Proxy(target, {
    get (obj, prop, receiver) {
      if (mutators.has(prop)) {
        return () => {
          throw new TypeError(label)
        }
      }
      const value = Reflect.get(obj, prop, receiver)
      if (typeof value === 'function') {
        return value.bind(obj)
      }
      return value
    }
  })
}

function normalizeMethodSpecificId (methodSpecificId) {
  return methodSpecificId
    .split(':')
    .map((segment) => {
      return segment.replace(/%[0-9a-fA-F]{2}/g, (match) => match.toUpperCase())
    })
    .join(':')
}

function isAbsoluteUri (value) {
  return typeof value === 'string' && ABSOLUTE_URI_PATTERN.test(value)
}

function parseMethodSpecificId (methodSpecificId) {
  if (!METHOD_SPECIFIC_ID_PATTERN.test(methodSpecificId)) {
    throw new DIDError(
      'invalid_method_specific_id',
      `Invalid method-specific-id: ${methodSpecificId}`
    )
  }
  return normalizeMethodSpecificId(methodSpecificId)
}

function normalizeMethodName (method) {
  if (!METHOD_NAME_PATTERN.test(method)) {
    throw new DIDError('invalid_method', `Invalid DID method name: ${method}`)
  }
  return method.toLowerCase()
}

function encodeParamValue (value) {
  return encodeURIComponent(value)
    .replace(/%5B/g, '[')
    .replace(/%5D/g, ']')
    .replace(/%3A/g, ':')
}

function ensureArray (value) {
  if (value == null) return []
  return Array.isArray(value) ? value : [value]
}

function isPlainObject (value) {
  return value != null && Object.getPrototypeOf(value) === Object.prototype
}

export class DIDError extends Error {
  constructor (code, message, details = null) {
    super(message)
    this.name = 'DIDError'
    this.code = code
    if (details) {
      this.details = details
    }
  }
}

export class DID {
  constructor (input) {
    const parsed = parseDIDLike(input)
    this.method = parsed.method
    this.methodSpecificId = parsed.methodSpecificId
    this.idSegments = Object.freeze([...parsed.idSegments])
    this.href = `did:${this.method}:${this.methodSpecificId}`
    Object.freeze(this)
  }

  toString () {
    return this.href
  }

  toJSON () {
    return this.href
  }

  equals (other) {
    const did = parseDIDLike(other)
    return (
      this.method === did.method &&
      this.methodSpecificId === did.methodSpecificId
    )
  }

  static parse (input) {
    return new DID(input)
  }

  static from (input) {
    return input instanceof DID ? input : new DID(input)
  }

  static normalize (input) {
    return DID.from(input).toString()
  }
}

export class DIDURL extends DID {
  constructor (input, init = null) {
    const parsed = parseDIDUrlLike(input)
    super(parsed.did)
    this.parameters = freezeDeep(parsed.parameters)
    this.path = parsed.path
    this.query = parsed.query
    this.fragment = parsed.fragment
    this.queryParams = parsed.queryParams
    if (init && typeof init === 'object') {
      applyUrlInit(this, init)
    } else {
      Object.freeze(this)
    }
  }

  withParameter (name, value) {
    const parameters = cloneParameters(this.parameters)
    setParameter(parameters, name, value)
    return DIDURL.from({
      did: super.toString(),
      parameters,
      path: this.path,
      query: this.query,
      fragment: this.fragment
    })
  }

  withoutParameter (name) {
    const parameters = cloneParameters(this.parameters)
    parameters.delete(name)
    return DIDURL.from({
      did: super.toString(),
      parameters,
      path: this.path,
      query: this.query,
      fragment: this.fragment
    })
  }

  withQuery (query) {
    return DIDURL.from({
      did: super.toString(),
      parameters: this.parameters,
      path: this.path,
      query,
      fragment: this.fragment
    })
  }

  withFragment (fragment) {
    return DIDURL.from({
      did: super.toString(),
      parameters: this.parameters,
      path: this.path,
      query: this.query,
      fragment
    })
  }

  toString () {
    return formatDIDUrl(this)
  }

  toJSON () {
    return {
      did: super.toString(),
      method: this.method,
      methodSpecificId: this.methodSpecificId,
      parameters: Object.fromEntries(this.parameters),
      path: this.path,
      query: this.query,
      fragment: this.fragment,
      href: this.toString()
    }
  }

  static parse (input) {
    return new DIDURL(input)
  }

  static from (input) {
    if (input instanceof DIDURL) return input
    if (input instanceof DID) return new DIDURL(input.toString())
    if (typeof input === 'string') return new DIDURL(input)
    return new DIDURL(formatDIDUrl(input))
  }
}

function applyUrlInit (didUrl, init) {
  const parameters = cloneParameters(didUrl.parameters)
  if (init.parameters) {
    parameters.clear()
    const entries =
      init.parameters instanceof Map
        ? init.parameters.entries()
        : Object.entries(init.parameters)
    for (const [name, values] of entries) {
      setParameter(parameters, name, values)
    }
  }

  const next = {
    did: didUrl.href,
    parameters,
    path: init.path ?? didUrl.path,
    query: init.query ?? didUrl.query,
    fragment: init.fragment ?? didUrl.fragment
  }

  const rebuilt = parseDIDUrlLike(next)
  didUrl.parameters = freezeDeep(rebuilt.parameters)
  didUrl.path = rebuilt.path
  didUrl.query = rebuilt.query
  didUrl.fragment = rebuilt.fragment
  didUrl.queryParams = rebuilt.queryParams
  Object.freeze(didUrl)
}

function cloneParameters (parameters) {
  const out = new Map()
  for (const [name, list] of parameters.entries()) {
    out.set(name, [...list])
  }
  return out
}

function setParameter (parameters, name, value) {
  const normalizedName = normalizeParameterName(name)
  const list = []
  const values = ensureArray(value)
  for (const item of values) {
    if (item == null) continue
    const stringValue = String(item)
    list.push(normalizeParameterValue(stringValue, normalizedName))
  }
  if (list.length === 0) {
    parameters.delete(normalizedName)
  } else {
    parameters.set(normalizedName, list)
  }
}

function normalizeParameterName (name) {
  if (typeof name !== 'string' || name.length === 0) {
    throw new DIDError(
      'invalid_parameter_name',
      'DID parameter name must be a non-empty string'
    )
  }
  if (!PARAM_CHAR_PATTERN.test(name)) {
    throw new DIDError(
      'invalid_parameter_name',
      `Invalid DID parameter name: ${name}`
    )
  }
  return name
}

function normalizeParameterValue (value, name) {
  if (!PARAM_VALUE_PATTERN.test(value)) {
    throw new DIDError(
      'invalid_parameter_value',
      `Invalid DID parameter value for ${name}: ${value}`
    )
  }
  try {
    return decodeURIComponent(value)
  } catch {
    throw new DIDError(
      'invalid_parameter_value',
      `Invalid percent encoding in parameter ${name}`
    )
  }
}

function parseDIDLike (input) {
  if (input instanceof DID) {
    return {
      method: input.method,
      methodSpecificId: input.methodSpecificId,
      idSegments: input.idSegments
    }
  }

  if (input instanceof DIDURL) {
    return parseDIDLike(input.href)
  }

  if (typeof input !== 'string') {
    throw new DIDError('invalid_did', 'DID must be a string')
  }

  const match = input.match(DID_PATTERN)
  if (!match) {
    throw new DIDError('invalid_did', `Invalid DID: ${input}`)
  }

  const method = normalizeMethodName(match[1])
  const methodSpecificId = parseMethodSpecificId(match[2])
  const idSegments = methodSpecificId.split(':')

  return { method, methodSpecificId, idSegments }
}

function parseDIDUrlLike (input) {
  if (input instanceof DIDURL) {
    return {
      did: input.toString(),
      parameters: cloneParameters(input.parameters),
      path: input.path,
      query: input.query,
      fragment: input.fragment,
      queryParams: new URLSearchParams(input.query ?? '')
    }
  }

  if (input instanceof DID) {
    input = input.toString()
  }

  if (input && typeof input === 'object') {
    const rawDid =
      input.did ??
      input.href ??
      (input.method && input.methodSpecificId
        ? `did:${input.method}:${input.methodSpecificId}`
        : null)
    if (!rawDid) {
      throw new DIDError(
        'invalid_did_url',
        'Object form requires did, href, or method/methodSpecificId'
      )
    }
    const parsedDid = parseDIDLike(rawDid)
    return {
      did: formatDID(parsedDid),
      parameters: toParameterMap(input.parameters ?? new Map()),
      path: input.path ?? null,
      query: input.query ?? null,
      fragment: input.fragment ?? null,
      queryParams: new URLSearchParams(input.query ?? '')
    }
  }

  if (typeof input !== 'string') {
    throw new DIDError(
      'invalid_did_url',
      'DID URL must be a string or iterable components'
    )
  }

  const { did, rest } = splitBeforePath(input)
  const parsedDid = parseDIDLike(did)
  const [parameters, remainder] = extractParameters(rest)
  const { path, query, fragment } = extractUrlSuffix(remainder)

  return {
    did: formatDID(parsedDid),
    parameters,
    path,
    query,
    fragment,
    queryParams: new URLSearchParams(query ?? '')
  }
}

function formatDID (parts) {
  return `did:${parts.method}:${parts.methodSpecificId}`
}

function splitBeforePath (input) {
  const didIndex = input.indexOf(':', 4)
  if (didIndex < 0) {
    throw new DIDError(
      'invalid_did_url',
      `Unable to determine DID portion of ${input}`
    )
  }
  const restIndex = input.indexOf('/', didIndex + 1)
  const queryIndex = input.indexOf('?', didIndex + 1)
  const fragmentIndex = input.indexOf('#', didIndex + 1)
  let boundary = input.length
  for (const index of [restIndex, queryIndex, fragmentIndex]) {
    if (index !== -1 && index < boundary) boundary = index
  }
  const did = input.slice(0, boundary)
  const rest = input.slice(boundary)
  return { did, rest }
}

function extractParameters (rest) {
  if (!rest.startsWith(';')) {
    return [new Map(), rest]
  }
  const params = new Map()
  let index = 1
  let buffer = ''
  const flush = () => {
    if (!buffer) return
    const [name, ...val] = buffer.split('=')
    setParameter(params, name, val.length ? val.join('=') : '')
    buffer = ''
  }
  for (; index < rest.length; index++) {
    const char = rest[index]
    if (char === ';') {
      flush()
    } else if (char === '/' || char === '?' || char === '#') {
      flush()
      return [params, rest.slice(index)]
    } else {
      buffer += char
    }
  }
  flush()
  return [params, '']
}

function extractUrlSuffix (input) {
  if (!input) return { path: null, query: null, fragment: null }
  let path = null
  let query = null
  let fragment = null
  let rest = input

  if (rest.startsWith('/')) {
    const queryIndex = rest.indexOf('?')
    const fragmentIndex = rest.indexOf('#')
    let end = rest.length
    if (queryIndex !== -1) end = queryIndex
    if (fragmentIndex !== -1 && fragmentIndex < end) end = fragmentIndex
    path = rest.slice(0, end) || null
    rest = rest.slice(end)
  }

  if (rest.startsWith('?')) {
    const fragmentIndex = rest.indexOf('#')
    if (fragmentIndex === -1) {
      query = rest.slice(1) || null
      rest = ''
    } else {
      query = rest.slice(1, fragmentIndex) || null
      rest = rest.slice(fragmentIndex)
    }
  }

  if (rest.startsWith('#')) {
    fragment = rest.slice(1) || null
  }

  return { path, query, fragment }
}

function formatDIDUrl (components) {
  const did = formatDID(parseDIDLike(components.did ?? components))
  const parameters =
    components.parameters instanceof Map
      ? components.parameters
      : toParameterMap(components.parameters ?? new Map())
  const segments = [did]
  for (const [name, values] of parameters.entries()) {
    for (const value of values) {
      segments.push(
        `;${name}${value === '' ? '' : `=${encodeParamValue(value)}`}`
      )
    }
  }
  if (components.path) segments.push(components.path)
  if (components.query != null) segments.push(`?${components.query}`)
  if (components.fragment != null) segments.push(`#${components.fragment}`)
  return segments.join('')
}

function toParameterMap (value) {
  const map = new Map()
  if (value instanceof Map) {
    for (const [name, val] of value.entries()) {
      setParameter(map, name, val)
    }
    return map
  }
  if (value && typeof value === 'object') {
    for (const [name, val] of Object.entries(value)) {
      setParameter(map, name, val)
    }
  }
  return map
}

function normalizeContexts (contexts) {
  const list = ensureArray(contexts)
  if (list.length === 0) {
    throw new DIDError(
      'invalid_document',
      'DID document must include an @context value'
    )
  }
  const normalized = list.map((value) => value)
  if (!normalized.includes(CORE_CONTEXT)) {
    normalized.unshift(CORE_CONTEXT)
  }
  return freezeDeep(normalized)
}

function validateControllers (value, errors, path) {
  if (value == null) return null
  const list = ensureArray(value)
  const normalized = []
  for (const entry of list) {
    try {
      normalized.push(DID.normalize(entry))
      continue
    } catch {}
    try {
      normalized.push(DIDURL.parse(entry).toString())
      continue
    } catch (error) {
      errors.push({ path, code: 'invalid_controller', message: error.message })
    }
  }
  return freezeDeep(normalized)
}

function validateAlsoKnownAs (value, errors) {
  if (value == null) return null
  if (!Array.isArray(value)) {
    errors.push({
      path: 'alsoKnownAs',
      code: 'invalid_alsoKnownAs',
      message: 'alsoKnownAs MUST be an array of URIs'
    })
    return null
  }
  const uris = []
  for (const entry of value) {
    if (
      typeof entry !== 'string' ||
      entry.length === 0 ||
      !isAbsoluteUri(entry)
    ) {
      errors.push({
        path: 'alsoKnownAs',
        code: 'invalid_uri',
        message: `Invalid URI in alsoKnownAs: ${entry}`
      })
      continue
    }
    uris.push(entry)
  }
  return freezeDeep(uris)
}

function ensureUniqueIds (objects, errors, path) {
  const seen = new Set()
  for (const item of objects) {
    if (!item?.id) continue
    if (seen.has(item.id)) {
      errors.push({
        path,
        code: 'duplicate_id',
        message: `Duplicate identifier detected: ${item.id}`
      })
    }
    seen.add(item.id)
  }
}

function normalizeVerificationMethod (value, documentId, errors, path) {
  if (!isPlainObject(value)) {
    errors.push({
      path,
      code: 'invalid_verification_method',
      message: 'Verification method must be an object'
    })
    return null
  }
  const entry = structuredCloneJSON(value)
  if (typeof entry.id !== 'string') {
    errors.push({
      path,
      code: 'invalid_verification_method_id',
      message: 'Verification method must include an id'
    })
  } else {
    try {
      entry.id = normalizeVerificationMethodId(entry.id, documentId)
    } catch (error) {
      errors.push({
        path,
        code: 'invalid_verification_method_id',
        message: error.message
      })
    }
  }

  if (typeof entry.type !== 'string' || entry.type.length === 0) {
    errors.push({
      path,
      code: 'invalid_verification_method_type',
      message: 'Verification method type MUST be a non-empty string'
    })
  }

  if (typeof entry.controller !== 'string') {
    errors.push({
      path,
      code: 'invalid_verification_method_controller',
      message: 'Verification method controller MUST be a DID or DID URL string'
    })
  } else {
    let normalizedController
    try {
      normalizedController = DID.normalize(entry.controller)
    } catch {
      try {
        normalizedController = DIDURL.parse(entry.controller).toString()
      } catch (error) {
        errors.push({
          path,
          code: 'invalid_verification_method_controller',
          message: error.message
        })
      }
    }
    if (normalizedController) {
      entry.controller = normalizedController
    }
  }

  return entry
}

function normalizeVerificationMethodId (id, documentId) {
  if (id.startsWith('#')) {
    if (!documentId) {
      throw new DIDError(
        'invalid_verification_method_id',
        'Relative id requires a document context'
      )
    }
    return `${documentId}${id}`
  }
  if (id.startsWith('did:')) {
    return DIDURL.parse(id).toString()
  }
  if (isAbsoluteUri(id)) {
    return id
  }
  throw new DIDError(
    'invalid_verification_method_id',
    `Invalid verification method id: ${id}`
  )
}

function normalizeService (value, documentId, errors, index) {
  if (!isPlainObject(value)) {
    errors.push({
      path: 'service',
      code: 'invalid_service',
      message: 'Service MUST be an object'
    })
    return null
  }
  const service = structuredCloneJSON(value)
  if (typeof service.id !== 'string') {
    errors.push({
      path: `service[${index}]`,
      code: 'invalid_service_id',
      message: 'Service MUST include an id'
    })
  } else {
    service.id = normalizeServiceId(service.id, documentId)
  }
  if (typeof service.type === 'string') {
    service.type = [service.type]
  } else if (Array.isArray(service.type)) {
    service.type = service.type.map(String)
  } else {
    errors.push({
      path: `service[${index}].type`,
      code: 'invalid_service_type',
      message: 'Service type MUST be string or array of strings'
    })
  }
  if (service.serviceEndpoint == null) {
    errors.push({
      path: `service[${index}].serviceEndpoint`,
      code: 'invalid_service_endpoint',
      message: 'Service MUST include a serviceEndpoint'
    })
  }
  return freezeDeep(service)
}

function normalizeServiceId (id, documentId) {
  if (id.startsWith('#')) {
    if (!documentId) {
      throw new DIDError(
        'invalid_service_id',
        'Relative id requires a document context'
      )
    }
    return `${documentId}${id}`
  }
  if (id.startsWith('did:')) {
    return DIDURL.parse(id).toString()
  }
  if (isAbsoluteUri(id)) {
    return id
  }
  throw new DIDError('invalid_service_id', `Invalid service id: ${id}`)
}

function normalizeRelationshipReference (reference, documentId) {
  if (typeof reference !== 'string' || reference.length === 0) {
    throw new DIDError(
      'invalid_reference',
      'Verification relationship reference must be a string'
    )
  }
  if (reference.startsWith('#')) {
    if (!documentId) {
      throw new DIDError(
        'invalid_reference',
        'Relative reference requires a document context'
      )
    }
    return `${documentId}${reference}`
  }
  try {
    return DIDURL.parse(reference).toString()
  } catch (error) {
    try {
      return DID.normalize(reference)
    } catch {
      if (isAbsoluteUri(reference)) {
        return reference
      }
      throw new DIDError('invalid_reference', error.message)
    }
  }
}

export function validateDocument (document, { allowExtensions = true } = {}) {
  const errors = []
  if (!isPlainObject(document)) {
    return {
      ok: false,
      errors: [
        {
          path: '',
          code: 'invalid_document',
          message: 'DID document MUST be a JSON object'
        }
      ]
    }
  }

  const normalized = structuredCloneJSON(document)
  try {
    normalized['@context'] = normalizeContexts(normalized['@context'])
  } catch (error) {
    errors.push({
      path: '@context',
      code: 'invalid_context',
      message: error.message
    })
  }

  let documentId = null
  if (typeof normalized.id !== 'string') {
    errors.push({
      path: 'id',
      code: 'invalid_id',
      message: 'DID document MUST include the id property'
    })
  } else {
    try {
      const parsed = parseDIDLike(normalized.id)
      documentId = formatDID(parsed)
      normalized.id = documentId
    } catch (error) {
      errors.push({ path: 'id', code: 'invalid_id', message: error.message })
    }
  }

  normalized.alsoKnownAs = validateAlsoKnownAs(normalized.alsoKnownAs, errors)
  normalized.controller = validateControllers(
    normalized.controller,
    errors,
    'controller'
  )

  if (Array.isArray(normalized.verificationMethod)) {
    normalized.verificationMethod = normalized.verificationMethod
      .map((value, index) =>
        normalizeVerificationMethod(
          value,
          documentId,
          errors,
          `verificationMethod[${index}]`
        )
      )
      .filter(Boolean)
    ensureUniqueIds(normalized.verificationMethod, errors, 'verificationMethod')
  } else if (normalized.verificationMethod != null) {
    errors.push({
      path: 'verificationMethod',
      code: 'invalid_verification_method',
      message: 'verificationMethod MUST be an array'
    })
    normalized.verificationMethod = []
  }

  for (const relationship of CORE_VERIFICATION_RELATIONSHIPS) {
    if (normalized[relationship] == null) continue
    if (!Array.isArray(normalized[relationship])) {
      errors.push({
        path: relationship,
        code: 'invalid_verification_relationship',
        message: `${relationship} MUST be an array`
      })
      normalized[relationship] = []
      continue
    }
    normalized[relationship] = normalized[relationship]
      .map((entry, index) => {
        if (typeof entry === 'string') {
          try {
            return normalizeRelationshipReference(entry, documentId)
          } catch (error) {
            errors.push({
              path: `${relationship}[${index}]`,
              code: 'invalid_reference',
              message: error.message
            })
            return null
          }
        }
        return normalizeVerificationMethod(
          entry,
          documentId,
          errors,
          `${relationship}[${index}]`
        )
      })
      .filter(Boolean)
  }

  if (Array.isArray(normalized.service)) {
    normalized.service = normalized.service
      .map((value, index) => normalizeService(value, documentId, errors, index))
      .filter(Boolean)
    ensureUniqueIds(normalized.service, errors, 'service')
  } else if (normalized.service != null) {
    errors.push({
      path: 'service',
      code: 'invalid_service',
      message: 'service MUST be an array'
    })
    normalized.service = []
  }

  if (!allowExtensions) {
    const keys = Object.keys(normalized)
    for (const key of keys) {
      if (!CORE_DOCUMENT_PROPERTIES.includes(key)) {
        errors.push({
          path: key,
          code: 'unexpected_property',
          message: `Unexpected property ${key}`
        })
      }
    }
  }

  return {
    ok: errors.length === 0,
    errors,
    document: documentId ? DID.parse(documentId) : null,
    normalized
  }
}

export class DIDDocument {
  constructor (input) {
    if (input instanceof DIDDocument) {
      this.#document = structuredCloneJSON(input.#document)
      return
    }
    const source =
      typeof input === 'string'
        ? JSON.parse(input)
        : structuredCloneJSON(input || {})
    const { ok, errors, normalized } = validateDocument(source)
    if (!ok) {
      throw new DIDError(
        'invalid_document',
        'DID document failed validation',
        errors
      )
    }
    this.#document = normalized
  }

  get id () {
    return this.#document.id
  }

  get context () {
    return [...this.#document['@context']]
  }

  get alsoKnownAs () {
    return this.#document.alsoKnownAs ? [...this.#document.alsoKnownAs] : []
  }

  get controller () {
    return this.#document.controller ? [...this.#document.controller] : []
  }

  get verificationMethod () {
    return this.#document.verificationMethod
      ? structuredCloneJSON(this.#document.verificationMethod)
      : []
  }

  get services () {
    return this.#document.service
      ? structuredCloneJSON(this.#document.service)
      : []
  }

  setContext (contexts) {
    this.#document['@context'] = normalizeContexts(contexts)
    return this
  }

  setControllers (controllers) {
    const errors = []
    const normalized = validateControllers(controllers, errors, 'controller')
    if (errors.length) {
      throw new DIDError(
        'invalid_controller',
        'Failed to normalize controller values',
        errors
      )
    }
    this.#document.controller = normalized
    return this
  }

  setAlsoKnownAs (list) {
    const errors = []
    const normalized = validateAlsoKnownAs(list, errors)
    if (errors.length) {
      throw new DIDError(
        'invalid_alsoKnownAs',
        'Failed to normalize alsoKnownAs entries',
        errors
      )
    }
    this.#document.alsoKnownAs = normalized
    return this
  }

  addVerificationMethod (method) {
    const errors = []
    const entry = normalizeVerificationMethod(
      method,
      this.id,
      errors,
      'verificationMethod'
    )
    if (errors.length) {
      throw new DIDError(
        'invalid_verification_method',
        'Failed to add verification method',
        errors
      )
    }
    const methods = this.#document.verificationMethod ?? []
    const index = methods.findIndex((existing) => existing.id === entry.id)
    if (index === -1) {
      methods.push(entry)
    } else {
      methods[index] = entry
    }
    this.#document.verificationMethod = methods
    return this
  }

  removeVerificationMethod (id) {
    if (!this.#document.verificationMethod) return this
    let normalizedId = id
    try {
      normalizedId = normalizeVerificationMethodId(id, this.id)
    } catch {
      if (typeof id === 'string' && id.startsWith('#')) {
        normalizedId = `${this.id}${id}`
      }
    }
    this.#document.verificationMethod =
      this.#document.verificationMethod.filter(
        (entry) => entry.id !== normalizedId && entry.id !== id
      )
    for (const relationship of CORE_VERIFICATION_RELATIONSHIPS) {
      if (!Array.isArray(this.#document[relationship])) continue
      this.#document[relationship] = this.#document[relationship].filter(
        (entry) => {
          if (typeof entry === 'string') {
            return entry !== normalizedId && entry !== id
          }
          return entry.id !== normalizedId && entry.id !== id
        }
      )
    }
    return this
  }

  addToRelationship (relationship, reference) {
    if (!CORE_VERIFICATION_RELATIONSHIPS.includes(relationship)) {
      throw new DIDError(
        'invalid_relationship',
        `Unknown verification relationship: ${relationship}`
      )
    }
    const entries = this.#document[relationship] ?? []
    if (typeof reference === 'string') {
      const normalized = normalizeRelationshipReference(reference, this.id)
      if (!entries.includes(normalized)) {
        entries.push(normalized)
      }
    } else {
      const errors = []
      const method = normalizeVerificationMethod(
        reference,
        this.id,
        errors,
        relationship
      )
      if (errors.length) {
        throw new DIDError(
          'invalid_verification_method',
          'Failed to add embedded method',
          errors
        )
      }
      const index = entries.findIndex(
        (entry) => typeof entry === 'object' && entry.id === method.id
      )
      if (index === -1) {
        entries.push(method)
      } else {
        entries[index] = method
      }
    }
    this.#document[relationship] = entries
    return this
  }

  removeFromRelationship (relationship, targetId) {
    if (!CORE_VERIFICATION_RELATIONSHIPS.includes(relationship)) return this
    if (!Array.isArray(this.#document[relationship])) return this
    const normalizedId = normalizeRelationshipReference(targetId, this.id)
    this.#document[relationship] = this.#document[relationship].filter(
      (entry) => {
        if (typeof entry === 'string') {
          return entry !== normalizedId && entry !== targetId
        }
        return entry.id !== normalizedId && entry.id !== targetId
      }
    )
    return this
  }

  addService (service) {
    const errors = []
    const entry = normalizeService(
      service,
      this.id,
      errors,
      this.#document.service?.length ?? 0
    )
    if (errors.length) {
      throw new DIDError('invalid_service', 'Failed to add service', errors)
    }
    const services = this.#document.service ?? []
    const index = services.findIndex((existing) => existing.id === entry.id)
    if (index === -1) {
      services.push(entry)
    } else {
      services[index] = entry
    }
    this.#document.service = services
    return this
  }

  removeService (id) {
    if (!this.#document.service) return this
    this.#document.service = this.#document.service.filter(
      (entry) => entry.id !== id
    )
    return this
  }

  toJSON () {
    return structuredCloneJSON(this.#document)
  }

  toObject () {
    return this.toJSON()
  }

  clone () {
    return new DIDDocument(this)
  }

  freeze () {
    freezeDeep(this.#document)
    return this
  }

  static from (input) {
    return input instanceof DIDDocument ? input : new DIDDocument(input)
  }

  #document
}

function normalizeResolutionResult (result, { representation = false } = {}) {
  const didResolutionMetadata = isPlainObject(result?.didResolutionMetadata)
    ? { ...result.didResolutionMetadata }
    : {}
  const didDocumentMetadata = isPlainObject(result?.didDocumentMetadata)
    ? { ...result.didDocumentMetadata }
    : {}

  if (representation) {
    let stream = null
    if (result?.didDocumentStream != null) {
      stream = cloneStream(result.didDocumentStream)
    } else if (result?.didDocument != null) {
      stream = encodeDidDocumentStream(result.didDocument)
    }
    if (stream != null && didResolutionMetadata.contentType == null) {
      didResolutionMetadata.contentType = 'application/did+json'
    }
    stream = assertValidRepresentationStream(
      stream,
      didResolutionMetadata.contentType
    )
    return {
      didResolutionMetadata,
      didDocumentStream: stream,
      didDocumentMetadata
    }
  }

  const didDocument =
    result?.didDocument != null ? structuredCloneJSON(result.didDocument) : null
  return { didResolutionMetadata, didDocument, didDocumentMetadata }
}

function encodeDidDocumentStream (document) {
  if (document == null) return null
  if (typeof document === 'string') return encodeToBytes(document)
  return encodeToBytes(JSON.stringify(document))
}

function encodeToBytes (value) {
  if (TEXT_ENCODER) {
    return TEXT_ENCODER.encode(value)
  }
  if (typeof Buffer !== 'undefined') {
    return Buffer.from(value, 'utf8')
  }
  const bytes = new Uint8Array(value.length)
  for (let index = 0; index < value.length; index++) {
    bytes[index] = value.charCodeAt(index) & 0xff
  }
  return bytes
}

function decodeToString (value) {
  if (typeof value === 'string') {
    return value
  }
  if (
    value instanceof Uint8Array ||
    (typeof Buffer !== 'undefined' &&
      typeof Buffer.isBuffer === 'function' &&
      Buffer.isBuffer(value))
  ) {
    if (TEXT_DECODER) {
      return TEXT_DECODER.decode(value)
    }
    if (typeof Buffer !== 'undefined') {
      return Buffer.from(value).toString('utf8')
    }
    let result = ''
    for (const byte of value) {
      result += String.fromCharCode(byte)
    }
    return result
  }
  if (ArrayBuffer.isView(value)) {
    const view = value
    return decodeToString(
      new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    )
  }
  if (value instanceof ArrayBuffer) {
    return decodeToString(new Uint8Array(value))
  }
  throw new DIDError(
    'invalid_representation_stream',
    'Unsupported representation stream type'
  )
}

function isBinaryLike (value) {
  return (
    value instanceof Uint8Array ||
    ArrayBuffer.isView(value) ||
    value instanceof ArrayBuffer
  )
}

function assertValidRepresentationStream (stream, contentType) {
  if (stream == null) {
    return stream
  }
  const isString = typeof stream === 'string'
  const isBinary = isBinaryLike(stream)
  if (!isString && !isBinary) {
    throw new DIDError(
      'invalid_representation_stream',
      'didDocumentStream must be a string, ArrayBuffer, or Uint8Array'
    )
  }

  const type =
    typeof contentType === 'string' && contentType.length
      ? contentType
      : 'application/did+json'
  if (isJsonMediaType(type)) {
    const text = isString ? stream : decodeToString(stream)
    try {
      JSON.parse(text)
    } catch {
      throw new DIDError(
        'invalid_representation_stream',
        'didDocumentStream must contain valid JSON data'
      )
    }
    return isString ? text : stream
  }

  if (isCborMediaType(type) && !isBinary) {
    throw new DIDError(
      'invalid_representation_stream',
      'CBOR representations must be provided as binary data'
    )
  }
  return stream
}

function isJsonMediaType (type) {
  const normalized = type.toLowerCase().split(';')[0].trim()
  return (
    normalized.endsWith('/json') ||
    normalized === 'application/json' ||
    normalized.includes('+json')
  )
}

function isCborMediaType (type) {
  const normalized = type.toLowerCase().split(';')[0].trim()
  return normalized === 'application/cbor' || normalized.includes('+cbor')
}

function cloneStream (stream) {
  if (stream == null) return stream
  if (typeof stream === 'string') return stream
  if (stream instanceof Uint8Array) return stream.slice()
  if (ArrayBuffer.isView(stream)) {
    const view = stream
    return new view.constructor(view)
  }
  if (stream instanceof ArrayBuffer) {
    return stream.slice(0)
  }
  if (typeof globalThis.structuredClone === 'function') {
    try {
      return globalThis.structuredClone(stream)
    } catch {}
  }
  return stream
}

function normalizeDereferencingResult (result) {
  return {
    dereferencingMetadata: isPlainObject(result?.dereferencingMetadata)
      ? { ...result.dereferencingMetadata }
      : {},
    contentStream: result?.contentStream ?? null,
    contentMetadata: isPlainObject(result?.contentMetadata)
      ? { ...result.contentMetadata }
      : {}
  }
}

export class Resolver {
  constructor ({ drivers = {}, cache = null } = {}) {
    this.#drivers = new Map()
    this.#cache = cache instanceof Map ? cache : null
    for (const [method, driver] of Object.entries(drivers)) {
      this.register(method, driver)
    }
  }

  register (method, driver) {
    const normalizedMethod = normalizeMethodName(method)
    if (!driver || typeof driver.resolve !== 'function') {
      throw new DIDError(
        'invalid_driver',
        'Resolver driver MUST implement resolve(did, options)'
      )
    }
    this.#drivers.set(normalizedMethod, driver)
  }

  unregister (method) {
    return this.#drivers.delete(normalizeMethodName(method))
  }

  has (method) {
    return this.#drivers.has(normalizeMethodName(method))
  }

  get (method) {
    return this.#drivers.get(normalizeMethodName(method))
  }

  listMethods () {
    return [...this.#drivers.keys()]
  }

  async resolve (did, options = {}) {
    const identifier = DID.normalize(did)
    const parsed = DID.parse(identifier)
    const cacheKey = options.cache === false ? null : `resolve:${identifier}`
    if (cacheKey && this.#cache?.has(cacheKey)) {
      return structuredCloneJSON(this.#cache.get(cacheKey))
    }
    const driver = this.#drivers.get(parsed.method)
    if (!driver) {
      throw new DIDError(
        'unsupported_did_method',
        `No resolver registered for method: ${parsed.method}`
      )
    }
    const result = await driver.resolve(identifier, options)
    const normalized = normalizeResolutionResult(result)
    if (normalized.didDocument) {
      const validation = validateDocument(normalized.didDocument, {
        allowExtensions: true
      })
      if (!validation.ok) {
        normalized.didResolutionMetadata = {
          ...normalized.didResolutionMetadata,
          error: normalized.didResolutionMetadata.error || 'invalidDocument',
          message: 'DID document failed validation',
          details: validation.errors
        }
      } else {
        normalized.didDocument = validation.normalized
      }
    }
    if (cacheKey && this.#cache) {
      this.#cache.set(cacheKey, structuredCloneJSON(normalized))
    }
    return normalized
  }

  async resolveRepresentation (did, options = {}) {
    const identifier = DID.normalize(did)
    const parsed = DID.parse(identifier)
    const cacheKey =
      options.cache === false
        ? null
        : `resolveRepresentation:${identifier}:${options.accept || ''}`
    if (cacheKey && this.#cache?.has(cacheKey)) {
      return structuredCloneJSON(this.#cache.get(cacheKey))
    }
    const driver = this.#drivers.get(parsed.method)
    if (!driver) {
      throw new DIDError(
        'unsupported_did_method',
        `No resolver registered for method: ${parsed.method}`
      )
    }
    let result
    if (typeof driver.resolveRepresentation === 'function') {
      result = await driver.resolveRepresentation(identifier, options)
    } else {
      const resolution = await this.resolve(identifier, options)
      result = {
        didResolutionMetadata: { ...resolution.didResolutionMetadata },
        didDocumentMetadata: { ...resolution.didDocumentMetadata },
        didDocumentStream: encodeDidDocumentStream(resolution.didDocument)
      }
      if (
        result.didDocumentStream != null &&
        result.didResolutionMetadata.contentType == null
      ) {
        result.didResolutionMetadata.contentType = 'application/did+json'
      }
    }
    const normalized = normalizeResolutionResult(result, {
      representation: true
    })
    if (cacheKey && this.#cache) {
      this.#cache.set(cacheKey, structuredCloneJSON(normalized))
    }
    return normalized
  }

  async dereference (didUrl, options = {}) {
    const url = DIDURL.parse(didUrl)
    const identifier = url.toString()
    const cacheKey =
      options.cache === false ? null : `dereference:${identifier}`
    if (cacheKey && this.#cache?.has(cacheKey)) {
      return structuredCloneJSON(this.#cache.get(cacheKey))
    }
    const driver = this.#drivers.get(url.method)
    if (driver && typeof driver.dereference === 'function') {
      const result = normalizeDereferencingResult(
        await driver.dereference(identifier, options)
      )
      if (cacheKey && this.#cache) {
        this.#cache.set(cacheKey, structuredCloneJSON(result))
      }
      return result
    }

    const resolution = await this.resolve(url, options)
    const dereferenced = fallbackDereference(url, resolution)
    if (cacheKey && this.#cache) {
      this.#cache.set(cacheKey, structuredCloneJSON(dereferenced))
    }
    return dereferenced
  }

  #drivers
  #cache
}

function fallbackDereference (url, resolution) {
  if (!resolution.didDocument) {
    return {
      dereferencingMetadata: {
        error: resolution.didResolutionMetadata?.error || 'notFound',
        message: 'DID document is not available'
      },
      contentStream: null,
      contentMetadata: {}
    }
  }

  const doc = resolution.didDocument
  const target = findDereferenceTarget(url, doc)
  if (!target) {
    return {
      dereferencingMetadata: {
        error: 'notFound',
        message: 'Unable to locate target within DID document'
      },
      contentStream: null,
      contentMetadata: {}
    }
  }

  return {
    dereferencingMetadata: {
      contentType:
        typeof target === 'string' ? 'text/plain' : 'application/json'
    },
    contentStream: structuredCloneJSON(target),
    contentMetadata: {}
  }
}

function findDereferenceTarget (url, document) {
  if (url.fragment) {
    const absoluteId = url.fragment.startsWith('#')
      ? `${url.href.split('#')[0]}${url.fragment}`
      : `${url.href.split('#')[0]}#${url.fragment}`
    const candidates = [
      ...(document.verificationMethod || []),
      ...CORE_VERIFICATION_RELATIONSHIPS.flatMap((rel) => document[rel] || []),
      ...(document.service || [])
    ]
    for (const entry of candidates) {
      if (typeof entry === 'string') {
        if (
          entry === absoluteId ||
          entry === `did:${url.method}:${url.methodSpecificId}#${url.fragment}`
        ) {
          return entry
        }
      } else if (
        entry?.id === absoluteId ||
        entry?.id ===
          `did:${url.method}:${url.methodSpecificId}#${url.fragment}`
      ) {
        return entry
      }
    }
  }

  if (url.parameters.has('service')) {
    const service = url.parameters.get('service')?.[0]
    if (service) {
      const services = document.service || []
      return services.find(
        (entry) => entry.id.endsWith(`#${service}`) || entry.id === service
      )
    }
  }

  return null
}

export const constants = {
  METHOD_NAME_PATTERN,
  METHOD_SPECIFIC_ID_PATTERN,
  DID_PATTERN,
  PARAM_CHAR_PATTERN,
  PARAM_VALUE_PATTERN,
  CORE_CONTEXT,
  CORE_VERIFICATION_RELATIONSHIPS,
  STANDARD_PARAMETERS,
  CORE_DOCUMENT_PROPERTIES
}

export {
  METHOD_NAME_PATTERN,
  METHOD_SPECIFIC_ID_PATTERN,
  DID_PATTERN,
  PARAM_CHAR_PATTERN,
  PARAM_VALUE_PATTERN,
  CORE_CONTEXT,
  CORE_VERIFICATION_RELATIONSHIPS,
  STANDARD_PARAMETERS,
  CORE_DOCUMENT_PROPERTIES
}

export const parse = (input) => DID.parse(input)
export const parseUrl = (input) => DIDURL.parse(input)
export const normalize = (input) => DID.normalize(input)
export const normalizeUrl = (input) => DIDURL.from(input).toString()
export const isValid = (input) => {
  try {
    DID.parse(input)
    return true
  } catch {
    return false
  }
}
export const isValidUrl = (input) => {
  try {
    DIDURL.parse(input)
    return true
  } catch {
    return false
  }
}
