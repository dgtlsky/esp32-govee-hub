import { Palette } from "lucide-react"

import { hexToRgb, rgbToHex, type Rgb } from "@/lib/api"
import { cn } from "@/lib/utils"

const PRESETS: { name: string; color: Rgb }[] = [
  { name: "Warm white", color: { r: 255, g: 180, b: 107 } },
  { name: "White", color: { r: 255, g: 255, b: 255 } },
  { name: "Red", color: { r: 255, g: 0, b: 0 } },
  { name: "Orange", color: { r: 255, g: 110, b: 0 } },
  { name: "Yellow", color: { r: 255, g: 220, b: 0 } },
  { name: "Green", color: { r: 0, g: 255, b: 60 } },
  { name: "Cyan", color: { r: 0, g: 255, b: 255 } },
  { name: "Blue", color: { r: 0, g: 60, b: 255 } },
  { name: "Purple", color: { r: 150, g: 0, b: 255 } },
  { name: "Pink", color: { r: 255, g: 0, b: 160 } },
]

type ColorSwatchesProps = {
  value?: Rgb
  disabled?: boolean
  onChange: (color: Rgb) => void
}

export function ColorSwatches({ value, disabled, onChange }: ColorSwatchesProps) {
  const selectedHex = value ? rgbToHex(value) : undefined

  return (
    <div className="flex flex-wrap gap-2">
      {PRESETS.map((preset) => {
        const hex = rgbToHex(preset.color)
        return (
          <button
            key={preset.name}
            type="button"
            title={preset.name}
            aria-label={preset.name}
            disabled={disabled}
            onClick={() => onChange(preset.color)}
            className={cn(
              "size-8 rounded-full border border-border shadow-xs transition-transform hover:scale-110 focus-visible:ring-3 focus-visible:ring-ring/50 focus-visible:outline-hidden disabled:pointer-events-none disabled:opacity-40",
              selectedHex === hex && "ring-2 ring-ring ring-offset-2 ring-offset-card",
            )}
            style={{ backgroundColor: hex }}
          />
        )
      })}
      <label
        title="Custom color"
        className={cn(
          "relative flex size-8 cursor-pointer items-center justify-center rounded-full border border-border bg-muted text-muted-foreground shadow-xs transition-transform hover:scale-110 focus-within:ring-3 focus-within:ring-ring/50",
          disabled && "pointer-events-none opacity-40",
        )}
      >
        <Palette className="size-4" />
        <input
          type="color"
          aria-label="Custom color"
          disabled={disabled}
          value={selectedHex ?? "#ffffff"}
          onChange={(e) => onChange(hexToRgb(e.target.value))}
          className="absolute inset-0 cursor-pointer opacity-0"
        />
      </label>
    </div>
  )
}
