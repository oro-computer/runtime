import { test } from 'oro:test'
import * as xpc from 'oro:xpc'
import ipc from 'oro:ipc'

const { Connection, uuid } = xpc
const TOO_LARGE_TIMEOUT = 9223372036855

function dispatchDataEvent (detail) {
  const event =
    typeof globalThis.CustomEvent === 'function'
      ? new globalThis.CustomEvent('data', { detail })
      : Object.assign(new globalThis.Event('data'), { detail })
  globalThis.dispatchEvent(event)
}

test('xpc.uuid marks typed UUID values', async (t) => {
  const descriptor = uuid('12345678-1234-5678-90ab-fedcba987654')
  t.equal(descriptor.type, 'uuid', 'descriptor uses uuid type')
  t.equal(
    descriptor.value,
    '12345678-1234-5678-90ab-fedcba987654',
    'descriptor carries provided value'
  )
})

test('Connection.send rejects when backend signals error', async (t) => {
  const originalRequest = ipc.request
  const connection = new Connection('err-test')

  ipc.request = async () => ({
    data: {
      error: true,
      reason: 'backend exploded',
      type: 'BackendFailure'
    }
  })

  try {
    await t.rejects(
      () => connection.send({ payload: true }),
      (err) =>
        err?.code === 'BackendFailure' &&
        err?.message === 'backend exploded' &&
        err?.connectionId === 'err-test',
      'propagates backend error payload'
    )
  } finally {
    ipc.request = originalRequest
  }
})

test('respond reuses raw descriptor payload', async (t) => {
  const originalRequest = ipc.request
  const requests = []
  const descriptor = {
    type: 'dictionary',
    value: {
      greeting: { type: 'string', value: 'hello' }
    }
  }
  const descriptorJSON = JSON.stringify(descriptor)

  ipc.request = async (command, payload) => {
    requests.push({ command, payload })
    if (command === 'xpc.availability') {
      return { data: { available: true } }
    }
    if (command === 'xpc.connect') {
      return {
        data: {
          connectionId: '88',
          listener: 'false',
          service: 'com.example.echo'
        }
      }
    }
    if (command === 'xpc.respond') {
      return { data: { messageId: payload.messageId, status: 'sent' } }
    }
    if (command === 'xpc.disconnect') {
      return { data: { connectionId: '88', status: 'ok' } }
    }
    throw new Error(`unexpected ipc.request command: ${command}`)
  }

  try {
    const connection = await xpc.connect({ service: 'com.example.echo' })

    const respondDone = new Promise((resolve) => {
      connection.once('message', async (envelope) => {
        await envelope.respond(envelope.raw)
        resolve()
      })
    })

    dispatchDataEvent({
      source: 'xpc.message',
      data: {
        connectionId: '88',
        messageId: '555',
        expectsReply: 'true',
        message: descriptor
      }
    })

    await respondDone

    const respondCall = requests.find(
      ({ command }) => command === 'xpc.respond'
    )
    t.ok(respondCall, 'respond route invoked with payload')
    t.equal(
      respondCall.payload?.message,
      descriptorJSON,
      'descriptor string forwarded unchanged'
    )

    await connection.close()
  } finally {
    ipc.request = originalRequest
  }
})

test('Connection.send validates timeout upper bound', async (t) => {
  const connection = new Connection('timeout-test')
  const originalRequest = ipc.request
  let called = false

  ipc.request = async () => {
    called = true
    return {}
  }

  try {
    await t.rejects(
      () => connection.send({ payload: true }, { timeout: TOO_LARGE_TIMEOUT }),
      (err) => err instanceof RangeError && /timeout/i.test(err?.message ?? ''),
      'throws when timeout exceeds supported range'
    )
    t.equal(called, false, 'does not issue IPC request when timeout invalid')
  } finally {
    ipc.request = originalRequest
  }
})

test('connect validates pending reply timeout upper bound', async (t) => {
  const originalRequest = ipc.request

  ipc.request = async (command) => {
    if (command === 'xpc.availability') {
      return { data: { available: true } }
    }
    throw new Error(`unexpected ipc.request command: ${command}`)
  }

  try {
    await t.rejects(
      () =>
        xpc.connect({
          service: 'com.example.echo',
          pendingReplyTimeout: TOO_LARGE_TIMEOUT
        }),
      (err) => err instanceof RangeError && /pending/i.test(err?.message ?? ''),
      'throws when pending reply timeout exceeds supported range'
    )
  } finally {
    ipc.request = originalRequest
  }
})

test('connect forwards pending reply timeout to backend', async (t) => {
  const originalRequest = ipc.request
  let connectPayload = null

  ipc.request = async (command, payload) => {
    if (command === 'xpc.availability') {
      return { data: { available: true } }
    }
    if (command === 'xpc.connect') {
      connectPayload = payload
      return {
        data: {
          connectionId: '99',
          listener: 'false',
          service: 'com.example.echo'
        }
      }
    }
    if (command === 'xpc.disconnect') {
      return { data: { connectionId: '99', status: 'ok' } }
    }
    throw new Error(`unexpected ipc.request command: ${command}`)
  }

  try {
    const connection = await xpc.connect({
      service: 'com.example.echo',
      pendingReplyTimeout: 1234
    })
    const parsed = JSON.parse(connectPayload?.options ?? '{}')
    t.equal(
      parsed.pendingReplyTimeout,
      1234,
      'pending reply timeout forwarded as integer'
    )
    await connection.close()
  } finally {
    ipc.request = originalRequest
  }
})

test('Connection emits timeout, drop, and closed state events', async (t) => {
  const originalRequest = ipc.request
  const responses = {
    'xpc.availability': { data: { available: true } },
    'xpc.connect': {
      data: {
        connectionId: '77',
        listener: 'false',
        service: 'com.example.echo'
      }
    },
    'xpc.disconnect': { data: { connectionId: '77', status: 'ok' } }
  }

  ipc.request = async (command) => {
    if (!responses[command]) {
      throw new Error(`unexpected ipc.request command: ${command}`)
    }
    return responses[command]
  }

  try {
    const connection = await xpc.connect({ service: 'com.example.echo' })

    const timeoutDetailPromise = new Promise((resolve) =>
      connection.once('message-timeout', resolve)
    )
    const droppedDetailPromise = new Promise((resolve) =>
      connection.once('message-dropped', resolve)
    )
    const errorPromise = new Promise((resolve) =>
      connection.once('error', resolve)
    )
    const closePromise = new Promise((resolve) =>
      connection.once('close', resolve)
    )

    dispatchDataEvent({
      source: 'xpc.message.timeout',
      data: {
        connectionId: '77',
        messageId: '123',
        reason: 'timed out waiting for reply'
      }
    })

    const timeoutDetail = await timeoutDetailPromise
    t.equal(timeoutDetail.messageId, '123', 'timeout event forwards message id')
    t.equal(
      timeoutDetail.reason,
      'timed out waiting for reply',
      'timeout event forwards reason'
    )

    dispatchDataEvent({
      source: 'xpc.message.dropped',
      data: {
        connectionId: '77',
        reason: 'queue full'
      }
    })

    const droppedDetail = await droppedDetailPromise
    t.equal(droppedDetail.reason, 'queue full', 'drop event forwards reason')

    const dropError = await errorPromise
    t.equal(
      dropError.code,
      'XPC_MESSAGE_DROPPED',
      'drop event emits error with code'
    )

    dispatchDataEvent({
      source: 'xpc.state',
      data: {
        connectionId: '77',
        state: 'closed',
        reason: 'unit test'
      }
    })

    const closeDetail = await closePromise
    t.equal(
      closeDetail.state,
      'closed',
      'closed state event forwarded to listeners'
    )
    t.equal(closeDetail.reason, 'unit test', 'close event forwards reason')

    await connection.close()
  } finally {
    ipc.request = originalRequest
  }
})

test('Connection.close tolerates invalid backend state and is idempotent', async (t) => {
  const originalRequest = ipc.request
  let disconnectCalls = 0

  ipc.request = async (command) => {
    if (command === 'xpc.availability') {
      return { data: { available: true } }
    }
    if (command === 'xpc.connect') {
      return {
        data: {
          connectionId: '101',
          listener: 'false',
          service: 'com.example.echo'
        }
      }
    }
    if (command === 'xpc.disconnect') {
      disconnectCalls++
      return {
        err: {
          type: 'InvalidStateError',
          message: 'Unknown XPC connection id'
        }
      }
    }
    throw new Error(`unexpected ipc.request command: ${command}`)
  }

  try {
    const connection = await xpc.connect({ service: 'com.example.echo' })
    let closeError = null

    const stateHandled = new Promise((resolve) => {
      connection.once('state', async () => {
        try {
          const result = await connection.close()
          t.equal(
            result,
            true,
            'close resolves even if backend already closed connection'
          )
        } catch (err) {
          closeError = err
        } finally {
          resolve()
        }
      })
    })

    dispatchDataEvent({
      source: 'xpc.state',
      data: {
        connectionId: '101',
        state: 'invalid'
      }
    })

    await stateHandled
    t.equal(closeError, null, 'close inside state handler does not throw')
    t.equal(disconnectCalls, 1, 'disconnect invoked exactly once')

    const secondClose = await connection.close()
    t.equal(secondClose, false, 'subsequent close resolves as no-op')
    t.equal(
      disconnectCalls,
      1,
      'no additional disconnect request after idempotent close'
    )
  } finally {
    ipc.request = originalRequest
  }
})
