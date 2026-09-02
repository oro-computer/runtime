import * as vm from '../vm.js'
import gc from '../gc.js'
import serialize from '../internal/serialize.js'
import ipc from '../ipc.js'

let realm = null

function serializeWindowMessage (message) {
  const transfers = new Set()
  const serialized = serialize(ipc.findIPCMessageTransfers(transfers, message))

  for (const transfer of transfers) {
    if (transfer instanceof ipc.IPCMessagePort) {
      ipc.IPCMessagePort.transfer(transfer)
    }
  }

  return serialized
}

function createTransferredError (error) {
  if (error instanceof Error) {
    const object = {
      name: error.name,
      type:
        Object.getPrototypeOf(error)?.constructor?.name ??
        error.constructor?.name ??
        'Error',
      message: error.message
    }

    if (error.stack) {
      object.stack = error.stack
    }

    if (error.code) {
      object.code = error.code
    }

    // assign any other enumerable properties
    Object.assign(object, error)

    if (error.cause instanceof Error) {
      object.cause = createTransferredError(error.cause)
    } else if (error.cause !== undefined) {
      object.cause = error.cause
    }

    return object
  }

  if (error && typeof error === 'object') {
    const clone = { ...error }

    if (clone.name === undefined) {
      clone.name = 'Error'
    }

    if (clone.type === undefined) {
      clone.type = error.constructor?.name ?? 'Object'
    }

    if (clone.message === undefined) {
      if (typeof error.message === 'string') {
        clone.message = error.message
      } else {
        clone.message = String(error ?? '')
      }
    }

    if (error.cause instanceof Error) {
      clone.cause = createTransferredError(error.cause)
    }

    return clone
  }

  return {
    name: 'Error',
    type: 'Error',
    message: String(error ?? '')
  }
}

function postWorldResult (message) {
  try {
    return realm.postMessage(serializeWindowMessage(message))
  } catch (error) {
    return realm.postMessage(
      serializeWindowMessage({
        type: 'world.result',
        err: createTransferredError(error),
        nonce: message.nonce,
        id: message.id
      })
    )
  }
}

const context = {}
Object.defineProperty(globalThis, 'globalObject', {
  configurable: false,
  enumerable: false,
  value: context
})

globalThis.addEventListener('message', async (event) => {
  if (!realm) {
    realm = event.source
  }

  const eventData = ipc.inflateIPCMessageTransfers(event.data)

  if (eventData?.type === 'script') {
    const { id, mode, nonce, source } = eventData
    const inputTransfers = []
    let result

    vm.findMessageTransfers(inputTransfers, eventData.context, {
      ignoreScriptReferenceArgs: true
    })

    for (const value of inputTransfers) {
      if (value instanceof MessagePort) {
        return postWorldResult({
          type: 'world.result',
          err: createTransferredError(
            new TypeError('MessagePort cannot be in context')
          ),
          nonce,
          id
        })
      }
    }

    const delta = vm.applyContextDifferences(
      context,
      eventData.context,
      context,
      true
    )

    for (const key in context) {
      Object.defineProperty(globalThis, key, {
        configurable: true,
        enumerable: false,
        get: () => Reflect.get(context, key)
      })
    }

    for (const key of delta.deletions) {
      Reflect.deleteProperty(globalThis, key)
    }

    try {
      vm.applyInputContextReferences(context)
      result = await vm.compileFunction(source, {
        async: true,
        type: mode !== 'module' ? 'classic' : 'module',
        wrap: mode !== 'module',
        context
      })()
    } catch (err) {
      vm.applyOutputContextReferences(context)
      return postWorldResult({
        type: 'world.result',
        err: createTransferredError(err),
        context,
        nonce,
        id
      })
    }

    if (typeof result === 'function') {
      result = vm.createReference(result, context).toJSON()
    } else if (result && typeof result === 'object') {
      if (
        Object.getPrototypeOf(result) === Object.prototype ||
        result instanceof Array
      ) {
        vm.applyOutputContextReferences(result)
      } else {
        vm.applyOutputContextReferences(result)
        result = vm.createReference(result, context).toJSON(true)
      }
    }

    vm.applyOutputContextReferences(context)

    return postWorldResult({
      type: 'world.result',
      data: result,
      context,
      nonce,
      id
    })
  }

  if (eventData?.type === 'destroy') {
    const { id } = eventData
    await gc.release()
    return realm.postMessage({ type: 'world.destroy', id })
  }
})

export {}
