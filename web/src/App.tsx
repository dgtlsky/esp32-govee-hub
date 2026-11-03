import { Toaster } from "@/components/ui/sonner"
import { Dashboard } from "@/pages/dashboard"

export default function App() {
  return (
    <>
      <Dashboard />
      <Toaster theme="dark" position="top-center" />
    </>
  )
}
