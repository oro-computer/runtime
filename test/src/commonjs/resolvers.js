const { Module } = require('oro:module')
const dgram = require('oro:dgram')
const test = require('oro:test')
const fs = require('oro:fs')

test('commonjs - custom resolver - mapping', (t) => {
  Module.resolvers.push((specifier, ctx, next) => {
    if (specifier === 'filesystem') {
      return next('oro:fs')
    }

    if (specifier === 'udp') {
      return next('oro:dgram')
    }

    return next(specifier)
  })

  t.equal(require('filesystem'), fs, 'filesystem -> fs')
  t.equal(require('udp'), dgram, 'udp -> dgram')
})

test('commonjs - custom resolver - virtual', (t) => {
  const virtual = { key: 'value' }
  Module.resolvers.push((specifier, ctx, next) => {
    if (specifier === 'virtual') {
      return virtual
    }

    return next(specifier)
  })

  t.equal(require('virtual'), virtual, "require('virtual') -> virtual")
})
