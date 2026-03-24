import React from './react.js'
import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
  CardDescription,
  Toolbar,
  ToolbarGroup,
  ToolbarTitle,
  ToolbarDescription,
  Separator,
  cn
} from './components.js'

const { createElement: h, Fragment, useEffect } = React

export function ExampleLayout ({
  title,
  description,
  badge,
  toolbarActions,
  aside,
  footer,
  children,
  className
}) {
  useEffect(() => {
    if (!title) return
    document.title = `Socket Examples · ${title}`
  }, [title])

  return h(
    'div',
    { className: cn('ui-page', className) },
    h(
      'header',
      { className: 'ui-page__header' },
      h(
        Toolbar,
        null,
        h(
          ToolbarGroup,
          null,
          title &&
            h(
              ToolbarTitle,
              null,
              title,
              badge ? h('span', { className: 'ui-page__badge' }, badge) : null
            ),
          description && h(ToolbarDescription, null, description)
        ),
        toolbarActions &&
          h(ToolbarGroup, { className: 'ui-page__actions' }, toolbarActions)
      ),
      h(Separator, { className: 'ui-page__divider' })
    ),
    h(
      'div',
      { className: 'ui-page__body' },
      h(
        'main',
        {
          className: cn('ui-page__content', !aside && 'ui-page__content--full')
        },
        children
      ),
      aside && h('aside', { className: 'ui-page__aside' }, aside)
    ),
    footer && h('footer', { className: 'ui-page__footer' }, footer)
  )
}

export function ExamplePanel ({
  title,
  description,
  accent,
  children,
  footer,
  actions,
  className,
  header
}) {
  return h(
    Card,
    { className: cn('ui-panel', className) },
    h(
      CardHeader,
      { className: 'ui-panel__header' },
      header ||
        h(
          Fragment,
          null,
          title && h(CardTitle, null, title),
          description && h(CardDescription, null, description),
          actions && h('div', { className: 'ui-panel__actions' }, actions)
        ),
      accent &&
        h('span', {
          className: cn('ui-panel__accent', `ui-panel__accent--${accent}`)
        })
    ),
    h(CardContent, { className: 'ui-panel__content' }, children),
    footer && h(CardContent, { className: 'ui-panel__footer' }, footer)
  )
}

export function ExampleGrid ({ columns = 2, className, children }) {
  const style = { '--ui-grid-columns': columns }
  return h('div', { className: cn('ui-grid', className), style }, children)
}

export function ExampleStack ({ gap = 'lg', className, children }) {
  return h(
    'div',
    { className: cn('ui-stack', gap && `ui-stack--${gap}`, className) },
    children
  )
}

export function ExampleSection ({
  title,
  description,
  actions,
  children,
  className
}) {
  return h(
    'section',
    { className: cn('ui-section', className) },
    (title || description || actions) &&
      h(
        'header',
        { className: 'ui-section__header' },
        h(
          'div',
          { className: 'ui-section__meta' },
          title && h('h3', { className: 'ui-section__title' }, title),
          description &&
            h('p', { className: 'ui-section__description' }, description)
        ),
        actions && h('div', { className: 'ui-section__actions' }, actions)
      ),
    h('div', { className: 'ui-section__body' }, children)
  )
}

export function ExampleProse ({ className, children }) {
  return h('div', { className: cn('ui-prose', className) }, children)
}
