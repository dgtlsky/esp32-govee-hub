import { Power, PowerOff } from "lucide-react"

import { BrightnessSlider } from "@/components/brightness-slider"
import { ColorSwatches } from "@/components/color-swatches"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import type { ControlCommand, Light } from "@/lib/api"

type AllLightsCardProps = {
  lights: Light[]
  onControl: (command: ControlCommand) => void
}

export function AllLightsCard({ lights, onControl }: AllLightsCardProps) {
  const onCount = lights.filter((l) => l.stateKnown && l.on).length
  const averageBrightness = lights.length
    ? Math.round(lights.reduce((sum, l) => sum + l.brightness, 0) / lights.length)
    : 100

  return (
    <Card>
      <CardHeader>
        <CardTitle>All lights</CardTitle>
        <CardDescription>
          {onCount} of {lights.length} on
        </CardDescription>
      </CardHeader>
      <CardContent className="flex flex-col gap-4">
        <div className="grid grid-cols-2 gap-2">
          <Button size="lg" onClick={() => onControl({ power: true })}>
            <Power /> All on
          </Button>
          <Button size="lg" variant="outline" onClick={() => onControl({ power: false })}>
            <PowerOff /> All off
          </Button>
        </div>
        <BrightnessSlider value={averageBrightness} onCommit={(brightness) => onControl({ brightness })} />
        <ColorSwatches onChange={(color) => onControl({ power: true, color })} />
      </CardContent>
    </Card>
  )
}
