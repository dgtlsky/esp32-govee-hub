import { Lightbulb, RefreshCw, WifiOff } from "lucide-react"

import { AllLightsCard } from "@/components/all-lights-card"
import { LightCard } from "@/components/light-card"
import { Button } from "@/components/ui/button"
import { Skeleton } from "@/components/ui/skeleton"
import { useLights } from "@/hooks/use-lights"

export function Dashboard() {
  const { lights, loading, connected, refresh, control } = useLights()

  return (
    <div className="mx-auto flex min-h-svh w-full max-w-6xl flex-col gap-6 px-4 py-6 sm:px-6 sm:py-10">
      <header className="flex items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="flex size-10 items-center justify-center rounded-lg bg-primary text-primary-foreground">
            <Lightbulb className="size-5" />
          </div>
          <div>
            <h1 className="font-heading text-xl font-semibold tracking-tight sm:text-2xl">Govee Lights</h1>
            <p className="text-sm text-muted-foreground">ESP32 controller</p>
          </div>
        </div>
        <Button variant="outline" size="icon-lg" onClick={refresh} aria-label="Refresh">
          <RefreshCw />
        </Button>
      </header>

      {!connected && (
        <div className="flex items-center gap-3 rounded-lg border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          <WifiOff className="size-4 shrink-0" />
          Can't reach the ESP32. Make sure it's powered on and on your WiFi.
        </div>
      )}

      {loading ? (
        <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
          {Array.from({ length: 6 }, (_, i) => (
            <Skeleton key={i} className="h-48 rounded-xl" />
          ))}
        </div>
      ) : (
        <>
          <AllLightsCard lights={lights} onControl={control} />
          <section className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
            {lights.map((light) => (
              <LightCard key={light.id} light={light} onControl={control} />
            ))}
          </section>
        </>
      )}
    </div>
  )
}
