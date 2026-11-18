/** Select granted devices for a USB permission descriptor.
 * @param {USBDevice[]} devices
 * @param {USBDeviceRequestOptions} options
 * @returns {USBDevice[]}
 */
export function filterUSBDevices(devices: USBDevice[], options: USBDeviceRequestOptions): USBDevice[];
/** Install the WebUSB API for this supported execution context.
 * @returns {NavigatorUSB|undefined}
 */
export function installNavigatorUSB(): NavigatorUSB | undefined;
export type USBDeviceFilter = {
    vendorId?: number;
    productId?: number;
    classCode?: number;
    subclassCode?: number;
    protocolCode?: number;
    serialNumber?: string;
};
export type USBDeviceRequestOptions = {
    filters?: USBDeviceFilter[];
    exclusionFilters?: USBDeviceFilter[];
    acceptAllDevices?: boolean;
};
export type USBControlTransferParameters = {
    requestType: "standard" | "class" | "vendor";
    recipient: "device" | "interface" | "endpoint" | "other";
    request: number;
    value: number;
    index: number;
};
export type USBEndpointDescriptor = {
    endpointNumber: number;
    direction: "in" | "out";
    type: "bulk" | "interrupt" | "isochronous";
    packetSize: number;
};
export type USBAlternateDescriptor = {
    configurationValue: number;
    interfaceNumber: number;
    alternateSetting: number;
    classCode: number;
    subclassCode?: number;
    protocolCode?: number;
    interfaceName?: string;
    endpoints?: USBEndpointDescriptor[];
};
export type USBDeviceDescriptor = {
    deviceId: string;
    vendorId?: number;
    productId?: number;
    classCode?: number;
    subclassCode?: number;
    protocolCode?: number;
    productName?: string;
    manufacturerName?: string;
    serialNumber?: string;
    opened?: boolean;
    claimedInterfaces?: {
        interfaceNumber: number;
        alternateSetting: number;
    }[];
    authorized?: boolean;
    usbVersion?: number;
    deviceVersion?: number;
    configurationValue?: number;
    configurations?: number[];
    configurationNames?: Record<string, string>;
    interfaces?: USBAlternateDescriptor[];
};
export type USBTransferStatus = "ok" | "stall" | "babble";
export class USBDevice extends EventTarget {
    /** @param {USBDeviceDescriptor} descriptor */
    constructor(descriptor: USBDeviceDescriptor);
    /** @param {USBDeviceDescriptor} descriptor */
    _applyDescriptor(descriptor: USBDeviceDescriptor): void;
    deviceId: string;
    vendorId: number;
    productId: number;
    deviceClass: number;
    deviceSubclass: number;
    deviceProtocol: number;
    productName: string;
    manufacturerName: string;
    serialNumber: string;
    _authorized: boolean;
    /** @type {boolean} */
    opened: boolean;
    usbVersionMajor: number;
    usbVersionMinor: number;
    usbVersionSubminor: number;
    deviceVersionMajor: number;
    deviceVersionMinor: number;
    deviceVersionSubminor: number;
    _configurationNames: Record<string, string>;
    _values: number[];
    _interfaces: USBAlternateDescriptor[];
    configurations: readonly USBConfiguration[];
    _configurationValue: any;
    /** @returns {USBConfiguration|null} */
    get configuration(): USBConfiguration | null;
    open(): Promise<void>;
    close(): Promise<void>;
    forget(): Promise<void>;
    /**
     * @param {number} configurationValue
     */
    selectConfiguration(configurationValue: number): Promise<void>;
    /**
     * @param {number} interfaceNumber
     */
    claimInterface(interfaceNumber: number): Promise<void>;
    /**
     * @param {number} interfaceNumber
     */
    releaseInterface(interfaceNumber: number): Promise<void>;
    /**
     * @param {number} interfaceNumber
     * @param {number} alternateSetting
     */
    selectAlternateInterface(interfaceNumber: number, alternateSetting: number): Promise<void>;
    /**
     * @param {USBControlTransferParameters} setup
     * @param {number} length
     */
    controlTransferIn(setup: USBControlTransferParameters, length: number): Promise<USBInTransferResult>;
    /**
     * @param {USBControlTransferParameters} setup
     * @param {BufferSource} [data]
     */
    controlTransferOut(setup: USBControlTransferParameters, data?: BufferSource): Promise<USBOutTransferResult>;
    /**
     * @param {number} endpointNumber
     * @param {number} length
     */
    transferIn(endpointNumber: number, length: number): Promise<USBInTransferResult>;
    /**
     * @param {number} endpointNumber
     * @param {BufferSource} data
     */
    transferOut(endpointNumber: number, data: BufferSource): Promise<USBOutTransferResult>;
    /** @param {number} endpointNumber @param {number[]} packetLengths */
    isochronousTransferIn(endpointNumber: number, packetLengths: number[]): Promise<USBIsochronousInTransferResult>;
    /** @param {number} endpointNumber @param {BufferSource} data @param {number[]} packetLengths */
    isochronousTransferOut(endpointNumber: number, data: BufferSource, packetLengths: number[]): Promise<USBIsochronousOutTransferResult>;
    /**
     * @param {'in'|'out'} direction
     * @param {number} endpointNumber
     */
    clearHalt(direction: "in" | "out", endpointNumber: number): Promise<void>;
    reset(): Promise<void>;
    #private;
}
export class NavigatorUSB extends EventTarget {
    /** @type {Map<string, USBDevice>} */
    _deviceCache: Map<string, USBDevice>;
    _onNativeConnect: (event: any) => void;
    _onNativeDisconnect: (event: any) => void;
    addEventListener(type: any, listener: any, options: any): void;
    _watching: boolean;
    set onconnect(handler: any);
    get onconnect(): any;
    _connectHandler: any;
    set ondisconnect(handler: any);
    get ondisconnect(): any;
    _disconnectHandler: any;
    /** @param {USBDeviceDescriptor} descriptor @returns {USBDevice|null} */
    _createDevice(descriptor: USBDeviceDescriptor): USBDevice | null;
    /** @returns {Promise<USBDevice[]>} */
    getDevices(): Promise<USBDevice[]>;
    /** @param {USBDeviceRequestOptions} options @returns {Promise<USBDevice>} */
    requestDevice(options?: USBDeviceRequestOptions): Promise<USBDevice>;
    cancelRequest(): Promise<void>;
    #private;
}
/** Describes one selectable USB configuration. */
export class USBConfiguration {
    /** @param {USBDevice} device @param {number} configurationValue */
    constructor(device: USBDevice, configurationValue: number);
    _device: USBDevice;
    configurationValue: number;
    configurationName: string;
    _records: USBAlternateDescriptor[];
    interfaces: readonly USBInterface[];
}
/** Describes a claimable USB interface and its alternate settings. */
export class USBInterface {
    /** @param {USBConfiguration} configuration @param {number} interfaceNumber */
    constructor(configuration: USBConfiguration, interfaceNumber: number);
    _device: USBDevice;
    _configurationValue: number;
    _records: USBAlternateDescriptor[];
    interfaceNumber: number;
    alternates: readonly USBAlternateInterface[];
    /** @returns {boolean} */
    get claimed(): boolean;
    /** @returns {USBAlternateInterface} */
    get alternate(): USBAlternateInterface;
}
/** Describes the endpoints and class of one interface setting. */
export class USBAlternateInterface {
    /** @param {USBInterface} iface @param {number} alternateSetting */
    constructor(iface: USBInterface, alternateSetting: number);
    alternateSetting: number;
    interfaceClass: number;
    interfaceSubclass: number;
    interfaceProtocol: number;
    interfaceName: string;
    _endpoints: USBEndpointDescriptor[];
    endpoints: readonly USBEndpoint[];
}
/** @typedef {{vendorId?: number, productId?: number, classCode?: number, subclassCode?: number, protocolCode?: number, serialNumber?: string}} USBDeviceFilter */
/** @typedef {{filters?: USBDeviceFilter[], exclusionFilters?: USBDeviceFilter[], acceptAllDevices?: boolean}} USBDeviceRequestOptions */
/** @typedef {{requestType: 'standard'|'class'|'vendor', recipient: 'device'|'interface'|'endpoint'|'other', request: number, value: number, index: number}} USBControlTransferParameters */
/** @typedef {{endpointNumber: number, direction: 'in'|'out', type: 'bulk'|'interrupt'|'isochronous', packetSize: number}} USBEndpointDescriptor */
/** @typedef {{configurationValue: number, interfaceNumber: number, alternateSetting: number, classCode: number, subclassCode?: number, protocolCode?: number, interfaceName?: string, endpoints?: USBEndpointDescriptor[]}} USBAlternateDescriptor */
/** @typedef {{deviceId: string, vendorId?: number, productId?: number, classCode?: number, subclassCode?: number, protocolCode?: number, productName?: string, manufacturerName?: string, serialNumber?: string, opened?: boolean, claimedInterfaces?: {interfaceNumber: number, alternateSetting: number}[], authorized?: boolean, usbVersion?: number, deviceVersion?: number, configurationValue?: number, configurations?: number[], configurationNames?: Record<string, string>, interfaces?: USBAlternateDescriptor[]}} USBDeviceDescriptor */
/** @typedef {'ok'|'stall'|'babble'} USBTransferStatus */
/** Describes an endpoint on an alternate interface. */
export class USBEndpoint {
    /** @param {USBAlternateInterface} alternate @param {number} endpointNumber @param {'in'|'out'} direction */
    constructor(alternate: USBAlternateInterface, endpointNumber: number, direction: "in" | "out");
    endpointNumber: number;
    direction: "in" | "out";
    type: "bulk" | "interrupt" | "isochronous";
    packetSize: number;
}
export class USBIsochronousInTransferResult {
    /** @param {USBIsochronousInTransferPacket[]} packets @param {DataView|null} [data] */
    constructor(packets: USBIsochronousInTransferPacket[], data?: DataView | null);
    packets: readonly USBIsochronousInTransferPacket[];
    data: DataView<ArrayBufferLike>;
}
export class USBIsochronousOutTransferResult {
    /** @param {USBIsochronousOutTransferPacket[]} packets */
    constructor(packets: USBIsochronousOutTransferPacket[]);
    packets: readonly USBIsochronousOutTransferPacket[];
}
export class USBIsochronousInTransferPacket extends USBInTransferResult {
}
export class USBIsochronousOutTransferPacket extends USBOutTransferResult {
}
export class USBInTransferResult {
    /** @param {USBTransferStatus} status @param {DataView|null} [dataView] */
    constructor(status: USBTransferStatus, dataView?: DataView | null);
    status: USBTransferStatus;
    data: DataView<ArrayBufferLike>;
}
export class USBOutTransferResult {
    /** @param {USBTransferStatus} status @param {number} [bytesWritten] */
    constructor(status: USBTransferStatus, bytesWritten?: number);
    status: USBTransferStatus;
    bytesWritten: number;
}
export class USBConnectionEvent extends Event {
    constructor(type: any, init: any);
    device: any;
}
