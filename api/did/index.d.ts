export function validateDocument(document: any, { allowExtensions }?: {
    allowExtensions?: boolean;
}): {
    ok: boolean;
    errors: {
        path: string;
        code: string;
        message: string;
    }[];
    document?: undefined;
    normalized?: undefined;
} | {
    ok: boolean;
    errors: {
        path: string;
        code: string;
        message: any;
    }[];
    document: DID;
    normalized: any;
};
export class DIDError extends Error {
    constructor(code: any, message: any, details?: any);
    code: any;
    details: any;
}
export class DID {
    static parse(input: any): DID;
    static from(input: any): DID;
    static normalize(input: any): string;
    constructor(input: any);
    method: any;
    methodSpecificId: any;
    idSegments: readonly any[];
    href: string;
    toString(): string;
    /** @returns {string|Record<string, any>} */
    toJSON(): string | Record<string, any>;
    equals(other: any): boolean;
}
export class DIDURL extends DID {
    static parse(input: any): DIDURL;
    static from(input: any): DIDURL;
    constructor(input: any, init?: any);
    parameters: any;
    path: any;
    query: any;
    fragment: any;
    queryParams: any;
    withParameter(name: any, value: any): DIDURL;
    withoutParameter(name: any): DIDURL;
    withQuery(query: any): DIDURL;
    withFragment(fragment: any): DIDURL;
}
export class DIDDocument {
    static from(input: any): DIDDocument;
    constructor(input: any);
    get id(): any;
    get context(): any[];
    get alsoKnownAs(): any[];
    get controller(): any[];
    get verificationMethod(): any;
    get services(): any;
    setContext(contexts: any): this;
    setControllers(controllers: any): this;
    setAlsoKnownAs(list: any): this;
    addVerificationMethod(method: any): this;
    removeVerificationMethod(id: any): this;
    addToRelationship(relationship: any, reference: any): this;
    removeFromRelationship(relationship: any, targetId: any): this;
    addService(service: any): this;
    removeService(id: any): this;
    toJSON(): any;
    toObject(): any;
    clone(): DIDDocument;
    freeze(): this;
    #private;
}
export class Resolver {
    constructor({ drivers, cache }?: {
        drivers?: {};
        cache?: any;
    });
    register(method: any, driver: any): void;
    unregister(method: any): boolean;
    has(method: any): boolean;
    get(method: any): any;
    listMethods(): any[];
    resolve(did: any, options?: {}): Promise<any>;
    resolveRepresentation(did: any, options?: {}): Promise<any>;
    dereference(didUrl: any, options?: {}): Promise<any>;
    #private;
}
export namespace constants {
    export { METHOD_NAME_PATTERN };
    export { METHOD_SPECIFIC_ID_PATTERN };
    export { DID_PATTERN };
    export { PARAM_CHAR_PATTERN };
    export { PARAM_VALUE_PATTERN };
    export { CORE_CONTEXT };
    export { CORE_VERIFICATION_RELATIONSHIPS };
    export { STANDARD_PARAMETERS };
    export { CORE_DOCUMENT_PROPERTIES };
}
export function parse(input: any): DID;
export function parseUrl(input: any): DIDURL;
export function normalize(input: any): string;
export function normalizeUrl(input: any): string;
export function isValid(input: any): boolean;
export function isValidUrl(input: any): boolean;
export const METHOD_NAME_PATTERN: RegExp;
export const METHOD_SPECIFIC_ID_PATTERN: RegExp;
export const DID_PATTERN: RegExp;
export const PARAM_CHAR_PATTERN: RegExp;
export const PARAM_VALUE_PATTERN: RegExp;
export const CORE_CONTEXT: "https://www.w3.org/ns/did/v1";
export const CORE_VERIFICATION_RELATIONSHIPS: string[];
export const STANDARD_PARAMETERS: string[];
export const CORE_DOCUMENT_PROPERTIES: string[];
