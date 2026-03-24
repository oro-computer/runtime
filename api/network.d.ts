export default network;
export function network(options: any): Promise<events>;
import { Cache } from './latica/index.js';
import { sha256 } from './latica/index.js';
import { Encryption } from './latica/index.js';
import { Packet } from './latica/index.js';
import { NAT } from './latica/index.js';
import events from './events.js';
export { Cache, sha256, Encryption, Packet, NAT };
