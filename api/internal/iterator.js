/**
 * Internal iterator utilities.
 * Currently only provides a trivial iterator wrapper.
 */

export function fromArray (items) {
  let index = 0

  return {
    next () {
      if (index >= items.length) {
        return { value: undefined, done: true }
      }

      const value = items[index++]
      return { value, done: false }
    }
  }
}
