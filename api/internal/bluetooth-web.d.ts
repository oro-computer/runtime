export class Bluetooth extends EventTarget {
    requestDevice(options?: {}): Promise<BluetoothDevice>;
    getDevices(): Promise<any[]>;
    getAvailability(): Promise<boolean>;
}
export class BluetoothDevice extends EventTarget {
    constructor({ id, name, services, manufacturerData }?: {
        id?: string;
        name?: string;
        services?: any[];
    });
    id: string;
    name: string;
    gatt: BluetoothRemoteGATTServer;
    gattServer: BluetoothRemoteGATTServer;
    uuids: string[];
    manufacturerData: BluetoothManufacturerDataMap;
    watchAdvertisements(): Promise<this>;
    forget(): Promise<this>;
}
export class BluetoothRemoteGATTServer extends EventTarget {
    constructor(device: any);
    device: any;
    connected: boolean;
    connect(): Promise<this>;
    disconnect(): void;
    getPrimaryService(uuid: any): Promise<BluetoothRemoteGATTService>;
    getPrimaryServices(uuid: any): Promise<any>;
}
export class BluetoothRemoteGATTService extends EventTarget {
    constructor(server: any, uuid: any, primary?: boolean);
    device: any;
    uuid: string;
    isPrimary: boolean;
    getCharacteristic(uuid: any): Promise<BluetoothRemoteGATTCharacteristic>;
    getCharacteristics(uuid: any): Promise<any>;
}
export class BluetoothRemoteGATTCharacteristic extends EventTarget {
    constructor(service: any, uuid: any);
    service: any;
    uuid: string;
    properties: Readonly<{
        broadcast: false;
        read: false;
        writeWithoutResponse: false;
        write: false;
        notify: false;
        indicate: false;
        authenticatedSignedWrites: false;
        reliableWrite: false;
        writableAuxiliaries: false;
    }>;
    readValue(): Promise<DataView<any>>;
    value: DataView<any>;
    writeValue(value: any): Promise<void>;
    writeValueWithResponse(value: any): Promise<void>;
    writeValueWithoutResponse(value: any): Promise<void>;
    startNotifications(): Promise<this>;
    stopNotifications(): Promise<this>;
}
export class BluetoothManufacturerDataMap extends Map<any, any> {
    constructor(entries: any);
    set(key: any, value: any): this;
    get(key: any): any;
    has(key: any): boolean;
}
