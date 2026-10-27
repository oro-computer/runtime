'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { fileURLToPath, pathToFileURL } = require('node:url')

function canonicalPath (file) {
  let resolved
  try { resolved = fs.realpathSync.native(file) } catch {
    const parent = path.dirname(file)
    resolved = parent === file ? file : path.join(canonicalPath(parent), path.basename(file))
  }
  return process.platform === 'win32' ? resolved.replace(/^([a-z]):/i, (_, drive) => `${drive.toUpperCase()}:`) : resolved
}

function canonical (uri) {
  try { return pathToFileURL(canonicalPath(fileURLToPath(uri))).href } catch { return uri }
}

/**
 * Constrain diagnostics and edits to original sources, preserving client URIs.
 * @param {object} model Exported compiler model.
 * @returns {object} Scope validation and message transformation functions.
 */
function scopeGuard (model) {
  const sources = model.originalSources.map(canonicalPath)
  const originals = new Set(sources.map(file => canonical(pathToFileURL(file).href)))
  const aliases = new Map(model.originalSources.map(file => {
    const uri = pathToFileURL(file).href
    return [canonical(uri), uri]
  }))
  const parents = new Set(sources.map(file => path.dirname(file)))
  const headerRoots = (model.headerRoots || []).map(canonicalPath)
  const projectRoot = canonicalPath(model.projectRoot)
  const cacheRoot = model.generation && canonicalPath(path.dirname(path.dirname(model.generation)))
  const accepts = uri => {
    uri = canonical(uri)
    if (originals.has(uri)) return true
    try {
      const file = fileURLToPath(uri)
      if (model.language === 'cpp' && /\.(h|hh|hpp|hxx)$/.test(file)) {
        if (parents.has(path.dirname(file))) return true
        if (headerRoots.some(root => file.startsWith(root + path.sep))) return true
      }
      const relative = path.relative(projectRoot, file)
      return model.sourceMode === 'inferred' && relative && !relative.startsWith('..') && !path.isAbsolute(relative) && !relative.split(path.sep).some(part => ['node_modules', '.git', '.oro-tooling'].includes(part)) && !(cacheRoot && file.startsWith(cacheRoot + path.sep))
    } catch { return false }
  }
  const assertEdit = uri => {
    if (!accepts(uri)) throw new Error(`Backend attempted to edit a file outside the declared source scope: ${uri}`)
  }
  const workspaceEdit = edit => {
    for (const uri of Object.keys(edit.changes || {})) assertEdit(uri)
    for (const change of edit.documentChanges || []) {
      if (change.textDocument) assertEdit(change.textDocument.uri)
      else {
        if (change.oldUri) assertEdit(change.oldUri)
        if (change.kind === 'delete') assertEdit(change.uri)
        const destination = change.newUri || (change.kind === 'create' && change.uri)
        if (destination && !accepts(destination)) {
          let parent
          try { parent = path.dirname(fileURLToPath(canonical(destination))) } catch {}
          if (!parents.has(parent)) throw new Error('File operation destination must be in a declared source directory')
        }
      }
    }
  }
  const validate = (message, method = message.method) => {
    // Opaque backend data may contain unrelated fields called "changes", such
    // as TypeScript's FileTextChanges array. Validate LSP edit fields by method.
    if (method === 'workspace/applyEdit') workspaceEdit(message.params.edit)
    else if (method === 'textDocument/rename' && message.result) workspaceEdit(message.result)
    else if (method === 'textDocument/codeAction') {
      for (const action of message.result || []) if (action.edit) workspaceEdit(action.edit)
    } else if (method === 'codeAction/resolve' && message.result?.edit) workspaceEdit(message.result.edit)
  }
  const clientUri = value => aliases.get(value) || (value.startsWith('file:') && aliases.get(canonical(value))) || value
  const remap = value => {
    if (!value || typeof value !== 'object') return value
    if (Array.isArray(value)) return value.map(remap)
    return Object.fromEntries(Object.entries(value).map(([key, child]) => [clientUri(key), typeof child === 'string' ? clientUri(child) : remap(child)]))
  }
  return {
    accepts,
    validate,
    outgoing: remap,
    transform (message) {
      const document = message.params?.textDocument
      if (document?.uri) {
        const uri = canonical(document.uri)
        if (!accepts(uri)) {
          if (message.id == null) return []
          throw new Error(`File is outside the Oro source scope: ${document.uri}. Declare it in oro.toml or the selected compiler configuration.`)
        }
        aliases.set(uri, document.uri)
        document.uri = uri
      }
      if (message.method === 'workspace/didChangeWatchedFiles') message.params.changes = message.params.changes.filter(change => accepts(change.uri))
      return [message]
    }
  }
}

module.exports = { scopeGuard }
