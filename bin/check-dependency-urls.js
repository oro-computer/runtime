import { readFileSync } from 'node:fs'

const targets = [
  'bin/install.sh',
  'bin/install.ps1',
  'bin/android-functions.sh',
  'bin/functions.sh'
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
