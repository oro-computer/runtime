import React from './react.js'
import { createRoot } from './react-dom-client.js'

export function mountExample (Component) {
  const container = document.getElementById('root')
  if (!container) throw new Error('Expected #root container in document')
  const root = createRoot(container)
  root.render(React.createElement(Component))
  return root
}

export function renderInto (container, Component) {
  if (!container) throw new Error('renderInto: missing container')
  const root = createRoot(container)
  root.render(React.createElement(Component))
  return root
}
