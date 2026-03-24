import React from './react.js'

const {
  createElement: h,
  useContext,
  useMemo,
  useState,
  useRef,
  forwardRef,
  useEffect
} = React

function toClassList (value) {
  if (!value) return []
  if (typeof value === 'string') return value.trim().split(/\s+/)
  if (Array.isArray(value)) return value.flatMap(toClassList)
  if (typeof value === 'object') {
    return Object.entries(value)
      .filter(([, ok]) => Boolean(ok))
      .map(([name]) => name)
  }
  return [`${value}`]
}

export function cn (...inputs) {
  return inputs.flatMap(toClassList).filter(Boolean).join(' ')
}

export const Button = forwardRef(function Button (
  { variant = 'default', size = 'md', className, ...props },
  ref
) {
  const classes = cn(
    'ui-button',
    `ui-button--${variant}`,
    `ui-button--${size}`,
    className
  )
  return h('button', { ...props, ref, className: classes })
})

export const Input = forwardRef(function Input ({ className, ...props }, ref) {
  return h('input', { ...props, ref, className: cn('ui-input', className) })
})

export const Textarea = forwardRef(function Textarea (
  { className, rows = 4, ...props },
  ref
) {
  return h('textarea', {
    ...props,
    ref,
    rows,
    className: cn('ui-textarea', className)
  })
})

export const Label = forwardRef(function Label ({ className, ...props }, ref) {
  return h('label', { ...props, ref, className: cn('ui-label', className) })
})

export const Select = forwardRef(function Select (
  { className, children, ...props },
  ref
) {
  return h(
    'select',
    { ...props, ref, className: cn('ui-select', className) },
    children
  )
})

export const Option = function Option ({ className, children, ...props }) {
  return h(
    'option',
    { ...props, className: cn('ui-option', className) },
    children
  )
}

export const Card = forwardRef(function Card ({ className, ...props }, ref) {
  return h('div', { ...props, ref, className: cn('ui-card', className) })
})

export const CardHeader = forwardRef(function CardHeader (
  { className, ...props },
  ref
) {
  return h('div', {
    ...props,
    ref,
    className: cn('ui-card__header', className)
  })
})

export const CardTitle = forwardRef(function CardTitle (
  { className, ...props },
  ref
) {
  return h('h2', { ...props, ref, className: cn('ui-card__title', className) })
})

export const CardDescription = forwardRef(function CardDescription (
  { className, ...props },
  ref
) {
  return h('p', {
    ...props,
    ref,
    className: cn('ui-card__description', className)
  })
})

export const CardContent = forwardRef(function CardContent (
  { className, ...props },
  ref
) {
  return h('div', {
    ...props,
    ref,
    className: cn('ui-card__content', className)
  })
})

export const CardFooter = forwardRef(function CardFooter (
  { className, ...props },
  ref
) {
  return h('div', {
    ...props,
    ref,
    className: cn('ui-card__footer', className)
  })
})

export function Badge ({ className, variant = 'neutral', ...props }) {
  const classes = cn('ui-badge', `ui-badge--${variant}`, className)
  return h('span', { ...props, className: classes })
}

export function Separator ({ className, direction = 'horizontal', ...props }) {
  const classes = cn(
    'ui-separator',
    direction === 'vertical' ? 'ui-separator--vertical' : '',
    className
  )
  return h('div', { ...props, role: 'separator', className: classes })
}

export function ScrollArea ({ className, children, ...props }) {
  return h(
    'div',
    { ...props, className: cn('ui-scroll-area', className) },
    children
  )
}

const TabsContext = React.createContext(null)

export function Tabs ({
  defaultValue,
  value,
  onValueChange,
  children,
  className
}) {
  const isControlled = value !== undefined
  const [internalValue, setInternalValue] = useState(defaultValue)
  const activeValue = isControlled ? value : internalValue
  const setValue = useMemo(() => {
    return (next) => {
      if (!isControlled) setInternalValue(next)
      if (onValueChange) onValueChange(next)
    }
  }, [isControlled, onValueChange])

  const ctx = useMemo(
    () => ({ value: activeValue, setValue }),
    [activeValue, setValue]
  )
  return h(
    TabsContext.Provider,
    { value: ctx },
    h('div', { className: cn('ui-tabs', className) }, children)
  )
}

export function TabsList ({ className, ...props }) {
  return h('div', {
    ...props,
    className: cn('ui-tabs__list', className),
    role: 'tablist'
  })
}

export function TabsTrigger ({ className, value, disabled = false, ...props }) {
  const ctx = useContext(TabsContext)
  if (!ctx) throw new Error('TabsTrigger must be used within <Tabs>')
  const { value: activeValue, setValue } = ctx
  const isActive = activeValue === value
  return h('button', {
    ...props,
    role: 'tab',
    type: 'button',
    disabled,
    'aria-selected': isActive,
    'data-state': isActive ? 'active' : 'inactive',
    onClick: (event) => {
      if (disabled) return
      if (props.onClick) props.onClick(event)
      setValue(value)
    },
    className: cn('ui-tabs__trigger', isActive && 'is-active', className)
  })
}

export function TabsContent ({ className, value, ...props }) {
  const ctx = useContext(TabsContext)
  if (!ctx) throw new Error('TabsContent must be used within <Tabs>')
  const isActive = ctx.value === value
  return h('div', {
    ...props,
    role: 'tabpanel',
    hidden: !isActive,
    'data-state': isActive ? 'active' : 'inactive',
    className: cn('ui-tabs__content', className, !isActive && 'is-hidden')
  })
}

export function Alert ({ className, variant = 'default', ...props }) {
  return h('div', {
    ...props,
    className: cn('ui-alert', `ui-alert--${variant}`, className),
    role: 'alert'
  })
}

export function AlertTitle ({ className, ...props }) {
  return h('div', { ...props, className: cn('ui-alert__title', className) })
}

export function AlertDescription ({ className, ...props }) {
  return h('div', {
    ...props,
    className: cn('ui-alert__description', className)
  })
}

const SwitchContext = React.createContext(null)

export function Switch ({
  checked: controlled,
  defaultChecked = false,
  onCheckedChange,
  disabled = false,
  className,
  label,
  ...props
}) {
  const isControlled = controlled !== undefined
  const [internalChecked, setInternalChecked] = useState(defaultChecked)
  const checked = isControlled ? controlled : internalChecked
  const buttonRef = useRef(null)

  const setChecked = (next) => {
    if (!isControlled) setInternalChecked(next)
    if (onCheckedChange) onCheckedChange(next)
  }

  const toggle = () => {
    if (disabled) return
    setChecked(!checked)
  }

  const onKeyDown = (event) => {
    if (event.key === ' ' || event.key === 'Enter') {
      event.preventDefault()
      toggle()
    }
  }

  useEffect(() => {
    if (!buttonRef.current) return
    buttonRef.current.setAttribute('aria-checked', checked ? 'true' : 'false')
  }, [checked])

  const ctx = useMemo(
    () => ({ checked, toggle, disabled }),
    [checked, disabled]
  )

  return h(
    SwitchContext.Provider,
    { value: ctx },
    h(
      'button',
      {
        ...props,
        type: 'button',
        role: 'switch',
        ref: buttonRef,
        'aria-checked': checked,
        'aria-label': label,
        disabled,
        onClick: toggle,
        onKeyDown,
        className: cn(
          'ui-switch',
          checked && 'is-active',
          disabled && 'is-disabled',
          className
        )
      },
      h('span', { className: 'ui-switch__thumb' })
    )
  )
}

export function Checkbox ({
  checked,
  defaultChecked = false,
  onChange,
  disabled = false,
  className,
  ...props
}) {
  const isControlled = checked !== undefined
  const [internal, setInternal] = useState(defaultChecked)
  const value = isControlled ? checked : internal
  return h(
    'label',
    { className: cn('ui-checkbox', disabled && 'is-disabled', className) },
    h('input', {
      ...props,
      type: 'checkbox',
      checked: value,
      disabled,
      onChange: (event) => {
        if (!isControlled) setInternal(event.target.checked)
        if (onChange) onChange(event)
      }
    }),
    h('span', { className: 'ui-checkbox__box' }),
    props.children &&
      h('span', { className: 'ui-checkbox__label' }, props.children)
  )
}

export function FormField ({
  label,
  description,
  htmlFor,
  required = false,
  children,
  className,
  hint
}) {
  return h(
    'div',
    { className: cn('ui-field', className) },
    label &&
      h(
        Label,
        { htmlFor, className: 'ui-field__label' },
        label,
        required ? h('span', { className: 'ui-field__required' }, '*') : null
      ),
    description && h('p', { className: 'ui-field__description' }, description),
    h('div', { className: 'ui-field__control' }, children),
    hint && h('p', { className: 'ui-field__hint' }, hint)
  )
}

export function StatusPill ({ status = 'idle', children, className }) {
  return h(
    'span',
    { className: cn('ui-status', `ui-status--${status}`, className) },
    children || status
  )
}

export function EmptyState ({ icon, title, description, actions, className }) {
  return h(
    'div',
    { className: cn('ui-empty', className) },
    icon && h('div', { className: 'ui-empty__icon' }, icon),
    title && h('h3', { className: 'ui-empty__title' }, title),
    description && h('p', { className: 'ui-empty__description' }, description),
    actions && h('div', { className: 'ui-empty__actions' }, actions)
  )
}

export function Toolbar ({ className, children, ...props }) {
  return h(
    'div',
    { ...props, className: cn('ui-toolbar', className) },
    children
  )
}

export function ToolbarGroup ({ className, children, ...props }) {
  return h(
    'div',
    { ...props, className: cn('ui-toolbar__group', className) },
    children
  )
}

export function ToolbarTitle ({ className, ...props }) {
  return h('h1', { ...props, className: cn('ui-toolbar__title', className) })
}

export function ToolbarDescription ({ className, ...props }) {
  return h('p', {
    ...props,
    className: cn('ui-toolbar__description', className)
  })
}

export function Code ({ className, children, ...props }) {
  return h(
    'pre',
    { ...props, className: cn('ui-code', className) },
    h('code', null, children)
  )
}

export function InlineCode ({ className, ...props }) {
  return h('code', { ...props, className: cn('ui-inline-code', className) })
}

export function Table ({ className, ...props }) {
  return h(
    'div',
    { className: cn('ui-table__wrapper', className) },
    h('table', { ...props, className: 'ui-table' })
  )
}

export function TableHead ({ className, ...props }) {
  return h('thead', { ...props, className: cn('ui-table__head', className) })
}

export function TableBody ({ className, ...props }) {
  return h('tbody', { ...props, className: cn('ui-table__body', className) })
}

export function TableRow ({ className, ...props }) {
  return h('tr', { ...props, className: cn('ui-table__row', className) })
}

export function TableHeader ({ className, ...props }) {
  return h('th', { ...props, className: cn('ui-table__header', className) })
}

export function TableCell ({ className, ...props }) {
  return h('td', { ...props, className: cn('ui-table__cell', className) })
}

export function LiveRegion ({
  as: Element = 'div',
  role = 'status',
  polite = true,
  assertive = false,
  className,
  children,
  ...props
}) {
  const ariaLive = assertive ? 'assertive' : polite ? 'polite' : undefined
  return h(
    Element,
    {
      ...props,
      role,
      'aria-live': ariaLive,
      className: cn('ui-live-region', className)
    },
    children
  )
}

export function StatusBanner ({
  title,
  message,
  prefix,
  actions,
  variant = 'info',
  className,
  role = 'status',
  polite = true,
  assertive = false,
  ...props
}) {
  return h(
    LiveRegion,
    {
      ...props,
      role,
      polite,
      assertive,
      className: cn(
        'ui-status-banner',
        `ui-status-banner--${variant}`,
        className
      )
    },
    h(
      'div',
      { className: 'ui-status-banner__body' },
      prefix && h('span', { className: 'ui-status-banner__prefix' }, prefix),
      h(
        'div',
        { className: 'ui-status-banner__content' },
        title && h('h3', { className: 'ui-status-banner__title' }, title),
        message && h('p', { className: 'ui-status-banner__message' }, message)
      ),
      actions && h('div', { className: 'ui-status-banner__actions' }, actions)
    )
  )
}

export function LogViewer ({
  entries = [],
  renderEntry,
  emptyState,
  className,
  labelledBy,
  autoScroll = true,
  ...props
}) {
  const containerRef = useRef(null)

  useEffect(() => {
    if (!autoScroll || !containerRef.current) return
    containerRef.current.scrollTop = containerRef.current.scrollHeight
  }, [entries, autoScroll])

  let content
  if (!entries || entries.length === 0) {
    content = h(
      'li',
      { className: 'ui-log__item is-empty' },
      typeof emptyState === 'function'
        ? emptyState()
        : emptyState || 'No activity yet.'
    )
  } else {
    content = entries.map((entry, index) => {
      const key =
        entry && typeof entry === 'object' && entry.id !== undefined
          ? entry.id
          : index
      if (renderEntry) {
        const node = renderEntry(entry, { index, key })
        return h('li', { key, className: 'ui-log__item' }, node)
      }
      const text =
        entry && typeof entry === 'object' && entry.text !== undefined
          ? entry.text
          : entry
      return h('li', { key, className: 'ui-log__item' }, text)
    })
  }

  return h(
    'div',
    {
      ...props,
      ref: containerRef,
      className: cn('ui-log', className),
      role: 'log',
      'aria-live': 'polite',
      'aria-relevant': 'additions text',
      'aria-labelledby': labelledBy
    },
    h('ul', { className: 'ui-log__list' }, content)
  )
}
