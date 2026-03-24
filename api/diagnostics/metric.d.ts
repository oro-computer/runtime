export class Metric {
    init(): void;
    update(_value: any): void;
    destroy(): void;
    toJSON(): {};
    toString(): string;
    [Symbol.iterator](): any;
    [Symbol.toStringTag](): string;
}
export default Metric;
