import { useState } from "react"
import { Cloud, X } from "lucide-react"

import { Alert, AlertAction, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import type { Light } from "@/lib/api"

const DISMISSED_KEY = "lan-notice-dismissed"

type LanModeNoticeProps = {
  lights: Light[]
}

export function LanModeNotice({ lights }: LanModeNoticeProps) {
  // Offline lights can't be switched to LAN either way, so only nag about reachable cloud ones.
  const cloudLights = lights.filter((light) => light.connection === "cloud" && light.online !== false)
  const key = cloudLights
    .map((light) => light.id)
    .sort()
    .join(",")
  const [dismissedKey, setDismissedKey] = useState(() => localStorage.getItem(DISMISSED_KEY))

  if (cloudLights.length === 0 || dismissedKey === key) return null

  const dismiss = () => {
    localStorage.setItem(DISMISSED_KEY, key)
    setDismissedKey(key)
  }

  const names = cloudLights.map((light) => light.name).join(", ")
  const many = cloudLights.length > 1

  return (
    <Alert className="px-4 py-3">
      <Cloud />
      <AlertTitle>
        {names} {many ? "aren't" : "isn't"} using LAN mode
      </AlertTitle>
      <AlertDescription className="text-pretty">
        <p>
          {many ? "They're" : "It's"} controlled through Govee's cloud, which is slower and needs the internet. In the
          Govee Home app, open {many ? "each light" : "the light"}, tap the settings gear, and turn on{" "}
          <span className="font-medium text-foreground">LAN Control</span>. Then unplug the hub and plug it back in.
        </p>
        <p>Some models don't offer LAN Control. Those keep working through the cloud.</p>
      </AlertDescription>
      <AlertAction>
        <Button variant="ghost" size="icon-sm" onClick={dismiss} aria-label="Dismiss">
          <X />
        </Button>
      </AlertAction>
    </Alert>
  )
}
