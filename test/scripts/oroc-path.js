import { spawnSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import path from 'node:path'

export function runtimeHostArchitecture (architecture = process.arch) {
  switch (architecture) {
    case 'x64':
    case 'x86-64':
    case 'x86_64':
      return 'x86_64'
    case 'arm64':
    case 'aarch64':
      return 'arm64'
    default:
      return architecture
  }
}

export function resolveOrocExecutable (repoRoot, env = process.env) {
  if (env.ORO_BIN) return env.ORO_BIN

  const executable = process.platform === 'win32' ? 'oroc.exe' : 'oroc'
  const local = path.join(
    repoRoot,
    'build',
    `${runtimeHostArchitecture()}-desktop`,
    'bin',
    executable
  )

  if (existsSync(local)) return local

  const command = process.platform === 'win32' ? 'where' : 'which'
  const result = spawnSync(command, ['oroc'], { stdio: 'ignore' })
  return result.status === 0 ? 'oroc' : local
}
