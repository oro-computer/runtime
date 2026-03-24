export function installNavigatorUSB(): any;
export class NavigatorUSB extends EventTarget {
    _deviceCache: Map<any, any>;
    _onNativeConnect: (event: any) => void;
    _onNativeDisconnect: (event: any) => void;
    _createDevice(descriptor: any): any;
    getDevices(): Promise<any>;
    requestDevice(options?: {}): Promise<any>;
    cancelRequest(): Promise<void>;
    #private;
}
export class USBDevice extends EventTarget {
    constructor(descriptor: any);
    _applyDescriptor(descriptor?: {}): void;
    deviceId: string;
    vendorId: number;
    productId: number;
    deviceClass: number;
    deviceSubclass: number;
    deviceProtocol: number;
    productName: any;
    manufacturerName: any;
    serialNumber: any;
    opened: boolean;
    _authorized: boolean;
    configurations: any;
    open(): Promise<void>;
    close(): Promise<void>;
    forget(): Promise<void>;
    selectConfiguration(configurationValue: any): Promise<void>;
    claimInterface(interfaceNumber: any): Promise<void>;
    releaseInterface(interfaceNumber: any): Promise<void>;
    selectAlternateInterface(interfaceNumber: any, alternateSetting: any): Promise<void>;
    controlTransferIn(setup: any, length: any): Promise<USBInTransferResult>;
    controlTransferOut(setup: any, data: any): Promise<USBOutTransferResult>;
    transferIn(endpointNumber: any, length: any): Promise<USBInTransferResult>;
    transferOut(endpointNumber: any, data: any): Promise<USBOutTransferResult>;
    clearHalt(direction: any, endpointNumber: any): Promise<void>;
    reset(): Promise<void>;
}
export class USBInTransferResult {
    constructor(status: any, dataView: any);
    status: any;
    data: any;
}
export class USBOutTransferResult {
    constructor(status: any, bytesWritten: any);
    status: any;
    bytesWritten: number;
}
export class USBConnectionEvent extends Event {
    constructor(type: any, init: any);
    device: any;
}
