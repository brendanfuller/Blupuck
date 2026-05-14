<script setup lang="ts">
import { ref } from 'vue'

const log = ref<string[]>([])
const device = ref<USBDevice | null>(null)
const supported = !!(navigator as any).usb

function append(line: string) {
  log.value.push(line)
}

async function connect() {
  try {
    const d = await navigator.usb.requestDevice({
      filters: [{ vendorId: 0x045e, productId: 0x0719 }],
    })
    await d.open()
    if (d.configuration === null) await d.selectConfiguration(1)
    await d.claimInterface(7)
    device.value = d
    append(`opened ${d.productName} / ${d.serialNumber}`)
    append(`interfaces: ${d.configuration!.interfaces.map(i => i.interfaceNumber).join(', ')}`)
  } catch (e) {
    append(`error: ${(e as Error).message}`)
  }
}

async function sendEcho() {
  if (!device.value) return
  const text = `ping ${Date.now()}`
  const out = new TextEncoder().encode(text)
  try {
    const tx = await device.value.transferOut(7, out)
    append(`tx ${tx.bytesWritten} bytes: ${text}`)
    const rx = await device.value.transferIn(8, 64)
    const got = new TextDecoder().decode(rx.data!)
    append(`rx ${rx.data!.byteLength} bytes: ${got}`)
  } catch (e) {
    append(`error: ${(e as Error).message}`)
  }
}

async function disconnect() {
  if (!device.value) return
  try {
    await device.value.close()
    append('closed')
  } catch (e) {
    append(`error: ${(e as Error).message}`)
  }
  device.value = null
}
</script>

<template>
  <main>
    <h1>Blupuck Config</h1>
    <p v-if="!supported" class="warn">WebUSB not available — use Chrome/Edge over HTTPS or localhost.</p>
    <div class="row">
      <button @click="connect" :disabled="!supported || !!device">Connect</button>
      <button @click="sendEcho" :disabled="!device">Send echo</button>
      <button @click="disconnect" :disabled="!device">Disconnect</button>
    </div>
    <pre>{{ log.join('\n') }}</pre>
  </main>
</template>

<style scoped>
main { max-width: 720px; margin: 2rem auto; font: 14px/1.4 system-ui, sans-serif; padding: 0 1rem; }
h1 { font-size: 1.4rem; }
.row { display: flex; gap: 0.5rem; margin: 1rem 0; }
button { padding: 0.5rem 1rem; }
pre { background: #111; color: #eee; padding: 0.75rem; border-radius: 4px; min-height: 8rem; white-space: pre-wrap; }
.warn { color: #c00; }
</style>
