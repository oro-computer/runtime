/**
 * @module network
 *
 * Provides a higher level API over the latica protocol.
 *
 * Options:
 * - `allowUnsigned?: boolean` Deliver unverified payloads to handlers. Default: false
 * - `controlPlaneAuth?: 'sig'|'off'` Require signatures on control-plane frames (Ping/Pong/Join/Intro/Query). Default: 'off'
 */
import api from './latica/api.js'
import { Cache, Packet, sha256, Encryption, NAT } from './latica/index.js'
import events from './events.js'
import dgram from './dgram.js'

const network = (options) => api(options, events, dgram)

export { network, Cache, sha256, Encryption, Packet, NAT }
export default network
