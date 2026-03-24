const Fragment = Symbol('Fragment')
const FORWARD_REF = Symbol('ForwardRef')
const CONTEXT_PROVIDER = Symbol('ContextProvider')

const enqueueMicrotask =
  typeof queueMicrotask === 'function'
    ? queueMicrotask
    : (fn) => Promise.resolve().then(fn)

let currentRoot = null
let currentInstance = null

function toChildArray (value) {
  const result = []
  const push = (child) => {
    if (Array.isArray(child)) {
      for (const nested of child) push(nested)
    } else if (
      child !== null &&
      child !== undefined &&
      child !== false &&
      child !== true
    ) {
      result.push(child)
    }
  }
  push(value)
  return result
}

function areDepsEqual (prev, next) {
  if (
    prev === undefined ||
    next === undefined ||
    prev === null ||
    next === null
  ) {
    return false
  }
  if (prev.length !== next.length) return false
  for (let i = 0; i < prev.length; i += 1) {
    if (!Object.is(prev[i], next[i])) return false
  }
  return true
}

function assignRef (ref, value) {
  if (!ref) return
  if (typeof ref === 'function') {
    ref(value)
  } else if (typeof ref === 'object') {
    ref.current = value
  }
}

function appendChildren (parent, children) {
  const array = toChildArray(children)
  for (const child of array) {
    const node = renderVNode(child)
    if (node) parent.appendChild(node)
  }
}

function setStyle (element, value) {
  if (!value) {
    element.removeAttribute('style')
    return
  }
  if (typeof value === 'string') {
    element.setAttribute('style', value)
    return
  }
  for (const key in value) {
    if (Object.prototype.hasOwnProperty.call(value, key)) {
      element.style[key] = value[key]
    }
  }
}

function setProperty (element, key, value) {
  if (key === 'className') {
    element.className = value || ''
    return
  }
  if (key === 'htmlFor') {
    element.setAttribute('for', value)
    return
  }
  if (key === 'style') {
    setStyle(element, value)
    return
  }
  if (key.startsWith('on') && typeof value === 'function') {
    const eventName = key.slice(2).toLowerCase()
    element.addEventListener(eventName, value)
    return
  }
  if (value === null || value === undefined || value === false) {
    element.removeAttribute(key)
    if (key in element && typeof value !== 'string') {
      try {
        element[key] = typeof value === 'boolean' ? false : ''
      } catch {}
    }
    return
  }
  if (key in element) {
    try {
      element[key] = value
      if (typeof value !== 'object' && typeof value !== 'function') return
    } catch {}
  }
  element.setAttribute(key, value === true ? '' : String(value))
}

function renderFragment (children) {
  const fragment = document.createDocumentFragment()
  appendChildren(fragment, children)
  return fragment
}

function renderContextProvider (context, props) {
  const value = Object.prototype.hasOwnProperty.call(props, 'value')
    ? props.value
    : context.defaultValue
  context.stack.push(value)
  const node = renderFragment(props.children)
  context.stack.pop()
  return node
}

function beginComponent (type) {
  const root = currentRoot
  const cursor = root.componentCursor++
  const prevInstances = root.prevInstances || []
  let instance = prevInstances[cursor]
  if (instance && instance.type === type) {
    instance.used = true
  } else {
    if (instance) cleanupInstance(instance)
    instance = {
      type,
      hooks: [],
      used: true
    }
  }
  instance.root = root
  instance.hookCursor = 0
  root.instances[cursor] = instance
  return instance
}

function renderFunctionComponent (component, props, ref, withRef) {
  const instance = beginComponent(component)
  const previous = currentInstance
  currentInstance = instance
  const output = withRef ? component(props, ref) : component(props)
  const node = renderVNode(output)
  currentInstance = previous
  return node
}

function renderDomElement (type, props, ref) {
  const element = document.createElement(type)
  let innerHTML = null
  if (props) {
    for (const key in props) {
      if (!Object.prototype.hasOwnProperty.call(props, key)) continue
      if (key === 'children' || key === 'ref') continue
      if (key === 'dangerouslySetInnerHTML') {
        const html = props[key]?.__html
        if (typeof html === 'string') innerHTML = html
        continue
      }
      setProperty(element, key, props[key])
    }
    if (innerHTML !== null) {
      element.innerHTML = innerHTML
    } else if (props.children !== undefined) {
      appendChildren(element, props.children)
    }
  }
  assignRef(ref, element)
  return element
}

function renderVNode (vnode) {
  if (vnode === null || vnode === undefined || typeof vnode === 'boolean') {
    return null
  }
  if (typeof vnode === 'string' || typeof vnode === 'number') {
    return document.createTextNode(String(vnode))
  }
  if (Array.isArray(vnode)) {
    return renderFragment(vnode)
  }
  if (vnode?.type === Fragment) {
    return renderFragment(vnode.props?.children)
  }
  if (typeof vnode?.type === 'object' && vnode.type !== null) {
    const type = vnode.type
    if (type.$$typeof === CONTEXT_PROVIDER) {
      return renderContextProvider(type.context, vnode.props || {})
    }
    if (type.$$typeof === FORWARD_REF) {
      return renderFunctionComponent(
        type.render,
        vnode.props || {},
        vnode.ref,
        true
      )
    }
  }
  if (typeof vnode?.type === 'function') {
    return renderFunctionComponent(
      vnode.type,
      vnode.props || {},
      vnode.ref,
      false
    )
  }
  if (typeof vnode?.type === 'string') {
    return renderDomElement(vnode.type, vnode.props || {}, vnode.ref)
  }
  return null
}

function cleanupInstance (instance) {
  if (!instance || !instance.hooks) return
  for (const hook of instance.hooks) {
    if (hook && hook.tag === 'effect' && typeof hook.cleanup === 'function') {
      try {
        hook.cleanup()
      } catch (err) {
        console.error(err)
      }
      hook.cleanup = undefined
    }
  }
}

function scheduleRender (root) {
  if (!root) return
  if (root.scheduled) return
  root.scheduled = true
  enqueueMicrotask(() => {
    root.scheduled = false
    root.update()
  })
}

function createElement (type, props, ...childrenArgs) {
  const nextProps = props ? { ...props } : {}
  let ref = null
  if (Object.prototype.hasOwnProperty.call(nextProps, 'ref')) {
    ref = nextProps.ref
    delete nextProps.ref
  }
  if (childrenArgs.length === 1) {
    nextProps.children = childrenArgs[0]
  } else if (childrenArgs.length > 1) {
    nextProps.children = childrenArgs
  }
  return { type, props: nextProps, ref }
}

function useState (initialValue) {
  if (!currentInstance) {
    throw new Error('useState can only be used inside a component')
  }
  const instance = currentInstance
  const cursor = instance.hookCursor++
  let hook = instance.hooks[cursor]
  if (!hook || hook.tag !== 'state') {
    const value =
      typeof initialValue === 'function' ? initialValue() : initialValue
    hook = {
      tag: 'state',
      value,
      setState: (next) => {
        const resolved = typeof next === 'function' ? next(hook.value) : next
        if (!Object.is(resolved, hook.value)) {
          hook.value = resolved
          scheduleRender(instance.root)
        }
      }
    }
    instance.hooks[cursor] = hook
  }
  hook.root = instance.root
  return [hook.value, hook.setState]
}

function useRef (initialValue = null) {
  if (!currentInstance) {
    throw new Error('useRef can only be used inside a component')
  }
  const instance = currentInstance
  const cursor = instance.hookCursor++
  let hook = instance.hooks[cursor]
  if (!hook || hook.tag !== 'ref') {
    hook = { tag: 'ref', value: { current: initialValue } }
    instance.hooks[cursor] = hook
  }
  return hook.value
}

function useMemo (factory, deps) {
  if (!currentInstance) {
    throw new Error('useMemo can only be used inside a component')
  }
  const instance = currentInstance
  const cursor = instance.hookCursor++
  let hook = instance.hooks[cursor]
  const shouldRecompute =
    deps === undefined ||
    !hook ||
    hook.tag !== 'memo' ||
    !areDepsEqual(hook.deps, deps)
  if (!hook || hook.tag !== 'memo') {
    hook = { tag: 'memo', value: undefined, deps: undefined }
    instance.hooks[cursor] = hook
  }
  if (shouldRecompute) {
    hook.value = factory()
    hook.deps = deps === undefined ? undefined : [...deps]
  }
  return hook.value
}

function useCallback (callback, deps) {
  return useMemo(() => callback, deps)
}

function useEffect (effect, deps) {
  if (!currentInstance) {
    throw new Error('useEffect can only be used inside a component')
  }
  const instance = currentInstance
  const cursor = instance.hookCursor++
  let hook = instance.hooks[cursor]
  const shouldRun =
    deps === undefined ||
    !hook ||
    hook.tag !== 'effect' ||
    !areDepsEqual(hook.deps, deps)
  if (!hook || hook.tag !== 'effect') {
    hook = { tag: 'effect', cleanup: undefined, deps: undefined }
    instance.hooks[cursor] = hook
  }
  if (shouldRun) {
    const root = instance.root
    root.pendingEffects.push(() => {
      if (typeof hook.cleanup === 'function') {
        try {
          hook.cleanup()
        } catch (err) {
          console.error(err)
        }
      }
      const cleanup = effect()
      hook.cleanup = typeof cleanup === 'function' ? cleanup : undefined
      hook.deps = deps === undefined ? undefined : [...deps]
    })
  }
}

const useLayoutEffect = useEffect

function createContext (defaultValue) {
  const context = {
    defaultValue,
    stack: []
  }
  context.Provider = { $$typeof: CONTEXT_PROVIDER, context }
  return context
}

function useContext (context) {
  if (context.stack.length > 0) {
    return context.stack[context.stack.length - 1]
  }
  return context.defaultValue
}

function forwardRef (renderFn) {
  return { $$typeof: FORWARD_REF, render: renderFn }
}

function commit (container, node) {
  while (container.firstChild) container.removeChild(container.firstChild)
  if (!node) return
  container.appendChild(node)
}

function createRoot (container) {
  if (!container) throw new Error('createRoot: container is required')
  const root = {
    container,
    element: null,
    instances: [],
    prevInstances: [],
    componentCursor: 0,
    pendingEffects: [],
    scheduled: false,
    update
  }

  function update () {
    const prevInstances = root.instances
    root.prevInstances = prevInstances
    if (prevInstances) {
      for (const instance of prevInstances) {
        if (instance) instance.used = false
      }
    }

    root.instances = []
    root.componentCursor = 0
    root.pendingEffects = []

    const previousRoot = currentRoot
    currentRoot = root
    const node = renderVNode(root.element)
    currentRoot = previousRoot

    commit(container, node)

    if (Array.isArray(root.prevInstances)) {
      for (const instance of root.prevInstances) {
        if (instance && !instance.used) cleanupInstance(instance)
      }
    }

    if (root.pendingEffects.length > 0) {
      const effects = root.pendingEffects.slice()
      root.pendingEffects.length = 0
      enqueueMicrotask(() => {
        for (const fn of effects) {
          try {
            fn()
          } catch (err) {
            console.error(err)
          }
        }
      })
    }
  }

  return {
    render (element) {
      root.element = element
      root.update()
    }
  }
}

const React = {
  createElement,
  Fragment,
  useState,
  useEffect,
  useLayoutEffect,
  useMemo,
  useCallback,
  useRef,
  createContext,
  useContext,
  forwardRef
}

export {
  createElement,
  Fragment,
  useState,
  useEffect,
  useLayoutEffect,
  useMemo,
  useCallback,
  useRef,
  createContext,
  useContext,
  forwardRef,
  createRoot
}

export default React
