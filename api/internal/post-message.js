import serialize from './serialize.js'

const { BroadcastChannel, MessagePort, postMessage } = globalThis

const platform = {
  BroadcastChannelPostMessage: BroadcastChannel.prototype.postMessage,
  MessagePortPostMessage: MessagePort.prototype.postMessage,
  GlobalPostMessage: postMessage
}

/**
 * Sends a window message from the current realm, preserving its source window.
 * Calling the target's JavaScript wrapper changes the incumbent realm in Chromium.
 * @ignore
 * @param {Window} target
 * @param {any} message
 * @param {string} targetOrigin
 * @returns {void}
 */
export function postWindowMessage (target, message, targetOrigin) {
  return platform.GlobalPostMessage.call(
    target,
    handlePostMessage(message),
    targetOrigin
  )
}

BroadcastChannel.prototype.postMessage = function (message, ...args) {
  return platform.BroadcastChannelPostMessage.call(
    this,
    handlePostMessage(message),
    ...args
  )
}

MessagePort.prototype.postMessage = function (message, ...args) {
  return platform.MessagePortPostMessage.call(
    this,
    handlePostMessage(message),
    ...args
  )
}

globalThis.postMessage = function (message, ...args) {
  return platform.GlobalPostMessage.call(
    this,
    handlePostMessage(message),
    ...args
  )
}

function handlePostMessage (message) {
  return serialize(message)
}

export default null
