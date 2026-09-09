import { ErrnoError } from 'oro:errors'
import errno from 'oro:errno'
import { test } from 'oro:test'

test('errno: preserve numeric system error codes', (t) => {
  for (const code of [errno.constants.ENOENT, -errno.constants.ENOENT]) {
    const error = new ErrnoError(code)
    t.equal(error.code, errno.constants.ENOENT, 'preserves the positive errno code')
    t.equal(error.name, 'ENOENT', 'resolves the symbolic name')
    t.equal(error.message, errno.getMessage('ENOENT'), 'resolves the error message')
  }
  t.equal(errno.getCode('enoent'), errno.constants.ENOENT, 'accepts a symbolic name')
  t.equal(errno.getCode(NaN), 0, 'rejects an invalid numeric code')
  t.equal(errno.getCode(1.5), 0, 'rejects a fractional code')
})
