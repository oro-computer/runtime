export function installNavigatorHID(): any;
export class NavigatorHID extends EventTarget {
    getDevices(): Promise<any>;
    requestDevice(options?: {}): Promise<any>;
    cancelRequest(): Promise<void>;
    #private;
}
export class HIDDevice extends EventTarget {
    constructor(descriptor: any, _navigatorHID: any);
    _applyDescriptor(descriptor?: {}): void;
    deviceId: string;
    vendorId: number;
    productId: number;
    productName: any;
    manufacturerName: any;
    serialNumber: any;
    set opened(value: boolean);
    get opened(): boolean;
    get collections(): any[];
    get authorized(): boolean;
    open(): Promise<void>;
    close(): Promise<void>;
    forget(): Promise<void>;
    sendReport(reportId: any, data: any): Promise<void>;
    sendFeatureReport(reportId: any, data: any): Promise<void>;
    receiveFeatureReport(reportId: any, length: any): Promise<DataView<any>>;
    #private;
}
export class HIDInputReportEvent extends Event {
    constructor(type: any, init: any);
    device: any;
    reportId: number;
    data: any;
}
