export const dispose = Symbol.dispose ?? Symbol.for('oro.runtime.gc.finalizer')
export const serialize = Symbol.serialize ?? Symbol.for('oro.runtime.serialize')

export default {
  dispose,
  serialize
}
