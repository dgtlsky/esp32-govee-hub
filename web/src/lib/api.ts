export type Rgb = { r: number; g: number; b: number }

export type Light = {
  id: string
  name: string
  sku: string
  connection: "lan" | "cloud"
  ip?: string
  stateKnown: boolean
  on: boolean
  brightness: number
  color: Rgb
}

export type ControlCommand = {
  ids?: string[]
  power?: boolean
  brightness?: number
  color?: Rgb
}

type ControlResponse = { lights: Light[]; failed: string[] }

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

export function hexToRgb(hex: string): Rgb {
  const value = parseInt(hex.slice(1), 16)
  return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 }
}
