import fs from 'node:fs'
import path from 'node:path'

/**
 * @typedef {object} NpmSmokeInstallOptions
 * @property {string} project Consumer project directory.
 * @property {string} prefix npm installation prefix.
 * @property {string[]} tarballs Absolute paths to the package archives.
 * @property {boolean} globalInstall Select a global installation.
 * @property {(args: string[], cwd: string) => void} npm npm command runner.
 */

/**
 * Install the exact package family into an isolated npm smoke consumer.
 * @param {NpmSmokeInstallOptions} options
 * @returns {void}
 */
export function installSmokePackages ({ project, prefix, tarballs, globalInstall, npm }) {
  const scripts = {
    cli: 'oroc',
    build: 'oroc build --prod -V',
    resources: 'oroc print-build-dir --prod'
  }
  const platformSource = `file:${tarballs[0].split(path.sep).join('/')}`
  const allowScripts = { [platformSource]: true }
  fs.writeFileSync(path.join(project, 'package.json'), JSON.stringify({ private: true, scripts, allowScripts }))
  npm([
    'install', ...(globalInstall ? ['--global', `--allow-scripts=${platformSource}`] : []), '--prefix', prefix,
    '--offline', '--no-package-lock', '--no-save', '--no-audit', '--no-fund', '--ignore-scripts=false', '--foreground-scripts', ...tarballs
  ], project)
}
