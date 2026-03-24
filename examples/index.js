import React from './ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExampleSection,
  ExampleGrid,
  ExampleStack,
  Card,
  CardHeader,
  CardTitle,
  CardDescription,
  CardContent,
  CardFooter,
  Badge,
  cn
} from './ui/index.js'

const { createElement: h, useMemo } = React

const GROUPS = [
  {
    title: 'AI & Control Surfaces',
    description:
      'Pair local models and MCP tooling with polished control planes.',
    accent: 'ocean',
    examples: [
      {
        slug: 'ai-chat',
        title: 'AI Chat',
        summary:
          'Talk to a local LLM served by the Oro Runtime. Load models from disk or attach to ones you have already initialised.',
        href: './ai-chat/',
        tags: ['AI', 'MCP'],
        accent: 'ocean'
      },
      {
        slug: 'ann',
        title: 'ANN Playground',
        summary:
          'Train and evaluate feed-forward classifiers using the oro:ai/ann runtime service.',
        href: './ann/',
        tags: ['AI', 'Model training'],
        accent: 'forest'
      },
      {
        slug: 'whisper',
        title: 'Whisper Transcription Studio',
        summary:
          'Load Whisper models, drop in audio, and inspect both streaming segments and final transcripts from the embedded whisper.cpp runtime.',
        href: './whisper/',
        tags: ['AI', 'Speech-to-text'],
        accent: 'aurora'
      },
      {
        slug: 'mcp-control',
        title: 'MCP Control Center',
        summary:
          'Start a local MCP server, inspect its status, and review activity published to remote clients.',
        href: './mcp-control/',
        tags: ['MCP', 'Server orchestration'],
        accent: 'violet'
      },
      {
        slug: 'kitchen-sink',
        title: 'Kitchen Sink',
        summary:
          'A grab bag of runtime capabilities that you can toggle on demand. Each control imports the relevant example lazily.',
        href: './kitchen-sink/',
        tags: ['Overview', 'Dynamic imports'],
        accent: 'sunrise'
      }
    ]
  },
  {
    title: 'Application Lifecycle & Surface Integration',
    description:
      'Wire desktop behaviours into the runtime: lifecycle hooks, deep links, and window chrome.',
    accent: 'amber',
    examples: [
      {
        slug: 'application',
        title: 'Application Deep Links',
        summary:
          'Observe URL routing events delivered via oro:hooks.onApplicationURL.',
        href: './application/',
        tags: ['Deep links', 'Hooks'],
        accent: 'amber'
      },
      {
        slug: 'lifecycle',
        title: 'Application Lifecycle',
        summary:
          'Listen for runtime lifecycle events and dispatch simulated transitions.',
        href: './lifecycle/',
        tags: ['Lifecycle', 'Desktop'],
        accent: 'violet'
      },
      {
        slug: 'window',
        title: 'Window Toolkit',
        summary:
          'Control window appearance, drive file pickers, and share data across windows.',
        href: './window/appearance.html',
        tags: ['Desktop', 'UI'],
        accent: 'aurora',
        links: [
          { label: 'Appearance', href: './window/appearance.html' },
          { label: 'File pickers', href: './window/file-pickers.html' },
          { label: 'Window messaging', href: './window/messaging.html' }
        ]
      },
      {
        slug: 'navigator-mounts',
        title: 'Navigator Mounts',
        summary:
          'Expose host directories to the runtime via navigator mounts and inspect the served files.',
        href: './navigator-mounts/',
        tags: ['Filesystem', 'Navigator'],
        accent: 'ocean'
      }
    ]
  },
  {
    title: 'System Bridges & Device APIs',
    description:
      'Interact with system buses, sensors, and device capabilities from JS.',
    accent: 'teal',
    examples: [
      {
        slug: 'dbus',
        title: 'DBus Demo',
        summary:
          'Exercise the Oro Runtime DBus bridge by connecting to the session bus, exporting objects, and handling method calls.',
        href: './dbus/',
        tags: ['System bus', 'IPC'],
        accent: 'teal'
      },
      {
        slug: 'usb',
        title: 'WebUSB Control Center',
        summary:
          'Authorize devices, drive custom chooser flows, and inspect runtime WebUSB permissions.',
        href: './usb/',
        tags: ['USB', 'Devices'],
        accent: 'aurora'
      },
      {
        slug: 'media',
        title: 'Media Devices',
        summary:
          'Capture camera and microphone streams with lifecycle-aware clean-up.',
        href: './media/',
        tags: ['Camera', 'Microphone'],
        accent: 'sunrise'
      },
      {
        slug: 'geolocation',
        title: 'Geolocation Watch',
        summary:
          'Request permission, stream positions, and observe lifecycle-aware behaviour.',
        href: './geolocation/',
        tags: ['Sensors', 'Permissions'],
        accent: 'forest'
      },
      {
        slug: 'notifications',
        title: 'Notifications',
        summary:
          'Exercise the notification API, track permission state, and observe user responses.',
        href: './notifications/',
        tags: ['UX', 'Permissions'],
        accent: 'amber'
      }
    ]
  },
  {
    title: 'Data & Storage',
    description:
      'Persist structured datasets, manage secrets, and explore runtime-backed storage layers.',
    accent: 'sunrise',
    columns: 2,
    examples: [
      {
        slug: 'sqlite',
        title: 'SQLite Notebook',
        summary:
          'Seed an in-memory dataset, execute ad-hoc SQL, and inspect result metadata from the native SQLite engine.',
        href: './sqlite/',
        tags: ['Database', 'Storage'],
        accent: 'forest'
      },
      {
        slug: 'secure-storage',
        title: 'Secure Storage Vault',
        summary:
          'Encrypt scoped key/value pairs, switch namespaces, and retrieve payloads with multiple encodings.',
        href: './secure-storage/',
        tags: ['Security', 'Secrets'],
        accent: 'violet'
      },
      {
        slug: 'asn1',
        title: 'ASN.1 Playground',
        summary:
          'Parse ASN.1 definitions with oro:asn1 and inspect module trees, constraints, and raw JSON output.',
        href: './asn1/',
        tags: ['Encoding', 'Parsing'],
        accent: 'teal'
      },
      {
        slug: 'ipfs',
        title: 'IPFS Workbench',
        summary:
          'Start the embedded libipfs node, add local files, and retrieve them through the runtime-managed API.',
        href: './ipfs/',
        tags: ['IPFS', 'Storage'],
        accent: 'aurora'
      }
    ]
  },
  {
    title: 'Networking & Service Workers',
    description:
      'Stream data, proxy requests, and broadcast datagrams using runtime primitives.',
    accent: 'violet',
    examples: [
      {
        slug: 'background-scheduler',
        title: 'Background Scheduler',
        summary:
          'Demonstrates the background service registration API with interval scheduling and UI logging.',
        href: './background-scheduler/',
        tags: ['Background', 'Workers'],
        accent: 'sunrise'
      },
      {
        slug: 'service-worker',
        title: 'Service Worker Basics',
        summary:
          'Register a service worker, exercise fetch handlers, and observe runtime output.',
        href: './service-worker/',
        tags: ['Service worker', 'Fetch'],
        accent: 'violet'
      },
      {
        slug: 'sse',
        title: 'Service Worker SSE',
        summary:
          'Proxy server-sent events through a service worker and observe delivery in real time.',
        href: './sse/',
        tags: ['Streaming', 'Service worker'],
        accent: 'aurora'
      },
      {
        slug: 'streaming',
        title: 'Streaming via Service Worker',
        summary:
          'Stream chunked responses intercepted by a service worker and inspect delivery timing.',
        href: './streaming/',
        tags: ['Streaming', 'Service worker'],
        accent: 'ocean'
      },
      {
        slug: 'dgram',
        title: 'UDP Multicast Echo',
        summary:
          'Send a UDP multicast datagram and verify local loopback using oro:dgram.',
        href: './dgram/',
        tags: ['Networking', 'UDP'],
        accent: 'sunrise'
      }
    ]
  },
  {
    title: 'Native & CLI References',
    description:
      'CLI-first flows and scripts that accompany the bundled desktop browser.',
    accent: 'forest',
    columns: 2,
    examples: [
      {
        slug: 'iroh',
        title: 'Iroh Connectivity Demo',
        summary:
          'Drive paired endpoints from an interactive controller, exchange datagrams/streams, and reuse the CLI workflow.',
        href: './iroh/',
        tags: ['Networking', 'Iroh'],
        accent: 'forest',
        links: [{ label: 'Open demo script', href: './iroh/demo.js' }],
        cta: 'Open controller'
      },
      {
        slug: 'tls',
        title: 'TLS Examples',
        summary:
          'Minimal TLS echo server and client using oro:tls with optional mutual TLS support.',
        href: './tls/',
        tags: ['Networking', 'CLI'],
        accent: 'violet',
        links: [
          { label: 'TLS server script', href: './tls/server.mjs' },
          { label: 'TLS client script', href: './tls/client.mjs' },
          { label: 'mTLS server script', href: './tls/server-mtls.mjs' },
          { label: 'mTLS client script', href: './tls/client-mtls.mjs' },
          { label: 'README', href: './tls/README.md' }
        ],
        cta: 'Open walkthrough'
      },
      {
        slug: 'orosh',
        title: 'orosh Virtual Terminal',
        summary:
          'Full-screen xterm shell with pipelines, redirection, and a writable POSIX tree rooted at path.DATA.',
        href: './orosh/',
        tags: ['Terminal', 'Shell'],
        accent: 'sunrise',
        cta: 'Launch terminal'
      }
    ]
  }
]

function useHeroActions () {
  return useMemo(
    () => [
      {
        label: 'Read the docs',
        href: 'https://oro.computer/runtime/docs',
        variant: 'primary'
      },
      {
        label: 'View source on GitHub',
        href: 'https://github.com/oro-computer/legacy-runtime',
        variant: 'ghost'
      }
    ],
    []
  )
}

function ExampleTile ({ example }) {
  const activateExample = () => {
    const href = example.href
    if (!href) return
    window.location.href = href
  }

  const handleClick = (event) => {
    if (event.defaultPrevented) return
    if (typeof event.button === 'number' && event.button !== 0) return
    const anchor =
      event.target && typeof event.target.closest === 'function'
        ? event.target.closest('a')
        : null
    if (anchor && anchor !== event.currentTarget) return
    if (event.metaKey || event.ctrlKey) {
      event.preventDefault()
      window.open(example.href, '_blank', 'noopener,noreferrer')
      return
    }
    if (event.preventDefault) event.preventDefault()
    activateExample()
  }

  const handleKeyDown = (event) => {
    if (event.defaultPrevented) return
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault()
      activateExample()
    }
  }

  return h(
    'div',
    { className: 'landing-card' },
    h(
      Card,
      {
        className: cn(
          'landing-card__surface',
          example.accent && `landing-card__surface--${example.accent}`
        ),
        role: 'link',
        tabIndex: 0,
        'aria-label': example.title,
        onClick: handleClick,
        onKeyDown: handleKeyDown
      },
      h(
        CardHeader,
        { className: 'landing-card__header' },
        example.tags?.length
          ? h(
            'div',
            { className: 'landing-card__tags' },
            example.tags.map((tag) =>
              h(
                Badge,
                {
                  key: tag,
                  className: 'landing-card__tag',
                  variant: 'neutral'
                },
                tag
              )
            )
          )
          : null,
        h(
          CardTitle,
          null,
          h(
            'a',
            {
              href: example.href,
              className: 'landing-card__title-link'
            },
            example.title
          )
        ),
        h(CardDescription, null, example.summary)
      ),
      example.links?.length
        ? h(
          CardContent,
          { className: 'landing-card__links' },
          h(
            'div',
            { className: 'landing-card__link-list' },
            example.links.map((link) =>
              h(
                'a',
                {
                  key: `${example.slug}-${link.href}`,
                  href: link.href,
                  className: 'landing-card__link'
                },
                link.label
              )
            )
          )
        )
        : null,
      h(
        CardFooter,
        { className: 'landing-card__footer' },
        h(
          'span',
          { className: 'landing-card__footer-text' },
          example.cta || 'Open example'
        ),
        h('span', { className: 'landing-card__footer-icon' }, '>')
      )
    )
  )
}

function ExamplesLandingPage () {
  const heroActions = useHeroActions()

  const hero = h(
    'section',
    { className: 'landing-hero' },
    h(
      'div',
      { className: 'landing-hero__copy' },
      h('p', { className: 'landing-hero__eyebrow' }, 'Your runtime playground'),
      h(
        'h1',
        { className: 'landing-hero__title' },
        'Discover what the Oro Runtime can do'
      ),
      h(
        'p',
        { className: 'landing-hero__lede' },
        'Browse interactive tiles grouped by capability. Launch the demos, inspect the source, and adapt the patterns for your own applications.'
      )
    ),
    h(
      ExampleStack,
      { gap: 'md', className: 'landing-hero__actions' },
      heroActions.map((action) =>
        h(
          'a',
          {
            key: action.href,
            href: action.href,
            className: cn(
              'landing-hero__cta',
              action.variant === 'ghost' && 'landing-hero__cta--ghost'
            ),
            target: action.href.startsWith('http') ? '_blank' : null,
            rel: action.href.startsWith('http') ? 'noreferrer noopener' : null
          },
          action.label
        )
      )
    )
  )

  const sections = GROUPS.map((group) => {
    return h(
      ExampleSection,
      {
        key: group.title,
        title: group.title,
        description: group.description
      },
      h(
        ExampleGrid,
        {
          columns: group.columns || 3,
          className: 'landing-grid'
        },
        group.examples.map((example) =>
          h(ExampleTile, {
            key: example.slug,
            example
          })
        )
      )
    )
  })

  return h(
    ExampleLayout,
    {
      title: 'Oro Runtime Examples',
      description:
        'Explore polished demos that highlight platform integrations, service workers, networking, and MCP automation in the Oro Runtime.'
    },
    hero,
    ...sections
  )
}

mountExample(ExamplesLandingPage)
