declare namespace _default {
    export { share };
    export { canShare };
    export { normalizeShareData };
    export { platformSupportsShare };
}
export default _default;
declare function share(data?: {}): Promise<void>;
declare function canShare(data?: {}): boolean;
declare function normalizeShareData(input?: {}, { allowEmpty }?: {
    allowEmpty?: boolean;
}): {
    title: string;
    text: string;
    url: string;
    files: any[];
    hasData: boolean;
};
declare function platformSupportsShare(): boolean;
