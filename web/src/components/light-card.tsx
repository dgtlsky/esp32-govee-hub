import { Cloud, Lightbulb, Wifi } from "lucide-react"

import { BrightnessSlider } from "@/components/brightness-slider"
import { ColorSwatches } from "@/components/color-swatches"
import { Badge } from "@/components/ui/badge"
import { Card, CardAction, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Switch } from "@/components/ui/switch"
import { rgbToHex, type ControlCommand, type Light } from "@/lib/api"

type LightCardProps = {
  light: Light
  onControl: (command: ControlCommand) => void
}

export function LightCard({ light, onControl }: LightCardProps) {
  const send = (command: Omit<ControlCommand, "ids">) => onControl({ ...command, ids: [light.id] })
  const isOn = light.stateKnown && light.on
  const glow = rgbToHex(light.color)

  return (
    <Card className="transition-opacity">
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <span
            className="flex size-8 shrink-0 items-center justify-center rounded-full border border-border transition-colors"
            style={isOn ? { backgroundColor: glow, boxShadow: `0 0 16px ${glow}` } : undefined}
          >
            <Lightbulb className={isOn ? "size-4 text-black/70" : "size-4 text-muted-foreground"} />
          </span>
          <span className="truncate">{light.name}</span>
        </CardTitle>
        <CardDescription className="flex flex-wrap items-center gap-2">
          <span>{light.sku}</span>
          {light.connection === "lan" ? (
            <Badge variant="secondary">
              <Wifi /> Local
            </Badge>
          ) : (
            <Badge variant="outline">
              <Cloud /> Cloud
            </Badge>
          )}
          {!light.stateKnown && <span>State unknown</span>}
        </CardDescription>
        <CardAction>
          <Switch
            checked={isOn}
            onCheckedChange={(checked) => send({ power: checked })}
            aria-label={`Turn ${light.name} ${isOn ? "off" : "on"}`}
          />
        </CardAction>
      </CardHeader>
      <CardContent className="flex flex-col gap-4">
        <BrightnessSlider
          value={light.brightness}
          disabled={!isOn}
          onCommit={(brightness) => send({ brightness })}
        />
        <ColorSwatches value={light.color} disabled={!isOn} onChange={(color) => send({ color })} />
      </CardContent>
    </Card>
  )
}
