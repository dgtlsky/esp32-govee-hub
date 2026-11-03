import { useCallback, useEffect, useRef, useState } from "react"
import { toast } from "sonner"

import { fetchLights, sendControl, type ControlCommand, type Light } from "@/lib/api"

const POLL_INTERVAL_MS = 5000

export function useLights() {
  const [lights, setLights] = useState<Light[]>([])
  const [loading, setLoading] = useState(true)
  const [connected, setConnected] = useState(true)
  const pendingCommands = useRef(0)

  const refresh = useCallback(async () => {
    if (pendingCommands.current > 0) return
    try {
      const latest = await fetchLights()
      if (pendingCommands.current === 0) setLights(latest)
      setConnected(true)
    } catch {
      setConnected(false)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    refresh()
    const timer = setInterval(() => {
      if (document.visibilityState === "visible") refresh()
    }, POLL_INTERVAL_MS)
    return () => clearInterval(timer)
  }, [refresh])

  const control = useCallback(async (command: ControlCommand) => {
    const targets = new Set(command.ids)
    const applies = (light: Light) => !command.ids || targets.has(light.id)

    setLights((current) =>
      current.map((light) =>
        applies(light)
          ? {
              ...light,
              stateKnown: light.stateKnown || command.power !== undefined,
              on: command.power ?? light.on,
              brightness: command.brightness ?? light.brightness,
              color: command.color ?? light.color,
            }
          : light,
      ),
    )

    pendingCommands.current++
    try {
      const result = await sendControl(command)
      setLights(result.lights)
      setConnected(true)
      if (result.failed.length > 0) {
        const names = result.lights.filter((l) => result.failed.includes(l.id)).map((l) => l.name)
        toast.error(`Couldn't update ${names.join(", ")}`)
      }
    } catch {
      setConnected(false)
      toast.error("Couldn't reach the ESP32 controller")
    } finally {
      pendingCommands.current--
    }
  }, [])

  return { lights, loading, connected, refresh, control }
}
