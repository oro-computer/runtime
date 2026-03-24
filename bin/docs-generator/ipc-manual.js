import * as acorn from 'acorn'
import * as walk from 'acorn-walk'

const IPC_METHODS = new Set(['request', 'send', 'sendSync', 'write'])

function escapeRoffText (text) {
  let value = String(text ?? '')
    .replace(/\\/g, '\\\\')
    .replace(/-/g, '\\-')

  if (value.startsWith('.') || value.startsWith("'")) {
    value = `\\&${value}`
  }

  return value
}

function renderParagraphs (paragraphs = []) {
  return paragraphs
    .map((paragraph) => paragraph.replace(/\s+/g, ' ').trim())
    .filter(Boolean)
    .map((paragraph) => `.PP\n${escapeRoffText(paragraph)}\n`)
    .join('')
}

function renderBulletList (items = []) {
  return items.map((item) => `.IP \\(bu 2\n${escapeRoffText(item)}\n`).join('')
}

function renderCodeBlock (lines = []) {
  if (!lines.length) return ''

  return `.nf\n${lines.map((line) => escapeRoffText(line)).join('\n')}\n.fi\n`
}

function renderSeeAlso (entries = []) {
  if (!entries.length) return ''
  return `.SH SEE ALSO\n${entries.map(escapeRoffText).join(', ')}\n`
}

function normalizeDocComment (comment) {
  return String(comment ?? '')
    .replace(/^\s*\*/gm, '')
    .replace(/```[a-z]*\s*([\s\S]*?)```/gi, '$1')
    .split('\n')
    .filter((line) => !line.trim().startsWith('@'))
    .join('\n')
    .trim()
}

function firstParagraph (comment) {
  const normalized = normalizeDocComment(comment)
  if (!normalized) return ''
  return normalized
    .split(/\n\s*\n/g)
    .map((paragraph) => paragraph.replace(/\s+/g, ' ').trim())
    .find(Boolean)
}

function normalizeLocation (location) {
  return String(location).replace(/\\/g, '/')
}

function moduleSpecifierFromLocation (location, knownLocations = new Set()) {
  let relative = normalizeLocation(location)
    .replace(/^api\//, '')
    .replace(/\.js$/, '')

  if (relative.endsWith('/index')) {
    const aliasLocation = `api/${relative.slice(0, -'/index'.length)}.js`
    if (knownLocations.has(aliasLocation)) {
      relative = relative.slice(0, -'/index'.length)
    }
  }

  return `oro:${relative}`
}

function getStaticPropertyName (node) {
  if (!node) return null
  if (!node.computed && node.property?.type === 'Identifier') {
    return node.property.name
  }
  if (node.computed && node.property?.type === 'Literal') {
    return String(node.property.value)
  }
  return null
}

function collectCommentMap (source) {
  let accumulateComments = []
  const comments = {}

  acorn.parse(String(source), {
    ecmaVersion: 'latest',
    sourceType: 'module',
    locations: true,
    onToken: (token) => {
      comments[token.start] = accumulateComments
      accumulateComments = []
    },
    onComment: (block, comment) => {
      if (!block) return
      if (comment[0] !== '*') return
      accumulateComments.push(normalizeDocComment(comment))
    }
  })

  return comments
}

function getScopeBodies (ancestors) {
  const scopes = []

  for (const ancestor of ancestors) {
    if (ancestor.type === 'Program') {
      scopes.push(ancestor.body)
      continue
    }

    if (ancestor.type === 'BlockStatement') {
      scopes.push(ancestor.body)
    }
  }

  return scopes
}

function getFunctionInfoFromNode (node) {
  if (!node) return null

  if (node.type === 'FunctionDeclaration' && node.id?.name) {
    return { name: node.id.name, node }
  }

  if (
    node.type === 'VariableDeclarator' &&
    node.id?.type === 'Identifier' &&
    (node.init?.type === 'FunctionExpression' ||
      node.init?.type === 'ArrowFunctionExpression')
  ) {
    return { name: node.id.name, node: node.init }
  }

  return null
}

function collectHelperWrappers (ast) {
  const helpers = new Map()

  function inspect (name, fnNode) {
    const routeParam = fnNode.params?.[0]
    if (routeParam?.type !== 'Identifier') return

    let info = null
    walk.simple(fnNode.body, {
      CallExpression (node) {
        if (info) return
        if (node.callee?.type !== 'MemberExpression') return
        if (node.callee.object?.type !== 'Identifier') return
        if (node.callee.object.name !== 'ipc') return

        const method = getStaticPropertyName(node.callee)
        if (!IPC_METHODS.has(method)) return

        const firstArg = node.arguments?.[0]
        if (firstArg?.type !== 'Identifier') return
        if (firstArg.name !== routeParam.name) return

        const optionsArg =
          method === 'write' ? node.arguments?.[3] : node.arguments?.[2]
        const binaryResponse =
          optionsArg?.type === 'ObjectExpression' &&
          optionsArg.properties?.some(
            (property) =>
              property.type === 'Property' &&
              !property.computed &&
              property.key?.type === 'Identifier' &&
              property.key.name === 'responseType' &&
              property.value?.type === 'Literal' &&
              property.value.value === 'arraybuffer'
          )

        info = {
          transport: method,
          binaryRequest: method === 'write',
          binaryResponse
        }
      }
    })

    if (info) {
      helpers.set(name, info)
    }
  }

  for (const statement of ast.body) {
    const functionInfo = getFunctionInfoFromNode(statement)
    if (functionInfo) {
      inspect(functionInfo.name, functionInfo.node)
      continue
    }

    if (statement.type !== 'VariableDeclaration') continue
    for (const declaration of statement.declarations) {
      const info = getFunctionInfoFromNode(declaration)
      if (info) {
        inspect(info.name, info.node)
      }
    }
  }

  return helpers
}

function getBindingFromStatement (statement, name) {
  if (statement.type === 'VariableDeclaration') {
    for (const declaration of statement.declarations) {
      if (
        declaration.id?.type === 'Identifier' &&
        declaration.id.name === name
      ) {
        return { type: 'variable', node: declaration }
      }
    }
  }

  if (statement.type === 'FunctionDeclaration' && statement.id?.name === name) {
    return { type: 'function', node: statement }
  }

  if (
    statement.type === 'ForStatement' &&
    statement.init?.type === 'VariableDeclaration'
  ) {
    return getBindingFromStatement(statement.init, name)
  }

  return null
}

function lookupBinding (name, ancestors, position) {
  for (let index = ancestors.length - 1; index >= 0; index--) {
    const ancestor = ancestors[index]

    if (
      (ancestor.type === 'FunctionDeclaration' ||
        ancestor.type === 'FunctionExpression' ||
        ancestor.type === 'ArrowFunctionExpression') &&
      ancestor.params?.some(
        (param) => param.type === 'Identifier' && param.name === name
      )
    ) {
      return { type: 'parameter' }
    }

    const scopeBodies = getScopeBodies(ancestors.slice(0, index + 1)).reverse()
    const body = scopeBodies[0] ?? []

    for (const statement of body) {
      if (statement.start >= position) {
        break
      }

      const binding = getBindingFromStatement(statement, name)
      if (binding) {
        return binding
      }
    }
  }

  return null
}

function dedupeStrings (values) {
  return [...new Set(values.filter(Boolean))]
}

function evaluateNode (node, ancestors, position, seen = new Set()) {
  if (!node) return null
  if (seen.has(node)) return null
  seen.add(node)

  if (node.type === 'Literal' && typeof node.value === 'string') {
    return { kind: 'strings', values: [node.value] }
  }

  if (node.type === 'TemplateLiteral' && node.expressions.length === 0) {
    return {
      kind: 'strings',
      values: [node.quasis.map((quasi) => quasi.value.cooked).join('')]
    }
  }

  if (node.type === 'ConditionalExpression') {
    const consequent = evaluateNode(
      node.consequent,
      ancestors,
      position,
      new Set(seen)
    )
    const alternate = evaluateNode(
      node.alternate,
      ancestors,
      position,
      new Set(seen)
    )

    return {
      kind: 'strings',
      values: dedupeStrings([
        ...(consequent?.kind === 'strings' ? consequent.values : []),
        ...(alternate?.kind === 'strings' ? alternate.values : [])
      ])
    }
  }

  if (node.type === 'LogicalExpression') {
    const left = evaluateNode(node.left, ancestors, position, new Set(seen))
    const right = evaluateNode(node.right, ancestors, position, new Set(seen))

    return {
      kind: 'strings',
      values: dedupeStrings([
        ...(left?.kind === 'strings' ? left.values : []),
        ...(right?.kind === 'strings' ? right.values : [])
      ])
    }
  }

  if (node.type === 'Identifier') {
    const binding = lookupBinding(node.name, ancestors, position)
    if (!binding || binding.type === 'parameter') {
      return null
    }

    if (binding.type === 'function') {
      return null
    }

    return evaluateNode(
      binding.node.init,
      ancestors,
      binding.node.start,
      new Set(seen)
    )
  }

  if (
    node.type === 'CallExpression' &&
    node.callee?.type === 'MemberExpression' &&
    node.callee.object?.type === 'Identifier' &&
    node.callee.object.name === 'Object' &&
    getStaticPropertyName(node.callee) === 'freeze'
  ) {
    return evaluateNode(node.arguments?.[0], ancestors, position, new Set(seen))
  }

  if (node.type === 'ObjectExpression') {
    const object = new Map()

    for (const property of node.properties) {
      if (property.type !== 'Property') continue

      let key = null
      if (!property.computed && property.key?.type === 'Identifier') {
        key = property.key.name
      } else if (property.key?.type === 'Literal') {
        key = String(property.key.value)
      }

      if (!key) continue

      const value = evaluateNode(
        property.value,
        ancestors,
        position,
        new Set(seen)
      )
      if (value?.kind === 'strings' && value.values.length > 0) {
        object.set(key, value.values)
      }
    }

    return { kind: 'object', value: object }
  }

  if (node.type === 'MemberExpression') {
    const propertyName = getStaticPropertyName(node)
    if (!propertyName) return null

    const objectValue = evaluateNode(
      node.object,
      ancestors,
      position,
      new Set(seen)
    )

    if (objectValue?.kind === 'object') {
      const values = objectValue.value.get(propertyName)
      if (values?.length) {
        return { kind: 'strings', values }
      }
    }
  }

  return null
}

function getNodeComments (commentMap, node) {
  return (commentMap[node.start] || []).filter(Boolean)
}

function describeContext (ancestors, commentMap, location, knownLocations) {
  const moduleSpecifier = moduleSpecifierFromLocation(location, knownLocations)
  let className = null

  for (const ancestor of ancestors) {
    if (ancestor.type === 'ClassDeclaration' && ancestor.id?.name) {
      className = ancestor.id.name
    }
  }

  for (let index = ancestors.length - 1; index >= 0; index--) {
    const ancestor = ancestors[index]

    if (
      ancestor.type === 'MethodDefinition' &&
      ancestor.key?.type === 'Identifier'
    ) {
      const summary = firstParagraph(
        getNodeComments(commentMap, ancestor).join('\n')
      )
      return {
        label: className
          ? `${moduleSpecifier} ${className}.${ancestor.key.name}()`
          : `${moduleSpecifier} ${ancestor.key.name}()`,
        summary
      }
    }

    if (ancestor.type === 'FunctionDeclaration' && ancestor.id?.name) {
      const summary = firstParagraph(
        getNodeComments(commentMap, ancestor).join('\n')
      )
      return {
        label: `${moduleSpecifier} ${ancestor.id.name}()`,
        summary
      }
    }

    if (
      ancestor.type === 'VariableDeclarator' &&
      ancestor.id?.type === 'Identifier' &&
      (ancestor.init?.type === 'FunctionExpression' ||
        ancestor.init?.type === 'ArrowFunctionExpression')
    ) {
      const summary = firstParagraph(
        getNodeComments(commentMap, ancestor).join('\n')
      )
      return {
        label: `${moduleSpecifier} ${ancestor.id.name}()`,
        summary
      }
    }
  }

  return {
    label: moduleSpecifier,
    summary: ''
  }
}

function getTransportForCall (node, helperWrappers) {
  if (
    node.callee?.type === 'MemberExpression' &&
    node.callee.object?.type === 'Identifier'
  ) {
    if (node.callee.object.name === 'ipc') {
      const method = getStaticPropertyName(node.callee)
      if (!IPC_METHODS.has(method)) return null
      const optionsArg =
        method === 'write' ? node.arguments?.[3] : node.arguments?.[2]
      const binaryResponse =
        optionsArg?.type === 'ObjectExpression' &&
        optionsArg.properties?.some(
          (property) =>
            property.type === 'Property' &&
            !property.computed &&
            property.key?.type === 'Identifier' &&
            property.key.name === 'responseType' &&
            property.value?.type === 'Literal' &&
            property.value.value === 'arraybuffer'
        )

      return {
        transport: method,
        binaryRequest: method === 'write',
        binaryResponse
      }
    }

    return null
  }

  if (
    node.callee?.type === 'Identifier' &&
    helperWrappers.has(node.callee.name)
  ) {
    return helperWrappers.get(node.callee.name)
  }

  return null
}

function collectRoutesFromFile ({ location, source }, knownLocations) {
  const commentMap = collectCommentMap(source)
  const ast = acorn.parse(String(source), {
    ecmaVersion: 'latest',
    sourceType: 'module',
    locations: true
  })
  const helperWrappers = collectHelperWrappers(ast)
  const references = []
  const unresolved = []

  walk.ancestor(ast, {
    CallExpression (node, ancestors) {
      const transportInfo = getTransportForCall(node, helperWrappers)
      if (!transportInfo) return

      const resolved = evaluateNode(node.arguments?.[0], ancestors, node.start)
      const context = describeContext(
        ancestors,
        commentMap,
        location,
        knownLocations
      )
      const line = node.loc.start.line

      if (!resolved?.kind || resolved.values.length === 0) {
        if (node.arguments?.[0]?.type === 'Identifier') {
          const binding = lookupBinding(
            node.arguments[0].name,
            ancestors,
            node.start
          )
          if (binding?.type === 'parameter') {
            return
          }
        }

        unresolved.push({
          transport: transportInfo.transport,
          location: `${location}#L${line}`,
          label: context.label
        })
        return
      }

      for (const route of resolved.values) {
        references.push({
          route,
          domain: route.split('.')[0] || route,
          transport: transportInfo.transport,
          binaryRequest: Boolean(transportInfo.binaryRequest),
          binaryResponse: Boolean(transportInfo.binaryResponse),
          location: `${location}#L${line}`,
          moduleSpecifier: moduleSpecifierFromLocation(
            location,
            knownLocations
          ),
          label: context.label,
          summary: context.summary
        })
      }
    }
  })

  return { references, unresolved }
}

function collectRouteInventory (files) {
  const routes = new Map()
  const unresolved = []
  const knownLocations = new Set(
    files.map((file) => normalizeLocation(file.location))
  )

  for (const file of files) {
    const { references, unresolved: fileUnresolved } = collectRoutesFromFile(
      file,
      knownLocations
    )
    unresolved.push(...fileUnresolved)

    for (const reference of references) {
      if (!routes.has(reference.route)) {
        routes.set(reference.route, {
          route: reference.route,
          domain: reference.domain,
          transports: new Set(),
          binaryRequest: false,
          binaryResponse: false,
          modules: new Set(),
          references: [],
          summaries: new Set()
        })
      }

      const routeInfo = routes.get(reference.route)
      routeInfo.transports.add(reference.transport)
      routeInfo.binaryRequest ||= reference.binaryRequest
      routeInfo.binaryResponse ||= reference.binaryResponse
      routeInfo.modules.add(reference.moduleSpecifier)

      const referenceKey = `${reference.transport}|${reference.location}|${reference.label}`
      if (!routeInfo.references.some((entry) => entry.key === referenceKey)) {
        routeInfo.references.push({
          key: referenceKey,
          transport: reference.transport,
          location: reference.location,
          label: reference.label
        })
      }

      if (reference.summary) {
        routeInfo.summaries.add(reference.summary)
      }
    }
  }

  return {
    routes: [...routes.values()]
      .map((route) => ({
        ...route,
        transports: [...route.transports].sort(),
        modules: [...route.modules].sort(),
        references: route.references.sort((a, b) =>
          a.label.localeCompare(b.label)
        ),
        summaries: [...route.summaries].sort()
      }))
      .sort((a, b) => a.route.localeCompare(b.route)),
    unresolved
  }
}

function createManPage ({
  name,
  summary,
  description = [],
  sections = [],
  seeAlso = []
}) {
  let man = `.TH ${name.toUpperCase()} 7 "" "Oro Runtime" "Oro Runtime Guides"\n`
  man += '.SH NAME\n'
  man += `${escapeRoffText(name)} \\- ${escapeRoffText(summary)}\n`
  man += '.SH DESCRIPTION\n'
  man += renderParagraphs(description)

  for (const section of sections) {
    man += `.SH ${section.title}\n`
    if (section.paragraphs) {
      man += renderParagraphs(section.paragraphs)
    }
    if (section.bullets) {
      man += renderBulletList(section.bullets)
    }
    if (section.code) {
      man += renderCodeBlock(section.code)
    }
    if (section.raw) {
      man += section.raw
    }
  }

  man += renderSeeAlso(seeAlso)
  return {
    filename: `${name}.7`,
    content: man
  }
}

function renderRouteCatalog (routes, unresolved) {
  const grouped = new Map()

  for (const route of routes) {
    if (!grouped.has(route.domain)) {
      grouped.set(route.domain, [])
    }
    grouped.get(route.domain).push(route)
  }

  let output = ''
  output += renderParagraphs([
    `This catalog is generated from JavaScript calls to ipc.request(), ipc.send(), ipc.sendSync(), ipc.write(), and thin wrappers around those primitives. It inventories ${routes.length} resolved routes across ${grouped.size} top-level domains.`
  ])

  if (unresolved.length > 0) {
    output += renderParagraphs([
      `${unresolved.length} IPC call sites still use route expressions that could not be reduced to static strings. Review those locations before treating this catalog as complete for private dynamic dispatch.`
    ])
    output += '.IP \\(bu 2\n'
    output += unresolved
      .slice(0, 12)
      .map((item) =>
        escapeRoffText(`${item.transport} ${item.label} (${item.location})`)
      )
      .join('\n.IP \\(bu 2\n')
    output += '\n'
  }

  const domains = [...grouped.keys()].sort()
  output += '.PP\n'
  output += `${escapeRoffText(`Domains: ${domains.join(', ')}`)}\n`

  for (const domain of domains) {
    output += `.SS ${escapeRoffText(domain)}\n`

    for (const route of grouped.get(domain)) {
      output += '.TP\n'
      output += `\\fB${escapeRoffText(route.route)}\\fR\n`
      output += `${escapeRoffText(`Transports: ${route.transports.join(', ')}`)}\n`

      if (route.binaryRequest || route.binaryResponse) {
        const notes = []
        if (route.binaryRequest) notes.push('binary request body')
        if (route.binaryResponse) notes.push('binary response body')
        output += '.br\n'
        output += `${escapeRoffText(`Binary semantics: ${notes.join(', ')}`)}\n`
      }

      output += '.br\n'
      output += `${escapeRoffText(`Modules: ${route.modules.join(', ')}`)}\n`

      if (route.references.length > 0) {
        const labels = route.references
          .slice(0, 6)
          .map((reference) => `${reference.label} (${reference.location})`)
        if (route.references.length > 6) {
          labels.push(`and ${route.references.length - 6} more call sites`)
        }

        output += '.br\n'
        output += `${escapeRoffText(`Exposed by: ${labels.join('; ')}`)}\n`
      }

      if (route.summaries.length > 0) {
        output += '.br\n'
        output += `${escapeRoffText(`Notes: ${route.summaries.slice(0, 2).join(' ')}`)}\n`
      }
    }
  }

  return output
}

export function generateIpcManpages (files) {
  const { routes, unresolved } = collectRouteInventory(files)
  const routeCount = routes.length
  const domainCount = new Set(routes.map((route) => route.domain)).size

  return [
    createManPage({
      name: 'oro-ipc',
      summary: 'IPC protocol, transports, and result semantics',
      description: [
        'Oro Runtime exposes native capabilities to JavaScript through a small family of IPC primitives layered behind the oro:ipc module and higher-level oro:* APIs.',
        'For most application code, prefer the high-level module methods in man section 3. Reach for raw IPC only when you are building a library, binding a new service, debugging a protocol edge case, or bridging to extension/native code.'
      ],
      sections: [
        {
          title: 'PROCESS MODEL',
          paragraphs: [
            'The API surface is split across render processes, the bridge process, and optional main/background workers. JavaScript talks to native services through ipc:// requests, queued responses, and longer-lived transports such as Conduit.',
            'The bridge owns route dispatch and turns native responses back into JavaScript Result objects, exceptions, ArrayBuffers, or streamed events depending on the transport you chose.'
          ]
        },
        {
          title: 'CHOOSE THE RIGHT TRANSPORT',
          bullets: [
            'ipc.request(): asynchronous request/response. Use when you need a Result object, structured errors, abort signals, timeouts, or responseType control.',
            'ipc.send(): asynchronous fire-and-wait helper for standard JSON-like request flows. Good default for many service calls that do not need binary response tuning.',
            'ipc.sendSync(): blocking route invocation. Reserve it for low-level compatibility or cases where asynchronous control flow is not viable.',
            'ipc.write(): asynchronous request with a binary payload. Use for archive writes, compression, streaming model input, or any route that expects bytes.',
            'Conduit/WebSocket transport: preferred for high-frequency or long-lived message streams when a service supports it.'
          ]
        },
        {
          title: 'RESULTS, ERRORS, AND NORMALIZATION',
          paragraphs: [
            'At the public wrapper layer, most async IPC flows resolve to a Result-like shape with `data` and `err`. Downstream wrappers frequently convert that into thrown exceptions, richer classes, or narrower return types for convenience.',
            'The low-level IPC contract deliberately preserves enough metadata for debugging. Error objects may carry names, numeric or string codes, URLs, backend fields, and original payload fragments that a wrapper later rehydrates with `maybeMakeError()`.'
          ],
          bullets: [
            'Success responses usually arrive as { source, data } or as binary data when responseType is arraybuffer.',
            'Errors may come back as structured plain objects. oro:ipc rehydrates them into Error instances with name, code, url, and backend metadata preserved.',
            'Binary responses should validate content-type and buffer shape before assuming a payload is usable.',
            'If a wrapper throws but the underlying route normally reports `result.err`, inspect both the wrapper contract and the raw route behavior before changing either.'
          ]
        },
        {
          title: 'BINARY AND STRUCTURED-CLONE BOUNDARIES',
          paragraphs: [
            'A surprising amount of the runtime surface is byte-oriented: compression, tar/archive work, TLS pin extraction, AI/audio payloads, child-process pipes, and some network services. Those paths are different from plain object IPC and should be treated as such.',
            'Oro also supports structured-clone-friendly wrappers for channels and transfers. That is useful for ports, ArrayBuffers, and richer bridge-side event flow, but it still needs explicit transport-aware handling.'
          ],
          bullets: [
            'Use `ipc.write()` when a route expects bytes in the request body.',
            'Use `responseType: arraybuffer` when a route returns raw bytes and the caller must not coerce them into a string or JSON blob.',
            'Do not layer ad hoc base64 encoding on top of a route that already has a byte transport.',
            'When posting through IPC message ports, preserve transfer lists and cloned buffer semantics instead of copying arbitrarily.'
          ]
        },
        {
          title: 'MESSAGE PORTS, BROADCAST, AND EVENT FLOWS',
          paragraphs: [
            'Not every interaction is a single request followed by a single response. `IPCMessagePort`, `IPCMessageChannel`, and `IPCBroadcastChannel` exist so modules can coordinate event-driven flows without re-inventing transport logic on top of plain request routes.',
            'These surfaces matter when you are bridging between windows, workers, and runtime services that need durable message delivery or structured clone semantics.'
          ],
          bullets: [
            'Use request/response IPC for one-shot commands.',
            'Use ports or broadcast-style APIs when you need repeated message delivery with shared transport state.',
            'When debugging message delivery, check both lifecycle state and origin/channel naming, not just the payload.'
          ]
        },
        {
          title: 'WHEN TO DROP BELOW PUBLIC WRAPPERS',
          paragraphs: [
            'The existence of a route in the catalog does not mean application code should call it directly. Many route names are implementation details hidden behind a stable public module because the wrapper also validates input, normalizes output, or enforces platform checks.'
          ],
          bullets: [
            'Stay at the public wrapper when one already exists for the feature you need.',
            'Drop to raw IPC when you are integrating an advanced feature, debugging a bridge issue, or using a capability that does not yet have a stable higher-level wrapper.',
            'If you build an app-local wrapper around raw IPC, document the transport family, ownership key, and error shape explicitly so callers do not have to reverse-engineer the contract.'
          ]
        },
        {
          title: 'DEBUGGING CHECKLIST',
          bullets: [
            'Confirm the route name, owning module, and transport family before changing payload shape.',
            'Inspect `result.err`, timeout options, abort signals, and responseType together.',
            'If the bug reproduces only on one platform, verify capability gating before assuming the route is globally supported.',
            'For binary routes, validate whether the breakage is in encoding/decoding rather than in the native service implementation.'
          ]
        },
        {
          title: 'EXAMPLES',
          code: [
            "import ipc from 'oro:ipc'",
            '',
            "const result = await ipc.request('window.create', {",
            '  index: 0,',
            "  path: '/index.html'",
            '})',
            '',
            'if (result.err) throw result.err',
            'console.log(result.data)',
            '',
            "const compressed = await ipc.write('zlib.deflate', { format: 'gzip' }, bytes, {",
            "  responseType: 'arraybuffer'",
            '})',
            '',
            '// Ports are appropriate when you need repeated message delivery.',
            'const channel = new ipc.IPCMessageChannel()',
            "channel.port1.onmessage = (event) => console.log('port1', event.data)",
            "channel.port2.postMessage({ type: 'ping' })"
          ]
        },
        {
          title: 'NEED TO KNOW',
          bullets: [
            'Prefer the highest abstraction level that already exists. If oro:window or oro:sqlite already wraps a route, use that wrapper instead of calling the route directly.',
            'Choose async transports by default. sendSync() blocks and should stay exceptional.',
            'Routes are namespaced by domain prefix such as window.*, application.*, dbus.*, ai.*, or udp.*. That prefix is the fastest way to find the owning module and native service.',
            'Binary-capable routes often pair write() with responseType=arraybuffer. Treat those as protocol contracts, not incidental implementation details.',
            'When debugging, inspect both the JS call site and the service implementation; IPC problems are often caused by parameter shape or process-lifecycle timing rather than the route name itself.'
          ]
        }
      ],
      seeAlso: [
        'oro-ipc-routes(7)',
        'oro-api-concepts(7)',
        'oro-agent-workflows(7)',
        'oro-conduit(7)',
        'oro-ipc(3)',
        'oroc(1)'
      ]
    }),
    createManPage({
      name: 'oro-ipc-routes',
      summary: 'generated catalog of resolved IPC routes',
      description: [
        `This guide indexes ${routeCount} resolved IPC routes across ${domainCount} top-level domains.`,
        'Each route entry lists the transport families used from JavaScript, the public oro:* APIs that expose it, and the source locations that define or consume it.'
      ],
      sections: [
        {
          title: 'HOW TO READ THIS CATALOG',
          bullets: [
            'Transports show which oro:ipc primitive is used at the call site.',
            'Modules list the oro:* entry points that surface the route.',
            'Exposed by points at the nearest documented API function or method plus the source line where the route is invoked.',
            'Binary semantics flags routes that carry raw request or response bytes rather than plain JSON-like payloads.'
          ]
        },
        {
          title: 'ROUTES',
          raw: renderRouteCatalog(routes, unresolved)
        }
      ],
      seeAlso: [
        'oro-ipc(7)',
        'oro-agent-workflows(7)',
        'oro-ipc(3)',
        'oro-application(3)',
        'oro-window(3)'
      ]
    }),
    createManPage({
      name: 'oro-api-concepts',
      summary: 'cross-cutting concepts that shape the public API surface',
      description: [
        'Oro Runtime is easier to use when you treat it as a layered system rather than as a flat pile of modules. The public API surface is organized around ownership boundaries, origin-scoped state, route families, and explicit platform capability checks.',
        'This guide explains the concepts that recur across the installed JavaScript APIs, CLI commands, generated references, and MCP-discoverable runtime docs. It is intended to shorten the path from "I need to build something" to "I know which module, config key, and transport to use".'
      ],
      sections: [
        {
          title: 'START AT THE HIGHEST LAYER',
          bullets: [
            'Use `oroc` when you need project discovery, configuration inspection, build/package operations, update tooling, or host-level workflows.',
            'Use the section 3 manpages for stable JavaScript and C extension contracts. That is the compatibility boundary application code should target first.',
            'Use the section 7 manpages for concepts, route catalogs, and workflows when you need to understand how several modules fit together.',
            'Use raw `oro:ipc` only for advanced integrations, unsupported edge cases, or debugging when the higher-level wrapper layer is not enough.',
            'For AI-oriented exploration, the MCP surface should prefer `workspace_info`, `search_docs`, `resources/list`, and specialized tools before falling back to `run_cli` or raw file walking.'
          ]
        },
        {
          title: 'PROCESS MODEL AND OWNERSHIP',
          paragraphs: [
            'The runtime is split across distinct responsibilities. Render code runs your HTML, CSS, and JavaScript. The bridge brokers IPC and owns many native service boundaries. Some applications also use worker-like or background execution surfaces for heavier work.',
            'This split matters because many APIs are shaped by ownership. `oro:application` owns app-wide lifecycle, window creation, enumeration, menu/tray setup, and process-launch helpers. `oro:window` represents one concrete window and is usually the right entry point once you already know which window you are talking about.'
          ],
          bullets: [
            'If the operation affects the whole app, start from `oro:application` or the CLI.',
            'If the operation affects one concrete window, expect a window id or index to be part of the contract.',
            'If an API looks global but still asks for an index, origin, or scope, that value is usually the real ownership key.',
            'Route families often reveal ownership quickly: `window.*`, `application.*`, `internal.conduit.*`, `bluetooth.*`, `usb.*`, and so on.'
          ]
        },
        {
          title: 'IDENTIFIERS AND ISOLATION KEYS',
          paragraphs: [
            'Several subsystems are intentionally scoped rather than globally shared. The same application may have multiple windows, multiple worker contexts, several mounted resource roots, and state that must stay isolated by origin or bundle identity.',
            'That is why the API surface repeatedly uses values such as window index, bundle identifier, origin, route name, scope, and path. These are not cosmetic parameters. They usually determine routing, persistence boundaries, or permission checks.'
          ],
          bullets: [
            'Window index: identifies a concrete application window and is frequently used for window-scoped UI mutations.',
            'Origin: identifies the URL/security context for browser-facing features such as cookies, service workers, loaders, and request helpers.',
            'Bundle identifier: ties packaging, update identity, and runtime origin construction together.',
            'Secure-storage scope: isolates stored secrets by origin-like namespace rather than by key alone.',
            'Route name: identifies the backend capability behind a public wrapper. It is useful for debugging, but it is usually not the primary application-level API.'
          ]
        },
        {
          title: 'RESOURCE LOADING AND THE BROWSER BOUNDARY',
          paragraphs: [
            'Oro distinguishes between direct host filesystem access and browser-style resource access. This distinction is fundamental: code running with `oro:fs` can talk to host paths directly, while code inside the webview often needs URL-based access through mounted resources, `oro:` origins, or fetch-compatible handlers.',
            'A productive rule is to choose the interface that matches the consumer. If the browser/webview needs to load something by URL, expose it as a resource. If build scripts or trusted runtime code need direct file access, use filesystem APIs.'
          ],
          bullets: [
            'Use oro:fs for direct host filesystem reads and writes.',
            'Use navigator mounts and oro:// resources when the webview needs URL-based access rather than direct fs APIs.',
            'Expect mounted resources to remain read-only from the browser side even when the backing host directory is writable.',
            'Treat `build.copy`, `webview.root`, and related packaging config as part of the resource-loading contract, not just build metadata.',
            'Protocol handlers and service-worker-backed flows are separate concepts. When a feature says it requires service worker support, do not assume a plain static resource mount is enough.'
          ]
        },
        {
          title: 'CONFIG IS PART OF THE API CONTRACT',
          paragraphs: [
            'Configuration is not an afterthought in Oro. `oro.toml`, `oro.ini`, local overrides, and generated config references shape how the runtime boots, where resources come from, how apps are packaged, and which platform-specific knobs are active.',
            'Many "API" questions are really config questions in disguise. If window sizing, service workers, signing, update identity, or resource roots look surprising, inspect the effective config before assuming the module implementation is wrong.'
          ],
          bullets: [
            'Relative config paths are generally interpreted from the project root or from the directory that contains the explicit config file being used.',
            'Do not assume a service-worker mode. Respect the project configuration and documented runtime defaults.',
            'Autoindex is opt-in. If a browser-facing path fails because there is no implicit directory listing, that is likely expected behavior.',
            'The generated config docs are there to explain meaning, but the effective merged config is what determines runtime behavior.'
          ]
        },
        {
          title: 'TRANSPORTS AND DATA SHAPES',
          paragraphs: [
            'The runtime uses several transport styles on purpose. Some operations are request/response, some are binary, and some are long-lived streams. Choosing the right transport is part of using the API correctly.',
            'If a module already wraps transport details for you, use that wrapper. If you do need to drop lower, preserve the transport expectations instead of rewriting them ad hoc.'
          ],
          bullets: [
            '`ipc.request()` is the general async request/response path when you need errors, timeouts, abort signals, or response-shape control.',
            '`ipc.send()` is an async convenience path for standard structured requests that do not need binary tuning.',
            '`ipc.sendSync()` blocks and should remain exceptional.',
            '`ipc.write()` is for routes that take raw bytes or large binary payloads.',
            'Conduit is the long-lived path for sustained message flow or high-frequency streaming.',
            'Binary-oriented APIs frequently use `Buffer`, `Uint8Array`, `ArrayBuffer`, or `responseType: arraybuffer`. Treat those as part of the public contract.',
            'The runtime does not guarantee `SharedArrayBuffer` or `Atomics.wait`. Always code defensively when shared-memory assumptions would affect correctness.'
          ]
        },
        {
          title: 'WINDOWS, EVENTS, AND LIFECYCLE',
          paragraphs: [
            'A large amount of application behavior is window-centric. Window creation, enumeration, focus, menu targeting, and some event delivery all use the window index or an `ApplicationWindow` instance as the effective routing key.',
            'When you create a window, think beyond just the HTML path. Size constraints, service-worker requirements, protocol handlers, theme behavior, and window-scoped menu routing are all part of the practical lifecycle contract.'
          ],
          bullets: [
            'Treat `createWindow()` as the start of a resource-loading and lifecycle configuration decision, not just a UI allocation call.',
            'If a menu or tray API accepts an optional target window index, that means the platform may support window-scoped behavior rather than only app-global behavior.',
            'If you are hydrating or enumerating windows, preserve ordering semantics and index identity rather than assuming the list is just a plain array.'
          ]
        },
        {
          title: 'PLATFORM GATING AND PARTIAL SURFACES',
          paragraphs: [
            'Not every route or helper exists on every target. Some modules are fundamentally platform-specific, and some subsystems are present but intentionally partial on a subset of platforms.',
            'A productive workflow is to treat platform support as data you verify, not as something you infer from a route name or from another platform behaving a certain way.'
          ],
          bullets: [
            'Check docs and public wrappers before assuming DBus, XPC, secure storage, AI helpers, Bluetooth, WebUSB, or TLS helpers are equivalent across desktop and mobile targets.',
            'Public wrappers are the right place for capability checks and normalized errors. Route presence alone does not imply release-ready parity.',
            'When shipping cross-platform features, document the degraded path explicitly rather than leaving clients to discover it through runtime errors.'
          ]
        },
        {
          title: 'DISCOVERY AND DEBUGGING WORKFLOW',
          paragraphs: [
            'Humans and AI agents are most productive when they move from stable documentation down toward low-level protocol details only as needed. That means starting with high-level docs, then public module contracts, then route catalogs, and only then low-level IPC behavior.'
          ],
          bullets: [
            'Start with `oroc help <query>` for CLI discovery and operator workflows.',
            'Use section 3 manpages for exact public API contracts.',
            'Use section 7 manpages when you need concepts, route catalogs, or higher-level workflows.',
            'Use MCP `search_docs` and `resources/list` when operating through the CLI MCP server.',
            'When debugging a failing call, inspect the public wrapper, the owning route family, the config that shapes it, and the platform gate together.',
            'If a feature carries bytes, also inspect `responseType`, buffer conversion, and any wrapper-level normalization before blaming native code.'
          ]
        },
        {
          title: 'EXAMPLES',
          code: [
            "import { createWindow } from 'oro:application'",
            "import { readFile } from 'oro:fs/promises'",
            "import { setItem } from 'oro:secure-storage'",
            "import ipc from 'oro:ipc'",
            '',
            "const win = await createWindow({ index: 0, path: '/index.html' })",
            '',
            '// Direct host filesystem access for trusted runtime code.',
            "const html = await readFile('./src/index.html', 'utf8')",
            'console.log(html.length)',
            '',
            '// Origin- or bundle-scoped secret storage.',
            "await setItem('refresh-token', token, { scope: 'oro://com.example.app' })",
            '',
            '// Drop to low-level IPC only when the higher-level wrapper is not the right tool.',
            "const result = await ipc.request('window.getBackgroundColor', { index: win.index })",
            'if (result.err) throw result.err'
          ]
        },
        {
          title: 'NEED TO KNOW',
          bullets: [
            'The public JS module is usually the compatibility boundary. Route names and raw payloads are lower-level implementation details unless you are working on the runtime itself.',
            'Look for scope, origin, id, index, bundle identifier, or route prefix parameters first. They usually reveal the true ownership model faster than the function name alone.',
            'If behavior surprises you, inspect the effective config and the current platform before assuming the API contract was universal.',
            'If a guide sounds too generic, verify it against the section 3 contract and the generated route catalog. The productive path is specific, not hand-wavy.',
            'When you build app-local abstractions on top of Oro, preserve the ownership key, config dependency, transport shape, and platform limits in your own docs and helpers.'
          ]
        }
      ],
      seeAlso: [
        'oroc(1)',
        'oroc-help(1)',
        'oro-ipc(7)',
        'oro-ipc-routes(7)',
        'oro-agent-workflows(7)',
        'oro-conduit(7)',
        'oro-application(3)',
        'oro-window(3)',
        'oro-ipc(3)',
        'oro-secure-storage(3)',
        'oro-fs(3)'
      ]
    }),
    createManPage({
      name: 'oro-conduit',
      summary: 'high-frequency binary transport guide for runtime services',
      description: [
        'Conduit is the long-lived WebSocket-based transport used when request/response IPC would impose too much overhead or UI-thread churn.',
        'Services such as UDP, streaming AI workloads, and future high-throughput features should prefer Conduit when they need sustained message flow and graceful reconnect behaviour.'
      ],
      sections: [
        {
          title: 'WHEN TO USE IT',
          bullets: [
            'Use Conduit for sustained inbound message streams.',
            'Use it when reconnect and pause/resume behaviour matter more than one-shot request latency.',
            'Keep ipc.request()/ipc.write() as the fallback path for setup, capability checks, and degraded operation.'
          ]
        },
        {
          title: 'MESSAGE MODEL',
          paragraphs: [
            'Conduit messages are binary payloads with a compact header section. Header values carry route and metadata, while the payload remains a raw Uint8Array.',
            'The transport is intentionally stable across reconnects: clients hold an id, the runtime restarts the server on pause/resume transitions, and reopen events tell higher layers to resubscribe.'
          ]
        },
        {
          title: 'LIFECYCLE AND RECONNECT',
          paragraphs: [
            'Conduit is designed around the reality that sockets may disappear during application lifecycle transitions, especially on mobile targets. A Conduit client is expected to reconnect and restore subscriptions rather than assuming a single immortal connection.',
            'That lifecycle behavior is not accidental glue code. It is a core part of the contract and should shape how higher-level services model state restoration.'
          ],
          bullets: [
            'Register receive handlers before sending commands that expect streamed responses.',
            'Treat reconnect or reopen notifications as the signal to restore subscriptions, listeners, or producer state.',
            'If a feature cannot survive reconnect, Conduit is probably the wrong transport for its current design.'
          ]
        },
        {
          title: 'STATUS, DIAGNOSTICS, AND SHARED KEYS',
          paragraphs: [
            'The public `oro:conduit` module exposes status and diagnostics helpers because connection state is operational data, not a hidden implementation detail. If you are debugging a stream-heavy feature, inspect the transport first.'
          ],
          bullets: [
            'Use status queries to verify whether the server is active and which port/key are currently in effect.',
            'Use diagnostics to confirm whether handles or clients are accumulating unexpectedly.',
            'If a shared key is part of the flow, treat it as a real compatibility/security input rather than as a debug convenience.'
          ]
        },
        {
          title: 'CHOOSING BETWEEN CONDUIT AND IPC',
          bullets: [
            'Choose plain IPC for one-shot commands, setup requests, validation, and fallback flows.',
            'Choose Conduit for sustained streams, push-heavy workloads, or transports where polling would be wasteful.',
            'Keep the initial capability check on IPC even when the steady-state data path uses Conduit.',
            'If a service must degrade cleanly, design the IPC path first and let Conduit be the acceleration path.'
          ]
        },
        {
          title: 'EXAMPLE',
          code: [
            "import Conduit from 'oro:conduit'",
            '',
            "const conduit = new Conduit({ id: 'udp-client' })",
            'conduit.receive((err, message) => {',
            '  if (err) throw err',
            '  console.log(message.options.route, message.payload)',
            '})',
            '',
            'await conduit.connect()'
          ]
        },
        {
          title: 'DEBUGGING CHECKLIST',
          bullets: [
            'Confirm the server is active before debugging application-level message handling.',
            'Inspect reconnect paths, not just initial connect paths.',
            'If `send()` returns false, treat the socket as unusable and switch to recovery logic immediately.',
            'When a stream stalls only on mobile lifecycle transitions, debug pause/resume handling first.'
          ]
        },
        {
          title: 'NEED TO KNOW',
          bullets: [
            'Register receive handlers before issuing commands that expect streamed data.',
            'A false return from send() means the socket is unusable and the caller should fall back and trigger reconnect().',
            'Pause/resume cycles are expected to tear the socket down; treat reopen as the signal to restore subscriptions.'
          ]
        }
      ],
      seeAlso: [
        'oro-ipc(7)',
        'oro-agent-workflows(7)',
        'oro-conduit(3)',
        'oro-dgram(3)',
        'oro-ai-chat(3)'
      ]
    }),
    createManPage({
      name: 'oro-agent-workflows',
      summary:
        'practical guidance for humans and AI agents working with Oro APIs',
      description: [
        'A productive Oro workflow starts by choosing the right layer: CLI for inspection and packaging, man3 for public API contracts, man7 for concepts and route catalogs, and raw IPC only for advanced integrations or deep debugging.',
        'This guide is written for app authors, tool builders, power users, and AI agents that need to move quickly without violating the runtime’s abstractions.'
      ],
      sections: [
        {
          title: 'WORKING ORDER',
          bullets: [
            'Start with the public module in section 3. If the wrapper already exists, use it.',
            'Use the route catalog in section 7 only when you need to understand the bridge contract behind a public API.',
            'Drop to low-level IPC details only after you know the owning module, route prefix, and transport family.'
          ]
        },
        {
          title: 'DISCOVERY RECIPES',
          code: [
            'oroc help ios signing',
            'oroc config --describe webview.root',
            'man 3 oro-ipc',
            'man 3 oro-window',
            'man 3 oro-application',
            'man 7 oro-ipc',
            'man 7 oro-api-concepts',
            'man 7 oro-ipc-routes',
            'man 1 oroc'
          ]
        },
        {
          title: 'HUMAN OPERATOR FLOW',
          bullets: [
            'Start with `oroc help <query>` or `man 1 oroc` when the task is operational: build, package, config inspection, update tooling, or device selection.',
            'Move to section 3 when you need exact function signatures, options objects, and return values.',
            'Move to section 7 when you need conceptual guidance, route inventories, or workflow maps that connect several modules.',
            'Only drop to low-level IPC details once the owning module and transport family are already known.'
          ]
        },
        {
          title: 'AI AGENT MCP FLOW',
          bullets: [
            'Initialize first, then inspect `workspace_info` before assuming config files or docs exist.',
            'Use `search_docs` when the topic is known but the exact file is not.',
            'Use `resources/list` to discover advertised docs from the workspace and installed runtime distribution.',
            'Use specialized MCP tools such as config/build/version helpers before falling back to generic CLI execution.',
            'Treat `run_cli` as a fallback, not the default discovery interface.'
          ]
        },
        {
          title: 'CHOOSING A TRANSPORT',
          bullets: [
            'If the API is synchronous today, preserve that only when compatibility requires it.',
            'If the route expects or returns bytes, use the binary-capable helper rather than layering ad hoc encoding on top of send().',
            'If the feature streams or pushes events, consider Conduit before adding more request polling.'
          ]
        },
        {
          title: 'APPLICATION CHANGE WORKFLOW',
          bullets: [
            'Find the public wrapper first and verify whether the requested behavior belongs there or in a lower-level service.',
            'Identify the ownership key early: window index, origin, scope, route family, bundle identifier, or config path.',
            'Prefer public modules and CLI/config workflows over raw route calls in application code.',
            'When you add an app-local abstraction, document platform limits, config expectations, and degraded behavior explicitly.'
          ]
        },
        {
          title: 'DEBUGGING CHECKLIST',
          bullets: [
            'Confirm the route name and transport family from the catalog before debugging payload shape.',
            'Inspect result.err, headers, and responseType before assuming a failure is in native code.',
            'Check platform capability docs for DBus, XPC, secure storage, AI, Bluetooth, or mobile-only surfaces.',
            'When a route is routed through a helper constant or wrapper, debug the wrapper and the final IPC primitive together.'
          ]
        },
        {
          title: 'SHIPPING CHECKLIST',
          bullets: [
            'Verify that your app docs, CLI usage, config examples, and supported-platform claims all agree with the installed runtime docs you are shipping against.',
            'Check the rendered manpages and generated references that ship with the runtime instead of assuming a behavior from memory.',
            'Release readiness depends on more than code. Cross-platform capability claims, config guidance, and operator workflows must all agree.'
          ]
        },
        {
          title: 'NEED TO KNOW',
          bullets: [
            'Section 3 documents stable public modules. Section 7 documents concepts, protocols, and generated route inventories.',
            'The installed manuals describe the contract of the runtime build you are actually using. Re-check them when you change runtime versions.',
            'Some dynamic dispatch sites remain harder to inventory statically. Treat the unresolved-site note in the route catalog as a signal that low-level interpretation may still be required.'
          ]
        }
      ],
      seeAlso: [
        'oro-ipc(7)',
        'oro-ipc-routes(7)',
        'oro-api-concepts(7)',
        'oro-ipc(3)',
        'oroc(1)'
      ]
    })
  ]
}
