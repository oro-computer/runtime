const fs = require('node:fs')
const path = require('node:path')

/** Generate only fixed SDK adapter subclasses and manifest entries from bundled data. */
function generate (filename, output, activity, mode = 'generated') {
  if (!['generated', 'external'].includes(mode)) throw new Error('Migration adapter mode must be generated or external')
  if (mode === 'external' && fs.existsSync(path.join(output, 'java', 'oro-migration'))) throw new Error('External adapter mode requires an output without previously generated adapters')
  const bytes = fs.readFileSync(filename)
  if (bytes.length > 65536) throw new Error('Migration definitions exceed 64 KiB')
  const policy = JSON.parse(bytes.toString('utf8'))
  if (policy.schema !== 1 || Object.keys(policy).some(key => !['schema', 'protected', 'components'].includes(key)) ||
      !Array.isArray(policy.protected) || !Array.isArray(policy.components) || policy.protected.length > 16 || policy.components.length > 16) {
    throw new Error('Migration schema 1 requires bounded protected and components arrays')
  }
  const profiles = new Set()
  const identifier = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,128}$/.test(value)
  for (const source of policy.protected) {
    if (source.exposure !== undefined && (source.exposure !== 'private-document' || source.authentication !== 'none')) throw new Error('Only declared noninteractive sources can expose private document reads')
    if (source.format === 'prefer-native-records-v1') {
      if (!identifier(source.id) || profiles.has(source.id) || !identifier(source.primary) || !identifier(source.fallback) ||
          source.authentication !== 'none' || !['biometric', 'biometric-or-device'].includes(source.policy) ||
          Object.keys(source).some(key => !['id', 'format', 'primary', 'fallback', 'authentication', 'policy', 'exposure'].includes(key))) throw new Error('Invalid fixed historical source selection')
      profiles.add(source.id); continue
    }
    if (source.format === 'preferences-json') {
      if (!identifier(source.id) || profiles.has(source.id) || source.authentication !== 'none' ||
          !['biometric', 'biometric-or-device'].includes(source.policy) || !/^[A-Za-z0-9_.-]{1,128}$/.test(source.preferences) ||
          typeof source.key !== 'string' || !source.key || source.key.length > 256 ||
          Object.keys(source).some(key => !['id', 'format', 'preferences', 'key', 'authentication', 'policy', 'exposure'].includes(key))) throw new Error('Invalid declared JSON preference source')
      profiles.add(source.id); continue
    }
    if (source.format === 'native-records-v1') {
      const records = source.records
      if (!identifier(source.id) || profiles.has(source.id) || source.authentication !== 'none' || !['biometric', 'biometric-or-device'].includes(source.policy) ||
          typeof source.key !== 'string' || !source.key || source.key.length > 256 ||
          Object.keys(source).some(key => !['id', 'format', 'records', 'key', 'authentication', 'policy', 'exposure', 'provenance'].includes(key)) ||
          !records || Object.keys(records).some(key => !['namespace', 'identity'].includes(key)) ||
          !/^[a-z][a-z0-9_.-]{0,63}$/.test(records.namespace) || records.namespace.startsWith('oro.') ||
          !records.identity || Object.keys(records.identity).sort().join(',') !== 'accountRef,authorityRef,identityRef') throw new Error('Invalid fixed native record migration source')
      for (const selector of Object.values(records.identity)) {
        if (typeof selector === 'string') {
          if (!selector || selector.includes('{sourceRef}') || /[{}]/.test(selector.replaceAll('{package}', 'application'))) throw new Error('Native record identities must select a fixed scope')
        } else if (!selector || Object.keys(selector).some(key => !['preferences', 'key', 'default'].includes(key)) ||
            !/^[A-Za-z0-9_.-]{1,128}$/.test(selector.preferences) || typeof selector.key !== 'string' || !selector.key ||
            typeof selector.default !== 'string' || !selector.default) throw new Error('Invalid fixed native record identity selector')
      }
      profiles.add(source.id)
      continue
    }
    if (!identifier(source.id) || profiles.has(source.id) || !/^[A-Za-z0-9_.-]{1,128}$/.test(source.preferences) ||
        ![undefined, 'aes-gcm'].includes(source.format) ||
        Object.keys(source).some(key => !['id', 'format', 'preferences', 'key', 'alias', 'aad', 'encoding', 'ciphertextField', 'authentication', 'policy', 'exposure'].includes(key)) ||
        !['base64', 'base64url'].includes(source.encoding) || !['none', 'biometric', 'biometric-or-device'].includes(source.authentication) ||
        !['biometric', 'biometric-or-device'].includes(source.policy) || !/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(source.ciphertextField) ||
        ['key', 'alias', 'aad'].some(key => typeof source[key] !== 'string' || !source[key] || Buffer.byteLength(source[key]) > 1024 ||
          [...source[key]].some(character => character.charCodeAt(0) < 32) || /[{}]/.test(source[key].replaceAll('{package}', 'application').replaceAll('{sourceRef}', 'reference'))) ||
        source.alias.startsWith('oro.')) throw new Error('Invalid fixed protected migration source')
    profiles.add(source.id)
  }
  const byId = new Map(policy.protected.map(source => [source.id, source]))
  const family = source => {
    if (typeof source.key !== 'string' || !source.key.endsWith('{sourceRef}') ||
        source.key.indexOf('{sourceRef}') !== source.key.lastIndexOf('{sourceRef}') || source.key === '{sourceRef}' ||
        /[{}]/.test(source.key.replaceAll('{package}', 'application').replace('{sourceRef}', 'reference'))) throw new Error('Source selection requires a fixed nonempty key prefix')
    return source.key.slice(0, -'{sourceRef}'.length).replaceAll('{package}', 'application')
  }
  for (const source of policy.protected) {
    if (source.provenance !== undefined) {
      const guard = source.provenance
      const legacy = byId.get(guard?.legacy)
      if (source.format !== 'native-records-v1' || !guard ||
          Object.keys(guard).some(key => !['legacy', 'markerKey', 'format', 'formatPointer', 'digestPointer', 'digestFormat'].includes(key)) ||
          !legacy || ![undefined, 'aes-gcm'].includes(legacy.format) || legacy.authentication !== 'none' ||
          guard.digestFormat !== 'android-json-pairs-sha256-v1' || !Number.isInteger(guard.format) || guard.format < 1 || guard.format > 2147483647 ||
          typeof guard.markerKey !== 'string' || !/^[A-Za-z0-9_.:-]{1,128}$/.test(guard.markerKey) ||
          ['formatPointer', 'digestPointer'].some(key => typeof guard[key] !== 'string' || Buffer.byteLength(guard[key]) > 256 || !/^\/[^/~]+(?:\/[^/~]+)*$/.test(guard[key])) ||
          guard.formatPointer === guard.digestPointer) throw new Error('Invalid fixed legacy provenance guard')
      family(legacy)
      if (guard.markerKey.startsWith(family(source))) throw new Error('Provenance marker must be outside the selected record family')
    }
    if (source.format === 'prefer-native-records-v1') {
      const primary = byId.get(source.primary); const fallback = byId.get(source.fallback)
      if (primary?.format !== 'native-records-v1' || primary.provenance?.legacy !== fallback?.id ||
          !fallback || ![undefined, 'aes-gcm'].includes(fallback.format) ||
          [primary, fallback].some(child => child.authentication !== 'none' || child.policy !== source.policy)) throw new Error('Source selection requires a guarded native primary and its fixed legacy fallback')
      family(primary); family(fallback)
    }
  }
  const classes = new Set()
  const ids = new Set()
  let manifest = ''
  const generated = []
  for (const component of policy.components) {
    const name = component.className
    if (!identifier(component.id) || ids.has(component.id) || typeof name !== 'string' || name.length > 256 ||
        !/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)+$/.test(name) ||
        /^(oro\.runtime|android|androidx|java|javax|kotlin)\./.test(name) || name === activity || classes.has(name) ||
        !['receiver', 'job', 'service', 'activity'].includes(component.type)) throw new Error('Invalid or duplicate retained component identity')
    classes.add(name); ids.add(component.id)
    const allowed = ['id', 'className', 'type', 'actions', 'source', 'referenceExtra', 'referencePointer', 'generationExtra', 'generationPointer',
      'tokenExtra', 'tokenPointer', 'senderPointer', 'senderPreferences', 'senderKey', 'identityFields', 'checks', 'minimumJobId', 'maximumJobId', 'targetActivity', 'referenceExtras']
    if (Object.keys(component).some(key => !allowed.includes(key))) throw new Error('Unknown retained component option')
    if (['receiver', 'job'].includes(component.type)) {
      const source = policy.protected.find(source => source.id === component.source)
      if (!source || source.authentication !== 'none' || typeof component.referenceExtra !== 'string' ||
          typeof component.referencePointer !== 'string' || !component.identityFields ||
          Object.keys(component.identityFields).sort().join(',') !== 'accountRef,authorityRef,targetRef') throw new Error('Retained dispatch requires a fixed noninteractive source and identity mapping')
      for (const mapping of Object.values(component.identityFields)) {
        if (Object.keys(mapping).some(key => !['pointer', 'encoding'].includes(key)) || typeof mapping.pointer !== 'string' ||
            !/^\/(?:[^/~]+\/)*[^/~]+$/.test(mapping.pointer) || mapping.pointer.length > 256 ||
            !['text', 'sha256'].includes(mapping.encoding)) throw new Error('Invalid retained identity mapping')
      }
    }
    if (['receiver', 'service'].includes(component.type) && (!Array.isArray(component.actions) || component.actions.length < 1 || component.actions.length > 8 ||
        component.actions.some(action => typeof action !== 'string' || !/^[A-Za-z][A-Za-z0-9_.]{1,159}$/.test(action)) ||
        new Set(component.actions).size !== component.actions.length)) throw new Error('Retained actions must be a fixed bounded allowlist')
    if (component.type === 'receiver' && (typeof component.tokenExtra !== 'string' || typeof component.tokenPointer !== 'string' ||
        Boolean(component.senderPointer) === Boolean(component.senderPreferences))) throw new Error('Retained receivers require fixed token and sender mapping')
    if (component.type === 'job' && (!Number.isInteger(component.minimumJobId) || !Number.isInteger(component.maximumJobId) ||
        component.minimumJobId < 0 || component.maximumJobId < component.minimumJobId || component.maximumJobId - component.minimumJobId >= 32)) throw new Error('Retained job range exceeds 32 identities')
    if (component.checks !== undefined && (!Array.isArray(component.checks) || component.checks.length > 8 || component.checks.some(check =>
      !check || Object.keys(check).some(key => !['pointer', 'equals', 'default'].includes(key)) || typeof check.pointer !== 'string' ||
      !check.pointer.startsWith('/') || !['string', 'boolean', 'number'].includes(typeof check.equals) ||
      (Object.hasOwn(check, 'default') && check.default !== check.equals)))) throw new Error('Invalid fixed source checks')
    if (component.type === 'activity') {
      if (Object.keys(component).some(key => !['id', 'type', 'className', 'targetActivity', 'actions', 'referenceExtras'].includes(key))) throw new Error('Invalid retained activity option')
      if (component.actions !== undefined && (!Array.isArray(component.actions) || component.actions.length < 1 || component.actions.length > 8 ||
          component.actions.some(action => typeof action !== 'string' || !/^[A-Za-z][A-Za-z0-9_.]{1,159}$/.test(action)) ||
          !Array.isArray(component.referenceExtras) || component.referenceExtras.length < 1 || component.referenceExtras.length > 8 ||
          component.referenceExtras.some(key => typeof key !== 'string' || !/^[A-Za-z][A-Za-z0-9_.]{0,63}$/.test(key)))) throw new Error('Invalid fixed historical routing references')
      if (component.targetActivity !== activity) throw new Error('A retained activity alias must target the configured main Activity')
      manifest += `<activity-alias android:name="${name}" android:targetActivity="${activity}" android:exported="true" />\n`
      continue
    }
    if (component.type === 'service' && Object.keys(component).some(key => !['id', 'type', 'className', 'actions'].includes(key))) throw new Error('Invalid retained service option')
    const offset = name.lastIndexOf('.')
    const parent = component.type === 'job'
      ? `RetainedJobService("${component.id}")`
      : component.type === 'receiver'
        ? `RetainedReceiver("${component.id}")`
        : 'RetainedImportanceService()'
    const source = `// Generated Oro migration adapter.\npackage ${name.slice(0, offset)}\n\nclass ${name.slice(offset + 1)} : oro.runtime.app.${parent}\n`
    const destination = path.join(output, 'java', 'oro-migration', name.replaceAll('.', '/') + '.kt')
    if (mode === 'generated' && fs.existsSync(destination) && !fs.readFileSync(destination, 'utf8').startsWith('// Generated Oro migration adapter.\n')) throw new Error('Refusing to replace a non-generated migration source')
    generated.push([destination, source])
    if (component.type === 'job') manifest += `<service android:name="${name}" android:exported="true" android:permission="android.permission.BIND_JOB_SERVICE" />\n`
    else {
      const tag = component.type === 'receiver' ? 'receiver' : 'service'
      manifest += `<${tag} android:name="${name}" android:exported="true"><intent-filter>` +
        component.actions.map(action => `<action android:name="${action}" />`).join('') + `</intent-filter></${tag}>\n`
    }
  }
  if (mode === 'generated') {
    for (const [destination, source] of generated) {
      fs.mkdirSync(path.dirname(destination), { recursive: true })
      fs.writeFileSync(destination, source)
    }
  }
  fs.mkdirSync(path.join(output, 'assets'), { recursive: true })
  fs.writeFileSync(path.join(output, 'assets', 'oro-migration.json'), JSON.stringify(policy))
  return mode === 'generated' ? manifest : ''
}

module.exports = { generate }
if (require.main === module) {
  try { process.stdout.write(generate(...process.argv.slice(2))) } catch (error) {
    process.stderr.write(error.message + '\n'); process.exitCode = 1
  }
}
