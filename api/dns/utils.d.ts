/**
 * Normalizes options for dns.lookup style APIs.
 * @param {(number|string|object)=} input
 * @returns {{ family: 0|4|6, hints: number, all: boolean, verbatim: boolean }}
 * @ignore
 */
export function normalizeLookupOptions(input?: (number | string | object) | undefined): {
    family: 0 | 4 | 6;
    hints: number;
    all: boolean;
    verbatim: boolean;
};
/**
 * Creates a Node.js compatible getaddrinfo error.
 * @param {string} hostname
 * @param {Error|object|null} cause
 * @returns {Error}
 * @ignore
 */
export function createLookupError(hostname: string, cause: Error | object | null): Error;
