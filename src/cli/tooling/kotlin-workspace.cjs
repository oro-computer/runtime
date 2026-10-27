'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { pathToFileURL } = require('node:url')

/**
 * Export the Kotlin LSP JSON workspace format from Gradle's exact source list.
 * Format: Kotlin/kotlin-lsp workspace-import/src/com/jetbrains/ls/imports/json/model.kt.
 * @param {object} model
 * @returns {object}
 */
function jetbrainsWorkspace (model) {
  const sources = new Set(model.originalSources)
  const directories = [...new Set(model.originalSources.map(file => path.dirname(file)))]
  const roots = directories.filter(dir => !directories.some(parent => parent !== dir && dir.startsWith(parent + path.sep)))
  const contentRoots = roots.map(root => {
    const excludedUrls = []
    const visit = directory => {
      for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const file = path.join(directory, entry.name)
        if (sources.has(file)) continue
        if (entry.isDirectory() && [...sources].some(source => source.startsWith(file + path.sep))) visit(file)
        else excludedUrls.push(pathToFileURL(file).href)
      }
    }
    visit(root)
    return { path: root, excludedUrls, sourceRoots: [{ path: root, type: 'java-source' }] }
  })
  const name = 'OroApplication'
  const classpath = [...new Set([...model.compileClasspath, ...model.runtimeClasses])]
  const libraries = classpath.map((file, index) => ({ name: `oro-${index}`, type: null, roots: [{ path: file }] }))
  return {
    modules: [{ name, contentRoots, dependencies: [{ type: 'sdk', name: 'Oro JDK', kind: 'JavaSDK' }, { type: 'moduleSource' }, ...libraries.map(library => ({ type: 'library', name: library.name, scope: 'compile' }))] }],
    libraries,
    sdks: [{ name: 'Oro JDK', type: 'JavaSDK', version: String(model.jdk.compilerVersion), homePath: model.jdk.compilerHome, additionalData: '' }],
    javaSettings: [{ module: name, inheritedCompilerOutput: false, excludeOutput: false, languageLevelId: `JDK_${model.compiler.javaSource.replace('1.', '')}`, manifestAttributes: {} }],
    kotlinSettings: [{
      name: 'Kotlin',
      module: name,
      sourceRoots: roots,
      configFileItems: [],
      useProjectSettings: false,
      implementedModuleNames: [],
      dependsOnModuleNames: [],
      additionalVisibleModuleNames: [],
      productionOutputPath: null,
      testOutputPath: null,
      sourceSetNames: [],
      isTestModule: false,
      externalProjectId: name,
      isHmppEnabled: false,
      pureKotlinSourceFolders: [],
      kind: 'default',
      compilerArguments: 'J' + JSON.stringify({ jvmTarget: model.compiler.jvmTarget, ...(model.compiler.languageVersion ? { languageVersion: model.compiler.languageVersion } : {}), ...(model.compiler.apiVersion ? { apiVersion: model.compiler.apiVersion } : {}) }),
      additionalArguments: model.compiler.freeCompilerArgs.map(arg => JSON.stringify(arg)).join(' '),
      scriptTemplates: null,
      scriptTemplatesClasspath: null,
      outputDirectoryForJsLibraryFiles: null,
      targetPlatform: null,
      externalSystemRunTasks: [],
      version: 5,
      flushNeeded: false
    }]
  }
}

module.exports = { jetbrainsWorkspace }
