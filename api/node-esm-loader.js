const { ORO_MODULES_DIR = 'node_modules', ORO_RUNTIME_PACKAGE_SCOPE } =
  process.env

const runtimePackageCandidates = Array.from(
  new Set([ORO_RUNTIME_PACKAGE_SCOPE, '@oro-computer/runtime'].filter(Boolean))
)

function isModuleNotFound (err) {
  if (!err) return false
  return (
    err.code === 'ERR_MODULE_NOT_FOUND' ||
    err.code === 'MODULE_NOT_FOUND' ||
    /Cannot find module/.test(err.message || '')
  )
}

export async function resolve (specifier, _ctx, next) {
  const modulesMatch = specifier.match(/^oro:modules\//)
  if (modulesMatch) {
    let moduleName = specifier.replace(/^oro:modules\//, '')
    if (moduleName.endsWith('.js')) {
      moduleName = moduleName.slice(0, -3)
    }

    specifier = `${ORO_MODULES_DIR}/${moduleName}.js`
  } else if (specifier.startsWith('oro:')) {
    let moduleName = specifier.replace(/^oro:/, '')
    if (moduleName.endsWith('.js')) {
      moduleName = moduleName.slice(0, -3)
    }

    let lastError
    for (const scope of runtimePackageCandidates) {
      try {
        return await next(`${scope}/${moduleName}.js`)
      } catch (err) {
        if (!isModuleNotFound(err)) throw err
        lastError = err
      }
    }
    throw (
      lastError ??
      new Error(`Unable to resolve runtime module specifier "${specifier}"`)
    )
  }

  return next(specifier)
}

export default resolve
