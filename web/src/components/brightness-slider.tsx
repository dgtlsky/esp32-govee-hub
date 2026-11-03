import { Sun } from "lucide-react"
import { useEffect, useState } from "react"

import { Slider } from "@/components/ui/slider"

type BrightnessSliderProps = {
  value: number
  disabled?: boolean
  onCommit: (value: number) => void
}

const firstValue = (value: number | readonly number[]) => (typeof value === "number" ? value : value[0])

export function BrightnessSlider({ value, disabled, onCommit }: BrightnessSliderProps) {
  const [draft, setDraft] = useState(value)

  useEffect(() => setDraft(value), [value])

  return (
    <div className="flex items-center gap-3">
      <Sun className="size-4 shrink-0 text-muted-foreground" />
      <Slider
        min={1}
        max={100}
        value={draft}
        disabled={disabled}
        onValueChange={(next) => setDraft(firstValue(next))}
        onValueCommitted={(next) => onCommit(firstValue(next))}
        aria-label="Brightness"
        className="py-2"
      />
      <span className="w-10 shrink-0 text-right text-sm tabular-nums text-muted-foreground">{draft}%</span>
    </div>
  )
}
