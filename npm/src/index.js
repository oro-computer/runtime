import path from 'node:path'
import os from 'node:os'
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'
import { spawn } from 'node:child_process'

const dirname = path.dirname(fileURLToPath(import.meta.url))

export const ORO_HOME = path.dirname(dirname)
export const PREFIX = ORO_HOME

export const platform = os.platform()
export const arch = os.arch()

const envFilename = process.env.ORO_ENV_FILENAME || '.oro.env'
const configHome = process.env.ORO_CONFIG_HOME || (
  platform === 'win32'
    ? path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'oro')
    : platform === 'darwin'
      ? path.join(os.homedir(), 'Library', 'Application Support', 'oro')
      : path.join(process.env.XDG_CONFIG_HOME || path.join(os.homedir(), '.config'), 'oro')
)
const userEnvPath = path.resolve(configHome, path.basename(envFilename))
const legacyEnvPath = path.resolve(ORO_HOME, envFilename)
const selectedEnvPath = process.env.ORO_ENV_FILE
  ? path.resolve(process.env.ORO_ENV_FILE)
  : userEnvPath

export const env = {
  ORO_HOME,
  PREFIX,
  ORO_ENV_FILE: selectedEnvPath
}

const binaryName = os.platform() === 'win32' ? 'oroc.exe' : 'oroc'

export const bin = {
  'oroc-platform': path.join(env.PREFIX, 'bin', 'oroc-platform.js'),
  oroc: path.join(env.PREFIX, 'bin', binaryName)
}

// Modifying this function requires full FTE testing (From fresh install) on all build OS's before signing off (Individual arch testing not required)
export const firstTimeExperienceSetup = async () => {
  const installPath = path.dirname(path.dirname(bin.oroc))
  if (fs.existsSync(env.ORO_ENV_FILE) || (!process.env.ORO_ENV_FILE && fs.existsSync(legacyEnvPath))) {
    return true
  }

  // env file doesn't exist, attempt to run setup for target being built.
  // This should also install vc_redist if it hasn't been installed
  const PLATFORM_PARAMETER = '--platform'
  let isSetupCall = false
  let isBuildCall = false
  let isRunCall = false

  let platform = ''
  const argv = process.argv.slice(2)
  argv.forEach((arg, index) => {
    if (arg === 'setup') {
      isSetupCall = true
    }

    if (arg === 'build') {
      isBuildCall = true
    }

    if (arg === 'run') {
      isRunCall = true
    }

    if (arg.indexOf(PLATFORM_PARAMETER + '=') === 0) {
      platform = arg.slice(PLATFORM_PARAMETER.length + 1)
    } else if (arg.indexOf(PLATFORM_PARAMETER) === 0) {
      platform = argv[index + 1]
    }
  })

  if (platform === 'android-emulator') {
    platform = 'android'
  }

  if (!platform && (isBuildCall || isRunCall)) {
    platform = os.platform() === 'win32' ? 'windows' : os.platform()
  }

  const startInfo = {
    env: { ...env, ...process.env, ORO_ENV_FILE: env.ORO_ENV_FILE },
    cwd: installPath,
    stdio: [process.stdin, process.stdout, process.stderr]
  }

  const spawnArgs = []
  if (isSetupCall || isBuildCall || isRunCall) {
    if (os.platform() === 'win32') {
      spawnArgs.push(
        // @ts-ignore
        'powershell.exe',
        [
          '.\\bin\\install.ps1',
          `-fte:${platform.length > 0 ? platform : 'all'}`
        ],
        startInfo
      )
    } else if (isSetupCall || isBuildCall || isRunCall) {
      spawnArgs.push(
        // @ts-ignore
        './bin/functions.sh',
        ['--fte', platform],
        startInfo
      )
    }
  }

  if (spawnArgs.length === 0) {
    return true
  }

  if (isSetupCall) {
    startInfo.env.VERBOSE = '1'
  }

  if (isSetupCall || isBuildCall || isRunCall) {
    console.log('# Checking build dependencies...')
  }

  const preferredEnvPath = env.ORO_ENV_FILE
  fs.mkdirSync(path.dirname(preferredEnvPath), { recursive: true, mode: 0o700 })

  // @ts-ignore
  const child = spawn(...spawnArgs)

  const exitCode = await new Promise((resolve, reject) => {
    // @ts-ignore
    child.on('close', resolve).on('error', reject)
  })

  if (exitCode !== 0) {
    // This path did not exist before setup, so a file here contains only
    // configuration written by the failed child.
    if (fs.existsSync(preferredEnvPath)) {
      fs.unlinkSync(preferredEnvPath)
    }
    throw new Error(`Oro dependency setup failed with exit code ${exitCode}`)
  }

  // If fte didn't create a configuration file, make an empty one to prevent
  // user being prompted again
  if (!fs.existsSync(preferredEnvPath)) {
    try {
      fs.writeFileSync(preferredEnvPath, '', { flag: 'wx', mode: 0o600 })
    } catch (err) {
      if (err.code !== 'EEXIST') throw err
    }
  }

  return !isSetupCall
}

export default {
  platform,
  arch,
  env,
  bin,
  firstTimeExperienceSetup
}
