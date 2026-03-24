/**
 * The NAT type is encoded using 5 bits:
 *
 * 0b00001 : the lsb indicates if endpoint dependence information is included
 * 0b00010 : the second bit indicates the endpoint dependence value
 *
 * 0b00100 : the third bit indicates if firewall information is included
 * 0b01000 : the fourth bit describes which requests can pass the firewall, only known IPs (0) or any IP (1)
 * 0b10000 : the fifth bit describes which requests can pass the firewall, only known ports (0) or any port (1)
 */
/**
 * Every remote will see the same IP:PORT mapping for this peer.
 *
 *                        :3333 ┌──────┐
 *   :1111                ┌───▶ │  R1  │
 * ┌──────┐    ┌───────┐  │     └──────┘
 * │  P1  ├───▶│  NAT  ├──┤
 * └──────┘    └───────┘  │     ┌──────┐
 *                        └───▶ │  R2  │
 *                        :3333 └──────┘
 */
export const MAPPING_ENDPOINT_INDEPENDENT: 3;
/**
 * Every remote will see a different IP:PORT mapping for this peer.
 *
 *                        :4444 ┌──────┐
 *   :1111                ┌───▶ │  R1  │
 * ┌──────┐    ┌───────┐  │     └──────┘
 * │  P1  ├───▶│  NAT  ├──┤
 * └──────┘    └───────┘  │     ┌──────┐
 *                        └───▶ │  R2  │
 *                        :5555 └──────┘
 */
export const MAPPING_ENDPOINT_DEPENDENT: 1;
/**
 * The firewall allows the port mapping to be accessed by:
 * - Any IP:PORT combination (FIREWALL_ALLOW_ANY)
 * - Any PORT on a previously connected IP (FIREWALL_ALLOW_KNOWN_IP)
 * - Only from previously connected IP:PORT combinations (FIREWALL_ALLOW_KNOWN_IP_AND_PORT)
 */
export const FIREWALL_ALLOW_ANY: 28;
export const FIREWALL_ALLOW_KNOWN_IP: 12;
export const FIREWALL_ALLOW_KNOWN_IP_AND_PORT: 4;
/**
 * The initial state of the nat is unknown and its value is 0
 */
export const UNKNOWN: 0;
/**
 * Full-cone NAT, also known as one-to-one NAT
 *
 * Any external host can send packets to iAddr:iPort by sending packets to eAddr:ePort.
 *
 * @summary its a packet party at this mapping and everyone's invited
 */
export const UNRESTRICTED: number;
/**
 * (Address)-restricted-cone NAT
 *
 * An external host (hAddr:any) can send packets to iAddr:iPort by sending packets to eAddr:ePort only
 * if iAddr:iPort has previously sent a packet to hAddr:any. "Any" means the port number doesn't matter.
 *
 * @summary The NAT will drop your packets unless a peer within its network has previously messaged you from *any* port.
 */
export const ADDR_RESTRICTED: number;
/**
 * Port-restricted cone NAT
 *
 * An external host (hAddr:hPort) can send packets to iAddr:iPort by sending
 * packets to eAddr:ePort only if iAddr:iPort has previously sent a packet to
 * hAddr:hPort.
 *
 * @summary The NAT will drop your packets unless a peer within its network
 * has previously messaged you from this *specific* port.
 */
export const PORT_RESTRICTED: number;
/**
 * Symmetric NAT
 *
 * Only an external host that receives a packet from an internal host can send
 * a packet back.
 *
 * @summary The NAT will only accept replies to a correspondence initialized
 * by itself, the mapping it created is only valid for you.
 */
export const ENDPOINT_RESTRICTED: number;
export function isEndpointDependenceDefined(nat: any): boolean;
export function isFirewallDefined(nat: any): boolean;
export function isValid(nat: any): boolean;
export function toString(n: any): "UNRESTRICTED" | "ADDR_RESTRICTED" | "PORT_RESTRICTED" | "ENDPOINT_RESTRICTED" | "UNKNOWN";
export function toStringStrategy(n: any): "STRATEGY_DEFER" | "STRATEGY_DIRECT_CONNECT" | "STRATEGY_TRAVERSAL_OPEN" | "STRATEGY_TRAVERSAL_CONNECT" | "STRATEGY_PROXY" | "STRATEGY_UNKNOWN";
export const STRATEGY_DEFER: 0;
export const STRATEGY_DIRECT_CONNECT: 1;
export const STRATEGY_TRAVERSAL_OPEN: 2;
export const STRATEGY_TRAVERSAL_CONNECT: 3;
export const STRATEGY_PROXY: 4;
export function connectionStrategy(a: any, b: any): 0 | 1 | 2 | 3 | 4;
