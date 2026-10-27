'use strict'

const fs = require('node:fs')

/**
 * TypeScript server plugin loaded only by the wrapped child server. The SDK's
 * ambient oro: declarations join the original project without copying sources
 * or replacing compiler options, module resolution, or project references.
 * @param {object} modules TypeScript's plugin API modules.
 * @returns {object}
 */
function initialize (modules) {
  const ts = modules.typescript
  const filename = process.env.ORO_TYPESCRIPT_MODEL
  if (!filename) throw new Error('Oro TypeScript plugin requires ORO_TYPESCRIPT_MODEL')
  const model = JSON.parse(fs.readFileSync(filename, 'utf8'))
  if (model.schemaVersion !== 1 || model.language !== 'typescript') throw new Error('Unsupported Oro TypeScript editor model')
  return {
    create (info) {
      const project = info.project
      // Configured projects include getExternalFiles in their root list.
      // Inferred projects only watch those files, so add the declaration to the
      // semantic host's roots. Leave snapshots and versions to tsserver, and keep
      // the syntax-only server's host untouched.
      if (project.projectKind === ts.server.ProjectKind.Inferred && project.projectService.serverMode === ts.LanguageServiceMode.Semantic) {
        const host = info.languageServiceHost
        const getFileNames = host.getScriptFileNames.bind(host)
        host.getScriptFileNames = () => {
          const files = getFileNames()
          return files.length && !files.includes(model.typings) ? [...files, model.typings] : files
        }
      }
      return info.languageService
    },
    // Let tsserver own ScriptInfo and graph updates for this declaration file.
    // The plugin is loaded only in the server process for this Oro workspace.
    getExternalFiles () { return [model.typings] }
  }
}

module.exports = initialize
