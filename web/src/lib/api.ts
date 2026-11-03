export type Rgb = { r: number; g: number; b: number }

export type Light = {
  id: string
  name: string
  sku: string
  connection: "lan" | "cloud"
  ip?: string
  online?: boolean
  stateKnown: boolean
  on: boolean
  brightness: number
  color: Rgb
  colorTempK?: number
}

export type ControlCommand = {
  ids?: string[]
  power?: boolean
  brightness?: number
  color?: Rgb
}

type ControlResponse = { lights: Light[]; failed: string[]; error?: string }

export async function fetchLights(): Promise<Light[]> {
  const res = await fetch("/api/lights")
  if (!res.ok) throw new Error(`Controller returned ${res.status}`)
  const body: { lights: Light[] } = await res.json()
  return body.lights
}

export async function sendControl(command: ControlCommand): Promise<ControlResponse> {
  const res = await fetch("/api/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(command),
  })
  if (!res.ok) throw new Error(`Controller returned ${res.status}`)
  return res.json()
}

export function rgbToHex({ r, g, b }: Rgb): string {
  return `#${[r, g, b].map((v) => v.toString(16).padStart(2, "0")).join("")}`
}

// Approximate on-screen color of white light at a given temperature (Tanner Helland's curve fit).
export function kelvinToRgb(kelvin: number): Rgb {
  const t = kelvin / 100
  const clamp = (v: number) => Math.round(Math.min(255, Math.max(0, v)))
  const r = t <= 66 ? 255 : 329.698727446 * Math.pow(t - 60, -0.1332047592)
  const g = t <= 66 ? 99.4708025861 * Math.log(t) - 161.1195681661 : 288.1221695283 * Math.pow(t - 60, -0.0755148492)
  const b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307
  return { r: clamp(r), g: clamp(g), b: clamp(b) }
}

export function displayColor(light: Light): Rgb {
  return light.colorTempK ? kelvinToRgb(light.colorTempK) : light.color
}

export function hexToRgb(hex: string): Rgb {
  const value = parseInt(hex.slice(1), 16)
  return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 }
}
