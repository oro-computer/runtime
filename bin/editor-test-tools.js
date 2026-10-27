import fs from 'node:fs'
import path from 'node:path'

/**
 * @typedef {object} EditorBackends
 * @property {string} [clangd] Selected clangd executable.
 * @property {string} [sourcekit-lsp] Selected SourceKit-LSP executable.
 */

/**
 * Expose selected POSIX editor backends without their compiler/linker siblings.
 * @param {string} directory Existing temporary installation directory.
 * @param {EditorBackends} backends
 * @returns {string|null} Isolated PATH directory, or null when no backends are selected.
 */
export function stageEditorBackends (directory, backends) {
  const entries = Object.entries(backends).map(([name, executable]) => {
    if (!['clangd', 'sourcekit-lsp'].includes(name)) throw new Error(`Unsupported editor backend: ${name}`)
    const target = fs.realpathSync(executable)
    if (!fs.statSync(target).isFile()) throw new Error(`Editor backend is not a file: ${target}`)
    fs.accessSync(target, fs.constants.X_OK)
    return [name, target]
  })
  if (!entries.length) return null
  const bin = fs.mkdtempSync(path.join(directory, 'editor-backends-'))
  for (const [name, executable] of entries) fs.symlinkSync(executable, path.join(bin, name))
  return bin
}
