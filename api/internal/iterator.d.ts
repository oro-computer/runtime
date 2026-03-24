/**
 * Internal iterator utilities.
 * Currently only provides a trivial iterator wrapper.
 */
export function fromArray(items: any): {
    next(): {
        value: any;
        done: boolean;
    };
};
