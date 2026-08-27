import { readFileSync } from 'node:fs'

const targets = [
  'bin/install.sh',
  'bin/install.ps1',
  'bin/android-functions.sh',
  'bin/functions.sh',
  'bin/fetch-sqlite.sh'
]

const forbiddenPatterns = [
  {
    pattern: /git@github\.com:/g,
    message: 'found SSH GitHub URL default'
  },
  {
    pattern: /ssh:\/\/git@github\.com\//g,
    message: 'found SSH GitHub URL default'
  },
  {
    pattern: /git clone --depth=1 --recurse-submodules/g,
    message: 'found recursive submodule clone; normalize .gitmodules URLs before running submodule update so public deps do not require SSH credentials'
  }
]

let failures = 0

for (const target of targets) {
  const source = readFileSync(new URL(`../${target}`, import.meta.url), 'utf8')

  for (const { pattern, message } of forbiddenPatterns) {
    let match = pattern.exec(source)
    pattern.lastIndex = 0

    while (match) {
      failures += 1
      const before = source.slice(0, match.index)
      const line = before.split('\n').length
      console.error(
        `[dep-url-check] ${target}:${line}: ${message} "${match[0]}". Use an HTTPS default so CI and downstream builds work without SSH credentials.`
      )
      match = pattern.exec(source)
    }

    pattern.lastIndex = 0
  }
}

const installSh = readFileSync(new URL('../bin/install.sh', import.meta.url), 'utf8')

if (!installSh.includes('bin/collect-cargo-licenses.js')) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.sh: Cargo dependency license collection is missing.'
  )
}

if (!/CARGO_NDK_VERSION:-[0-9]+\.[0-9]+\.[0-9]+/.test(installSh)) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.sh: cargo-ndk is missing a pinned default version.'
  )
}

if (!/cargo install cargo-ndk --version "\$cargo_ndk_version" --locked/.test(installSh)) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.sh: cargo-ndk installation must use its pinned version and lockfile.'
  )
}

if (!/cargo build --release --locked/.test(installSh)) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.sh: oro-iroh must build from its committed Cargo.lock.'
  )
}

const installPs1 = readFileSync(new URL('../bin/install.ps1', import.meta.url), 'utf8')
const authenticodeCalls = installPs1.match(
  /Confirm-AuthenticodeInstaller "\$env:TEMP\\\$installer"/g
) || []
if (authenticodeCalls.length !== 5) {
  failures += 1
  console.error(
    `[dep-url-check] bin/install.ps1: expected 5 downloaded Windows installer signature checks, found ${authenticodeCalls.length}.`
  )
}

if (/curl\.exe\s+(?![^\r\n]*--fail)/.test(installPs1)) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.ps1: curl downloads must fail on HTTP errors.'
  )
}

const pinnedGitDependencies = [
  'LIBSODIUM',
  'ZLIB',
  'JSONCONS',
  'LIBUV',
  'LIBUSB',
  'ASN1C',
  'LIBIPFS',
  'CRSQLITE',
  'MBEDTLS',
  'LLAMA',
  'WHISPER',
  'IROH'
]

for (const dependency of pinnedGitDependencies) {
  const revision = new RegExp(
    `${dependency}_GIT_REVISION="\\$\\{${dependency}_GIT_REVISION:-[0-9a-f]{40}\\}"`
  )
  if (!revision.test(installSh)) {
    failures += 1
    console.error(
      `[dep-url-check] bin/install.sh: ${dependency} is missing an immutable 40-character default revision.`
    )
  }
}

const cloneCalls = installSh.match(/git clone --depth=1/g) || []
if (cloneCalls.length !== 1 || !installSh.includes('function _clone_pinned_dependency')) {
  failures += 1
  console.error(
    '[dep-url-check] bin/install.sh: network Git dependencies must use _clone_pinned_dependency so tags are verified before use.'
  )
}

const functionsSh = readFileSync(
  new URL('../bin/functions.sh', import.meta.url),
  'utf8'
)
if (/download_to_tmp "\$uri"\)/.test(functionsSh + installSh)) {
  failures += 1
  console.error(
    '[dep-url-check] dependency download is missing a pinned checksum.'
  )
}

const sqliteFetcher = readFileSync(
  new URL('../bin/fetch-sqlite.sh', import.meta.url),
  'utf8'
)
if (!/EXPECTED_SHA256="[0-9a-f]{64}"/.test(sqliteFetcher)) {
  failures += 1
  console.error(
    '[dep-url-check] bin/fetch-sqlite.sh: SQLite download is missing a pinned SHA-256 checksum.'
  )
}

const requiredGuards = [
  {
    label: 'cr-sqlite submodule normalization',
    pattern: /_rewrite_github_submodule_urls "\$BUILD_DIR\/cr-sqlite"[\s\S]{0,300}?submodule update --init --recursive/
  },
  {
    label: 'mbedtls submodule normalization',
    pattern: /_rewrite_github_submodule_urls "\$BUILD_DIR\/mbedtls"[\s\S]{0,300}?submodule update --init --recursive/
  }
]

for (const { label, pattern } of requiredGuards) {
  if (!pattern.test(installSh)) {
    failures += 1
    console.error(
      `[dep-url-check] bin/install.sh: missing ${label}. Public submodule fetches must normalize GitHub SSH URLs before recursive updates.`
    )
  }
}

if (failures > 0) {
  process.exit(1)
}

console.log('[dep-url-check] OK')
