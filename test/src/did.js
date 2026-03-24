import test from 'oro:test'
import DID, {
  DIDURL,
  DIDDocument,
  Resolver,
  constants,
  DIDError,
  parse,
  parseUrl,
  validateDocument,
  normalize,
  normalizeUrl,
  isValid,
  isValidUrl
} from 'oro:did'

const SAMPLE_DID = 'did:example:123456'

const SAMPLE_DOCUMENT = {
  '@context': 'https://www.w3.org/ns/did/v1',
  id: SAMPLE_DID,
  alsoKnownAs: ['https://example.com/alice', 'did:example:alice'],
  controller: SAMPLE_DID,
  verificationMethod: [
    {
      id: '#key-1',
      type: 'Ed25519VerificationKey2018',
      controller: SAMPLE_DID,
      publicKeyMultibase: 'z6Mkh5k4bE3S1d14e6'
    }
  ],
  authentication: ['#key-1'],
  assertionMethod: ['#key-1'],
  service: [
    {
      id: '#agent',
      type: 'AgentService',
      serviceEndpoint: 'https://agent.example.com/endpoint'
    }
  ]
}

test('did.parse normalizes method name and percent encoding', (t) => {
  const did = parse('did:Example:abc%2f123')
  t.equal(did.method, 'example', 'method normalized to lowercase')
  t.equal(
    did.methodSpecificId,
    'abc%2F123',
    'percent encoding preserved in uppercase hex'
  )
  t.equal(
    did.toString(),
    'did:example:abc%2F123',
    'string serialization uses normalized form'
  )
  t.ok(isValid(did.toString()), 'normalized DID reports as valid')
})

test('did.normalize accepts DIDURL input', (t) => {
  const did = new DID('did:example:1234')
  const url = new DIDURL({
    did: did.toString(),
    path: '/resource',
    query: 'versionId=1'
  })
  t.equal(normalize(url), did.toString(), 'normalize strips DID URL components')
})

test('DIDURL.parse parses parameters, path, query, fragment', (t) => {
  const input =
    'did:example:123456;service=agent;hl=fr/path/to/resource?versionId=1#keys-1'
  const url = parseUrl(input)
  t.equal(
    url.parameters.get('service')[0],
    'agent',
    'service parameter decoded'
  )
  t.equal(url.parameters.get('hl')[0], 'fr', 'hl parameter decoded')
  t.equal(url.path, '/path/to/resource', 'path preserved')
  t.equal(
    url.queryParams.get('versionId'),
    '1',
    'query parameter parsed via URLSearchParams'
  )
  t.equal(url.fragment, 'keys-1', 'fragment stored without leading hash')
  t.equal(url.toString(), input, 'toString round-trips original DID URL')

  const withVersionTime = url.withParameter(
    'versionTime',
    '2021-05-24T08:41:10Z'
  )
  t.equal(
    withVersionTime.parameters.get('versionTime')[0],
    '2021-05-24T08:41:10Z',
    'withParameter stores ISO timestamp'
  )
  t.ok(
    withVersionTime
      .toString()
      .includes(';versionTime=2021-05-24T08%3A41%3A10Z'),
    'serialized DID URL encodes reserved characters'
  )

  const withoutService = url.withoutParameter('service')
  t.notOk(
    withoutService.parameters.has('service'),
    'withoutParameter removes entry'
  )
  t.equal(
    url.parameters.get('service')[0],
    'agent',
    'original DIDURL remains immutable'
  )
})

test('DIDURL.parameters is immutable', (t) => {
  const url = parseUrl('did:example:123456;service=agent')
  const parameters = url.parameters
  t.throws(
    () => parameters.set('service', ['other']),
    /Cannot modify frozen DIDURL parameters/,
    'set throws on frozen parameters map'
  )
  t.throws(
    () => parameters.delete('service'),
    /Cannot modify frozen DIDURL parameters/,
    'delete throws on frozen parameters map'
  )
  t.throws(
    () => parameters.clear(),
    /Cannot modify frozen DIDURL parameters/,
    'clear throws on frozen parameters map'
  )

  const serviceValues = parameters.get('service')
  t.ok(Array.isArray(serviceValues), 'service parameter returns an array')
  t.throws(
    () => serviceValues.push('new'),
    undefined,
    'array mutation is prevented'
  )
})

test('validateDocument enforces DID document requirements', (t) => {
  const result = validateDocument(SAMPLE_DOCUMENT)
  t.ok(result.ok, 'valid document passes validation')
  t.equal(result.normalized.id, SAMPLE_DID, 'document id normalized')
  t.equal(
    result.normalized.verificationMethod[0].id,
    `${SAMPLE_DID}#key-1`,
    'relative verification method id expanded'
  )
  t.ok(
    result.normalized.alsoKnownAs.includes('did:example:alice'),
    'alsoKnownAs accepts DID URIs'
  )
  t.equal(
    result.normalized.service[0].id,
    `${SAMPLE_DID}#agent`,
    'service id normalized to absolute DID URL'
  )

  const invalid = validateDocument({ id: 'did:example:789' })
  t.notOk(invalid.ok, 'missing @context fails validation')
  t.ok(
    invalid.errors.some((error) => error.path === '@context'),
    '@context error reported'
  )
})

test('DIDDocument helper mutates safely', (t) => {
  const doc = new DIDDocument(SAMPLE_DOCUMENT)
  doc.addService({
    id: '#messaging',
    type: 'MessagingService',
    serviceEndpoint: 'https://example.com/messaging'
  })
  t.ok(
    doc.services.some((service) => service.id.endsWith('#messaging')),
    'new service added'
  )
  doc.addService({
    id: 'urn:uuid:123e4567-e89b-12d3-a456-426614174000',
    type: 'AgentService',
    serviceEndpoint: 'https://example.com/urn'
  })
  t.ok(
    doc.services.some(
      (service) =>
        service.id === 'urn:uuid:123e4567-e89b-12d3-a456-426614174000'
    ),
    'service accepts absolute URIs with custom schemes'
  )
  doc.removeService(`${SAMPLE_DID}#messaging`)
  t.notOk(
    doc.services.some((service) => service.id.endsWith('#messaging')),
    'service removal works'
  )

  doc.addToRelationship('assertionMethod', '#key-1')
  t.ok(
    doc.toJSON().assertionMethod?.includes(`${SAMPLE_DID}#key-1`),
    'relationship references normalized ids'
  )
  doc.addToRelationship('authentication', '#key-1')
  doc.removeFromRelationship('authentication', '#key-1')
  t.notOk(
    doc.toJSON().authentication?.includes(`${SAMPLE_DID}#key-1`),
    'removeFromRelationship handles relative ids'
  )

  doc.removeVerificationMethod('#key-1')
  t.equal(
    doc.verificationMethod.length,
    0,
    'removeVerificationMethod removes entries regardless of id form'
  )
  t.notOk(
    doc.toJSON().assertionMethod?.includes(`${SAMPLE_DID}#key-1`),
    'relationships updated after removing verification method'
  )
})

const testDriver = {
  async resolve (_did) {
    return {
      didResolutionMetadata: { contentType: 'application/did+json' },
      didDocumentMetadata: { updated: '2023-01-01T00:00:00Z' },
      didDocument: SAMPLE_DOCUMENT
    }
  }
}

test('Resolver resolves, renders representations, and dereferences', async (t) => {
  const resolver = new Resolver({ drivers: { example: testDriver } })
  const resolution = await resolver.resolve(SAMPLE_DID)
  t.equal(
    resolution.didDocument?.id,
    SAMPLE_DID,
    'resolution returns normalized DID document'
  )
  t.equal(
    resolution.didDocumentMetadata.updated,
    '2023-01-01T00:00:00Z',
    'metadata preserved'
  )

  const representation = await resolver.resolveRepresentation(SAMPLE_DID)
  t.ok(
    representation.didDocumentStream instanceof Uint8Array ||
      typeof representation.didDocumentStream === 'string',
    'representation returns a byte stream or string'
  )
  t.equal(
    representation.didResolutionMetadata.contentType,
    'application/did+json',
    'content type defaults to DID JSON when unspecified'
  )

  const dereferenced = await resolver.dereference(`${SAMPLE_DID};service=agent`)
  t.equal(
    dereferenced.dereferencingMetadata.contentType,
    'application/json',
    'dereference infers JSON content'
  )
  t.equal(
    dereferenced.contentStream.id,
    `${SAMPLE_DID}#agent`,
    'dereferenced service returned'
  )
})

test('constants expose core specification metadata', (t) => {
  t.ok(
    constants.CORE_VERIFICATION_RELATIONSHIPS.includes('authentication'),
    'core relationships exposed'
  )
  t.ok(
    constants.STANDARD_PARAMETERS.includes('service'),
    'standard parameters list includes service'
  )
  t.ok(
    isValidUrl(`${SAMPLE_DID}#key-1`),
    'isValidUrl accepts DID URL fragments'
  )
  t.notOk(isValid('not-a-did'), 'isValid rejects invalid DID strings')
  t.equal(
    normalizeUrl('did:example:123#frag'),
    'did:example:123#frag',
    'normalizeUrl returns canonical DID URL string'
  )
})

test('DIDDocument setter validation propagates errors', (t) => {
  const doc = new DIDDocument(SAMPLE_DOCUMENT)
  t.throws(
    () => doc.setAlsoKnownAs(['not a uri']),
    /invalid_alsoKnownAs/,
    'setAlsoKnownAs throws for invalid URI'
  )
  t.throws(
    () => doc.setControllers(['not-a-did']),
    /invalid_controller/,
    'setControllers throws for invalid DID value'
  )
})

test('Resolver accepts CBOR representation streams', async (t) => {
  const resolver = new Resolver({
    drivers: {
      example: {
        async resolveRepresentation () {
          return {
            didResolutionMetadata: { contentType: 'application/did+cbor' },
            didDocumentMetadata: {},
            didDocumentStream: new Uint8Array([0xa0]) // empty CBOR map
          }
        }
      }
    }
  })

  const representation =
    await resolver.resolveRepresentation('did:example:123456')
  t.ok(
    representation.didDocumentStream instanceof Uint8Array,
    'returns binary CBOR stream'
  )
  t.equal(
    representation.didResolutionMetadata.contentType,
    'application/did+cbor',
    'retains CBOR content type'
  )
})

test('Resolver rejects invalid representation streams', async (t) => {
  const resolver = new Resolver({
    drivers: {
      bad: {
        async resolveRepresentation () {
          return {
            didResolutionMetadata: { contentType: 'application/did+json' },
            didDocumentMetadata: {},
            didDocumentStream: { invalid: true }
          }
        }
      }
    }
  })

  try {
    await resolver.resolveRepresentation('did:bad:123')
    t.fail('resolveRepresentation should reject for invalid stream')
  } catch (error) {
    t.ok(error instanceof DIDError, 'throws DIDError instance')
    t.equal(
      error.code,
      'invalid_representation_stream',
      'propagates invalid representation error code'
    )
  }
})

test('Resolver rejects malformed JSON representation streams', async (t) => {
  const resolver = new Resolver({
    drivers: {
      badjson: {
        async resolveRepresentation () {
          return {
            didResolutionMetadata: { contentType: 'application/did+json' },
            didDocumentMetadata: {},
            didDocumentStream: '{ not valid json '
          }
        }
      }
    }
  })

  try {
    await resolver.resolveRepresentation('did:badjson:123')
    t.fail('resolveRepresentation should reject for malformed JSON text')
  } catch (error) {
    t.ok(error instanceof DIDError, 'throws DIDError instance')
    t.equal(
      error.code,
      'invalid_representation_stream',
      'propagates invalid representation error code for malformed JSON'
    )
  }
})
