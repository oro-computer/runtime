import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleSection,
  ExampleStack,
  ExampleProse,
  Button,
  InlineCode,
  StatusBanner,
  LogViewer,
  StatusPill
} from '../ui/index.js'

import { createMediaController } from './user-media.js'

const {
  createElement: h,
  useState,
  useMemo,
  useCallback,
  useRef,
  useEffect
} = React

function formatError (error) {
  if (!error) return 'Unexpected error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function describeTracks (stream) {
  if (!stream) return 'No active stream'
  const audio = stream.getAudioTracks().filter((track) => track.enabled)
  const video = stream.getVideoTracks().filter((track) => track.enabled)
  return `${video.length} video track(s) · ${audio.length} audio track(s)`
}

function MediaDevicesApp () {
  const [status, setStatus] = useState({
    variant: 'info',
    title: 'Idle',
    message: 'Request camera and microphone access to begin streaming.'
  })
  const [logs, setLogs] = useState([])
  const [stream, setStream] = useState(null)
  const [errorMessage, setErrorMessage] = useState(null)
  const [isStreaming, setIsStreaming] = useState(false)
  const videoRef = useRef(null)
  const controllerRef = useRef(null)
  const shouldStreamRef = useRef(false)

  const appendLog = useCallback((line) => {
    const ts = new Date().toISOString().slice(11, 23)
    setLogs((prev) => {
      const next = [...prev, `[${ts}] ${line}`]
      return next.length > 400 ? next.slice(next.length - 400) : next
    })
  }, [])

  const ensureController = useCallback(() => {
    if (!controllerRef.current) {
      controllerRef.current = createMediaController({
        log: (message) => appendLog(message)
      })
    }
    return controllerRef.current
  }, [appendLog])

  useEffect(() => {
    const controller = ensureController()
    controller.setVideoElement(videoRef.current)
  }, [ensureController])

  const startMedia = useCallback(async () => {
    const controller = ensureController()
    try {
      const ok = await controller.ensurePermissions()
      if (!ok) {
        appendLog('permission denied')
        setStatus({
          variant: 'danger',
          title: 'Permission denied',
          message: 'Grant camera and microphone access to start streaming.'
        })
        setErrorMessage('Permission denied')
        setIsStreaming(false)
        return
      }
      controller.setVideoElement(videoRef.current)
      const nextStream = await controller.start()
      setStream(nextStream)
      setIsStreaming(true)
      setErrorMessage(null)
      shouldStreamRef.current = true
      setStatus({
        variant: 'success',
        title: 'Streaming media',
        message: 'Camera and microphone are active. Close tracks to stop.'
      })
    } catch (error) {
      const message = formatError(error)
      appendLog(`failed to start: ${message}`)
      setStatus({
        variant: 'danger',
        title: 'Failed to start media',
        message
      })
      setErrorMessage(message)
      shouldStreamRef.current = false
      setIsStreaming(false)
    }
  }, [appendLog, ensureController])

  const stopMedia = useCallback(() => {
    const controller = controllerRef.current
    if (controller) controller.stop()
    setStream(null)
    setIsStreaming(false)
    shouldStreamRef.current = false
    setStatus({
      variant: 'info',
      title: 'Media stopped',
      message: 'Tracks have been halted. Start again to resume.'
    })
    appendLog('media stopped manually')
  }, [appendLog])

  useEffect(() => {
    const handlePause = () => {
      const controller = controllerRef.current
      if (controller && controller.getStream()) {
        controller.stop()
        appendLog('runtime paused; media stopped')
        setIsStreaming(false)
      }
    }
    const handleResume = () => {
      if (shouldStreamRef.current) {
        setTimeout(() => {
          startMedia().catch(() => {})
        }, 150)
      }
    }
    const handleStop = () => {
      const controller = controllerRef.current
      if (controller) controller.stop()
      shouldStreamRef.current = false
      setIsStreaming(false)
      setStream(null)
      appendLog('runtime stopped; media released')
    }

    globalThis.addEventListener('applicationpause', handlePause)
    globalThis.addEventListener('applicationresume', handleResume)
    globalThis.addEventListener('applicationstop', handleStop)
    return () => {
      globalThis.removeEventListener('applicationpause', handlePause)
      globalThis.removeEventListener('applicationresume', handleResume)
      globalThis.removeEventListener('applicationstop', handleStop)
    }
  }, [appendLog, startMedia])

  useEffect(
    () => () => {
      const controller = controllerRef.current
      if (controller) controller.dispose()
      shouldStreamRef.current = false
    },
    []
  )

  const statusBanner = useMemo(
    () =>
      h(StatusBanner, {
        variant: status.variant,
        title: status.title,
        message: status.message
      }),
    [status]
  )

  return h(
    ExampleLayout,
    {
      title: 'Media Devices',
      description:
        'Capture camera and microphone streams with lifecycle-aware clean-up.'
    },
    h(
      ExamplePanel,
      {
        title: 'Media controller',
        description: 'Start the stream to attach camera output to the preview.'
      },
      h(
        ExampleStack,
        { gap: 'lg' },
        statusBanner,
        errorMessage &&
          h(StatusBanner, {
            variant: 'danger',
            title: 'Latest error',
            message: errorMessage,
            assertive: true
          }),
        h(
          ExampleSection,
          {
            title: 'Controls'
          },
          h(
            ExampleStack,
            { gap: 'sm' },
            h(
              Button,
              {
                type: 'button',
                onClick: () => startMedia().catch(() => {}),
                disabled: isStreaming
              },
              isStreaming ? 'Streaming…' : 'Start media'
            ),
            h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                onClick: () => stopMedia(),
                disabled: !isStreaming
              },
              'Stop media'
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Preview',
            description: 'Live video preview from getUserMedia.'
          },
          h(
            'div',
            { className: 'ui-prose' },
            h(
              StatusPill,
              { status: isStreaming ? 'running' : 'idle' },
              isStreaming ? 'streaming' : 'idle'
            ),
            h('p', null, describeTracks(stream))
          ),
          h('video', {
            ref: videoRef,
            style: {
              width: '100%',
              maxWidth: '420px',
              borderRadius: '18px',
              border: '1px solid rgba(148, 163, 184, 0.25)',
              boxShadow: '0 24px 60px rgba(15, 23, 42, 0.35)'
            },
            autoPlay: true,
            muted: true,
            playsInline: true
          })
        ),
        h(
          ExampleSection,
          {
            title: 'How this works'
          },
          h(
            ExampleProse,
            null,
            h(
              'ul',
              null,
              h(
                'li',
                null,
                'Checks permissions via ',
                h(InlineCode, null, 'navigator.permissions'),
                ' before requesting media.'
              ),
              h(
                'li',
                null,
                'Uses ',
                h(InlineCode, null, 'navigator.mediaDevices.getUserMedia'),
                ' to capture audio and video streams.'
              ),
              h(
                'li',
                null,
                'Stops tracks to release devices when the app pauses or you press stop.'
              )
            )
          )
        ),
        h(
          ExampleSection,
          {
            title: 'Activity log',
            description: 'Media events and lifecycle logs.',
            actions: h(
              Button,
              {
                type: 'button',
                variant: 'ghost',
                size: 'sm',
                onClick: () => setLogs([])
              },
              'Clear log'
            )
          },
          h(LogViewer, {
            entries: logs,
            emptyState: 'Start the media stream to populate logs.'
          })
        )
      )
    )
  )
}

mountExample(MediaDevicesApp)
