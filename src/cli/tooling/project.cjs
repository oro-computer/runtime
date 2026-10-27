'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { fileURLToPath } = require('node:url')

const markers = ['oro.toml', 'oro.ini']
const recovery = 'Set the editor rootPatterns to ["oro.toml", "oro.ini"], or pass --config /path/to/oro.toml.'

function search (start, parents) {
  let directory = start
  for (;;) {
    for (const marker of markers) {
      const filename = path.join(directory, marker)
      if (fs.existsSync(filename) && fs.statSync(filename).isFile()) return fs.realpathSync(filename)
    }
    const parent = path.dirname(directory)
    if (!parents || parent === directory) return null
    directory = parent
  }
}

function uriDirectory (uri) {
  try {
    return fileURLToPath(uri)
  } catch (cause) {
    throw new Error(`The editor project root must be a local file URI: ${uri}. ${recovery}`, { cause })
  }
}

/**
 * @typedef {object} ProjectSelection
 * @property {string} [config] Explicit project configuration file.
 * @property {string} [project] Explicit directory containing a project marker.
 */

/**
 * Select one Oro application from explicit arguments or LSP initialization roots.
 * Without editor roots, tooling and legacy clients search upward from cwd.
 * @param {ProjectSelection} options
 * @param {object} [initialization] LSP initialize parameters.
 * @param {string} [cwd] Launch directory, also used for relative CLI arguments.
 * @returns {string} Canonical configuration filename.
 */
function findConfiguration (options, initialization = {}, cwd = process.cwd()) {
  if (options.config) {
    const filename = fs.realpathSync(path.resolve(cwd, options.config))
    if (!markers.includes(path.basename(filename)) || !fs.statSync(filename).isFile()) {
      throw new Error('--config must select an oro.toml or oro.ini file')
    }
    return filename
  }
  if (options.project) {
    const directory = path.resolve(cwd, options.project)
    const configuration = search(directory, false)
    if (!configuration) throw new Error(`No oro.toml or oro.ini in the selected project directory ${directory}. ${recovery}`)
    return configuration
  }
  let directories
  const editorRoot = initialization.rootUri
    ? () => uriDirectory(initialization.rootUri)
    : initialization.rootPath ? () => path.resolve(cwd, initialization.rootPath) : null
  if (initialization.workspaceFolders?.length) {
    directories = initialization.workspaceFolders.map(folder => uriDirectory(folder.uri))
  } else if (editorRoot) {
    directories = [editorRoot()]
  } else {
    directories = [cwd]
  }
  directories = [...new Set(directories.map(directory => path.resolve(directory)))]
  const configurations = [...new Set(directories.map(directory => search(directory, true)).filter(Boolean))]
  // Coc can send an enclosing workspace alongside a more specific client root.
  if (!configurations.length && initialization.workspaceFolders?.length && editorRoot) {
    const directory = path.resolve(editorRoot())
    if (!directories.includes(directory)) {
      directories.push(directory)
      const configuration = search(directory, true)
      if (configuration) configurations.push(configuration)
    }
  }
  if (configurations.length > 1) {
    throw new Error(`The editor workspace contains multiple Oro projects: ${configurations.join(', ')}. Run one server per application and select it with --config.`)
  }
  if (!configurations.length) {
    throw new Error(`No oro.toml or oro.ini found in or above ${directories.join(', ')} (launch directory: ${cwd}). ${recovery}`)
  }
  return configurations[0]
}

module.exports = { findConfiguration }
