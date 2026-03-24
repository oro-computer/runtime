import { test } from 'oro:test'
import process from 'oro:process'
import dns from 'oro:dns'
import os from 'oro:os'

// node compat
// import dns from 'node:dns'

const IPV4_REGEX =
  /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/
const IPV6_REGEX =
  /^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$/

const isOnline = Boolean(
  globalThis?.navigator?.onLine || process?.versions?.node
)
const LOCALHOST = 'localhost'

test('dns exports', (t) => {
  t.ok(typeof dns.lookup === 'function', 'lookup is available')
  t.ok(
    typeof dns.promises.lookup === 'function',
    'promises.lookup is available'
  )
  t.equal(typeof dns.ALL, 'number', 'ALL constant exported')
  t.equal(dns.constants.ALL, dns.ALL, 'constants.ALL matches ALL')
  t.equal(dns.promises.ALL, dns.ALL, 'promise namespace exposes ALL')
})

test('dns.lookup executes asynchronously', async (t) => {
  await new Promise((resolve, reject) => {
    let sync = true
    dns.lookup(LOCALHOST, (err) => {
      if (err) return reject(err)
      t.equal(sync, false, 'lookup callback defers from caller frame')
      resolve()
    })
    sync = false
  })
})

test('dns.lookup', async (t) => {
  if (!isOnline) {
    return t.comment('skipping offline')
  }

  await Promise.all([
    new Promise((resolve) => {
      dns.lookup('google.com', (err, address, family) => {
        if (err) return t.fail(err)

        const isValidFamily = family === 4 || family === 6

        t.ok(isValidFamily, 'is either IPv4 or IPv6 family')

        const v4 = IPV4_REGEX.test(address)
        const v6 = IPV6_REGEX.test(address)

        t.ok(v4 || v6, 'has valid address')
        resolve()
      })
    }),
    new Promise((resolve) => {
      dns.lookup('example.com', { family: 'IPv4' }, (err, address, family) => {
        if (err) return t.fail(err)
        t.equal(family, 4, 'is IPv4 family')
        t.ok(IPV4_REGEX.test(address), 'has valid IPv4 address')
        resolve()
      })
    }),
    os.platform() !== 'win32' &&
      new Promise((resolve) => {
        dns.lookup('google.com', 6, (err, address, family) => {
          if (err) return t.fail(err)
          t.equal(family, 6, 'is IPv6 family')
          t.ok(IPV6_REGEX.test(address), 'has valid IPv6 address')
          resolve()
        })
      }),
    new Promise((resolve) => {
      dns.lookup(LOCALHOST, { all: true }, (err, addresses) => {
        if (err) return t.fail(err)
        t.ok(Array.isArray(addresses), 'all=true returns an array')
        t.ok(addresses.length > 0, 'returns at least one address')
        for (const entry of addresses) {
          t.equal(typeof entry.address, 'string', 'entry has address')
          t.ok(
            entry.family === 4 || entry.family === 6,
            'entry has valid family'
          )
        }
        resolve()
      })
    })
  ])
})

const BAD_HOSTNAME = 'thisisnotahostname'

test('dns.lookup bad hostname', async (t) => {
  if (!isOnline) {
    return t.comment('skipping offline')
  }

  await new Promise((resolve) => {
    dns.lookup(BAD_HOSTNAME, (err) => {
      t.ok(err instanceof Error, 'returns an error instance')
      t.equal(err.code, 'ENOTFOUND', 'exposes ENOTFOUND code for missing hosts')
      t.equal(err.hostname, BAD_HOSTNAME, 'exposes hostname on error')
      resolve()
    })
  })
})

test('dns.promises.lookup', async (t) => {
  if (!isOnline) {
    return t.comment('skipping offline')
  }

  try {
    const info = await dns.promises.lookup('google.com', 4)
    t.ok(
      info && typeof info === 'object',
      'returns a non-error object after resolving a hostname'
    )
    t.equal(info.family, 4, 'is IPv4 family')
    t.ok(IPV4_REGEX.test(info.address), 'has valid IPv4 address')
  } catch (err) {
    t.fail(err)
  }

  try {
    const info = await dns.promises.lookup('google.com')
    t.ok(
      info && typeof info === 'object',
      'returns object when no family provided'
    )
    t.ok(info.family === 4 || info.family === 6, 'defaults to any family')
  } catch (err) {
    t.fail(err)
  }

  if (os.platform() !== 'win32') {
    try {
      const info = await dns.promises.lookup('google.com', 6)
      t.ok(
        info && typeof info === 'object',
        'returns a non-error object after resolving a hostname'
      )
      t.equal(info.family, 6, 'is IPv6 family')
      t.ok(IPV6_REGEX.test(info.address), 'has valid IPv4 address')
    } catch (err) {
      t.fail(err)
    }
  }

  try {
    const info = await dns.promises.lookup('google.com', { family: 'IPv4' })
    t.ok(
      info && typeof info === 'object',
      'returns a non-error object after resolving a hostname'
    )
    t.equal(info.family, 4, 'is IPv4 family')
    t.ok(IPV4_REGEX.test(info.address), 'has valid IPv4 address')
  } catch (err) {
    t.fail(err)
  }

  if (os.platform() !== 'win32') {
    try {
      const info = await dns.promises.lookup('cloudflare.com', 6)
      t.ok(
        info && typeof info === 'object',
        'returns a non-error object after resolving a hostname'
      )
      t.equal(info.family, 6, 'is IPv6 family')
      t.ok(IPV6_REGEX.test(info.address), 'has valid IPv6 address')
    } catch (err) {
      t.fail(err)
    }
    try {
      const info = await dns.promises.lookup('cloudflare.com', { family: 6 })
      t.ok(
        info && typeof info === 'object',
        'returns a non-error object after resolving a hostname'
      )
      t.equal(info.family, 6, 'is IPv6 family')
      t.ok(IPV6_REGEX.test(info.address), 'has valid IPv6 address')
    } catch (err) {
      t.fail(err)
    }
  }

  try {
    const addresses = await dns.promises.lookup(LOCALHOST, { all: true })
    t.ok(Array.isArray(addresses), 'all=true returns an array')
    t.ok(addresses.length > 0, 'returns at least one address')
    for (const entry of addresses) {
      t.equal(typeof entry.address, 'string', 'address entry has address')
      t.ok(
        entry.family === 4 || entry.family === 6,
        'address entry has valid family'
      )
    }
  } catch (err) {
    t.fail(err)
  }
})

test('dns.promises.lookup bad hostname', async (t) => {
  if (!isOnline) {
    return t.comment('skipping offline')
  }

  try {
    await dns.promises.lookup(BAD_HOSTNAME)
  } catch (err) {
    t.equal(err?.code, 'ENOTFOUND', 'returns ENOTFOUND on unknown hostname')
    t.equal(
      err?.hostname,
      BAD_HOSTNAME,
      'exposes hostname on unknown host error'
    )
  }
})
