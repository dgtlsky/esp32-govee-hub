import { Sun } from "lucide-react"
import { useState } from "react"

import { Slider } from "@/components/ui/slider"

type BrightnessSliderProps = {
  value: number
  disabled?: boolean
  onCommit: (value: number) => void
}

const firstValue = (value: number | readonly number[]) => (typeof value === "number" ? value : value[0])

export function BrightnessSlider({ value, disabled, onCommit }: BrightnessSliderProps) {
  // Only set while dragging, so the slider follows `value` the rest of the time.
  const [draft, setDraft] = useState<number | null>(null)
  const shown = draft ?? value

  return (
    <div className="flex items-center gap-3">
      <Sun className="size-4 shrink-0 text-muted-foreground" />
      <Slider
        min={1}
        max={100}
        value={shown}
        disabled={disabled}
        onValueChange={(next) => setDraft(firstValue(next))}
        onValueCommitted={(next) => {
          setDraft(null)
          onCommit(firstValue(next))
        }}
        aria-label="Brightness"
        className="py-2"
      />
      <span className="w-10 shrink-0 text-right text-sm tabular-nums text-muted-foreground">{shown}%</span>
    </div>
  )
}
