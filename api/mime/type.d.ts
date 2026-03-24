export class MIMEType {
    constructor(input: any);
    set type(value: any);
    get type(): any;
    set subtype(value: any);
    get subtype(): any;
    get essence(): string;
    get params(): any;
    toString(): string;
    toJSON(): string;
    #private;
}
export default MIMEType;
