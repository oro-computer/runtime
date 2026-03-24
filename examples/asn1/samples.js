export const ASN1_SAMPLES = [
  {
    id: 'device-profile',
    title: 'Device profile schema',
    description:
      'Sequences with defaults, optional SET OF members, and enumerations.',
    source: `
DeviceProfile DEFINITIONS AUTOMATIC TAGS ::= BEGIN

EXPORTS Device, Sensor, SensorKind;

Device ::= SEQUENCE {
  id           PrintableString (SIZE (5..64)),
  firmware     FirmwareVersion,
  sensors      SET SIZE (1..32) OF Sensor OPTIONAL,
  lastSeen     GeneralizedTime OPTIONAL
}

FirmwareVersion ::= SEQUENCE {
  major     INTEGER (0..255),
  minor     INTEGER (0..255),
  patch     INTEGER (0..1023) DEFAULT 0,
  metadata  UTF8String OPTIONAL
}

Sensor ::= SEQUENCE {
  identifier   UTF8String (SIZE (1..128)),
  kind         SensorKind,
  unit         UTF8String OPTIONAL,
  threshold    REAL OPTIONAL,
  calibration  BIT STRING (SIZE (16)) OPTIONAL,
  enabled      BOOLEAN DEFAULT TRUE
}

SensorKind ::= ENUMERATED {
  temperature (0),
  humidity (1),
  pressure (2),
  motion (3),
  light (4),
  custom (255)
}

END
`.trim()
  },
  {
    id: 'telemetry',
    title: 'Telemetry payloads',
    description:
      'CHOICE types, SEQUENCE OF collections, and constrained strings.',
    source: `
Telemetry DEFINITIONS IMPLICIT TAGS ::= BEGIN

MeasuredValue ::= CHOICE {
  integerValue   INTEGER,
  floatValue     REAL,
  textValue      UTF8String,
  absent         NULL
}

Annotation ::= SEQUENCE {
  key     UTF8String (SIZE (1..32)),
  value   UTF8String OPTIONAL
}

Metric ::= SEQUENCE {
  name        UTF8String (SIZE (1..64)),
  timestamp   GeneralizedTime,
  value       MeasuredValue,
  quality     ENUMERATED { good (0), warn (1), bad (2) } DEFAULT good,
  annotations SEQUENCE SIZE (0..16) OF Annotation OPTIONAL
}

MetricsEnvelope ::= SEQUENCE {
  deviceId    OCTET STRING (SIZE (16)),
  payload     SEQUENCE OF Metric,
  signature   BIT STRING OPTIONAL
}

END
`.trim()
  },
  {
    id: 'certificate',
    title: 'Certificate template',
    description: 'Imports, object identifiers, nested choices, and defaults.',
    source: `
CertificateProfile DEFINITIONS EXPLICIT TAGS ::= BEGIN

IMPORTS
  AlgorithmIdentifier, SubjectPublicKeyInfo
    FROM PKIX1Explicit88
      { iso(1) identified-organization(3) dod(6) internet(1) security(5)
        mechanisms(5) pkix(7) id-mod(0) id-pkix1-explicit(18) };

CertificateTemplate ::= SEQUENCE {
  version      INTEGER { v1(0), v2(1), v3(2) } DEFAULT v3,
  serialNumber INTEGER,
  signature    AlgorithmIdentifier,
  issuer       Name,
  validity     Validity,
  subject      Name,
  subjectPublicKeyInfo SubjectPublicKeyInfo,
  extensions   Extensions OPTIONAL
}

Name ::= CHOICE {
  rdnSequence SEQUENCE OF RelativeDistinguishedName
}

RelativeDistinguishedName ::= SET SIZE (1..MAX) OF AttributeTypeAndValue

AttributeTypeAndValue ::= SEQUENCE {
  type   OBJECT IDENTIFIER,
  value  ANY
}

Validity ::= SEQUENCE {
  notBefore  Time,
  notAfter   Time
}

Time ::= CHOICE {
  utcTime        UTCTime,
  generalizedTime GeneralizedTime
}

Extensions ::= SEQUENCE SIZE (1..MAX) OF Extension

Extension ::= SEQUENCE {
  extnID     OBJECT IDENTIFIER,
  critical   BOOLEAN DEFAULT FALSE,
  extnValue  OCTET STRING
}

END
`.trim()
  }
]
