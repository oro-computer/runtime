export { me as URLPattern };
declare var me: {
    new (t: {}, r: any, n: any): {
        "__#private@#i": any;
        "__#private@#n": {};
        "__#private@#t": {};
        "__#private@#e": {};
        "__#private@#s": {};
        "__#private@#l": boolean;
        test(t: {}, r: any): boolean;
        exec(t: {}, r: any): {
            inputs: any[] | {}[];
        };
        get protocol(): any;
        get username(): any;
        get password(): any;
        get hostname(): any;
        get port(): any;
        get pathname(): any;
        get search(): any;
        get hash(): any;
        get hasRegExpGroups(): boolean;
    };
    compareComponent(t: any, r: any, n: any): number;
};
