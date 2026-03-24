import test from 'oro:test'
import * as NAT from 'oro:latica/nat'

test('NAT connectionStrategy matrix', async (t) => {
  // UNRESTRICTED destination
  t.equal(
    NAT.connectionStrategy(NAT.UNRESTRICTED, NAT.UNRESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.ADDR_RESTRICTED, NAT.UNRESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )

  // ADDR_RESTRICTED destination
  t.equal(
    NAT.connectionStrategy(NAT.UNRESTRICTED, NAT.ADDR_RESTRICTED),
    NAT.STRATEGY_DEFER
  )
  t.equal(
    NAT.connectionStrategy(NAT.ADDR_RESTRICTED, NAT.ADDR_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.PORT_RESTRICTED, NAT.ADDR_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.ENDPOINT_RESTRICTED, NAT.ADDR_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )

  // PORT_RESTRICTED destination
  t.equal(
    NAT.connectionStrategy(NAT.UNRESTRICTED, NAT.PORT_RESTRICTED),
    NAT.STRATEGY_DEFER
  )
  t.equal(
    NAT.connectionStrategy(NAT.ADDR_RESTRICTED, NAT.PORT_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.PORT_RESTRICTED, NAT.PORT_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.ENDPOINT_RESTRICTED, NAT.PORT_RESTRICTED),
    NAT.STRATEGY_TRAVERSAL_CONNECT
  )

  // ENDPOINT_RESTRICTED destination
  t.equal(
    NAT.connectionStrategy(NAT.UNRESTRICTED, NAT.ENDPOINT_RESTRICTED),
    NAT.STRATEGY_DEFER
  )
  t.equal(
    NAT.connectionStrategy(NAT.ADDR_RESTRICTED, NAT.ENDPOINT_RESTRICTED),
    NAT.STRATEGY_DIRECT_CONNECT
  )
  t.equal(
    NAT.connectionStrategy(NAT.PORT_RESTRICTED, NAT.ENDPOINT_RESTRICTED),
    NAT.STRATEGY_TRAVERSAL_OPEN
  )
  t.equal(
    NAT.connectionStrategy(NAT.ENDPOINT_RESTRICTED, NAT.ENDPOINT_RESTRICTED),
    NAT.STRATEGY_PROXY
  )
})
