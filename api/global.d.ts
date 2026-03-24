type MenuItemSelection = {
  title: string
  parent: string
  state: '0'
}

/**
 * Preferred runtime detection flag for Oro Runtime.
 * Always true under Oro and intended for feature detection.
 */
declare const isOroRuntime: boolean

declare interface Window {
  addEventListener(
    type: 'menuItemSelected',
    listener: (event: CustomEvent<MenuItemSelection>) => void,
    options?: boolean | AddEventListenerOptions
  ): void
  addEventListener(
    type: 'process-error',
    listener: (event: CustomEvent<string>) => void,
    options?: boolean | AddEventListenerOptions
  ): void
  addEventListener(
    type: 'backend-exit',
    listener: (event: CustomEvent<string>) => void,
    options?: boolean | AddEventListenerOptions
  ): void
}
