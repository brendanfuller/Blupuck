<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import {
  ArrowDown,
  ArrowUp,
  Battery,
  BatteryFull,
  BatteryLow,
  BatteryMedium,
  Copy,
  Lightbulb,
  MousePointer2,
  Plug,
  PlugZap,
  RefreshCw,
  Search,
  SearchX,
  Trash2,
} from 'lucide-vue-next'

import {
  FrameDecoder,
  decodeBondList,
  decodeController,
  decodeHello,
  decodeScanning,
  encodeForgetAll,
  encodeGetStatus,
  encodeIdentify,
  encodeSetForward,
  encodeSetScanning,
  encodeSetSlot,
} from './protocol/messages'
import { MessageType, macToString } from './protocol/types'
import type { BondEntry, ControllerPayload, HelloPayload } from './protocol/types'

const SLOT_PREF_KEY = 'blupuck-slot-prefs-v1'
const FORWARD_KEY = 'blupuck-forwards-v1'

const supported = !!(navigator as any).usb

const device = ref<USBDevice | null>(null)
const hello = ref<HelloPayload | null>(null)
const scanning = ref<boolean | null>(null)
const slots = reactive<Record<number, ControllerPayload>>({})
const bonds = ref<BondEntry[]>([])
const log = ref<string[]>([])

// MAC → XInput position (0..3). Mirrors the bridge's slot_prefs table,
// pushed on Connect so a power-cycled bridge re-applies them.
const slotPref = reactive<Record<string, number>>(loadSlotPrefs())

// MAC → MAC mirror selections, also pushed on Connect.
const forwardPref = reactive<Record<string, string>>(loadForwards())

const decoder = new FrameDecoder()
let readLoopActive = false

// All four positions, always rendered. Empty positions show a placeholder.
const xiSlots = computed(() => [0, 1, 2, 3].map(xi => slots[xi]))

function loadSlotPrefs(): Record<string, number> {
  try { return JSON.parse(localStorage.getItem(SLOT_PREF_KEY) || '{}') } catch { return {} }
}

function loadForwards(): Record<string, string> {
  try { return JSON.parse(localStorage.getItem(FORWARD_KEY) || '{}') } catch { return {} }
}

watch(slotPref, v => localStorage.setItem(SLOT_PREF_KEY, JSON.stringify(v)), { deep: true })
watch(forwardPref, v => localStorage.setItem(FORWARD_KEY, JSON.stringify(v)), { deep: true })

function append(line: string) {
  log.value.unshift(`${new Date().toLocaleTimeString()}  ${line}`)
  log.value = log.value.slice(0, 100)
}

function batteryPercent(raw: number): number | null {
  if (raw === 0) return null
  return Math.round(((raw - 1) / 254) * 100)
}

function batteryIcon(raw: number) {
  const p = batteryPercent(raw)
  if (p === null) return Battery
  if (p >= 67) return BatteryFull
  if (p >= 34) return BatteryMedium
  if (p >= 10) return BatteryLow
  return Battery
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ')
}

function macFromString(s: string): Uint8Array {
  const parts = s.split(':')
  const out = new Uint8Array(6)
  for (let i = 0; i < 6; i++) out[i] = parseInt(parts[i] || '0', 16)
  return out
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
    append(`opened ${d.productName}`)
    startReadLoop()
    await send(encodeGetStatus())
    // Push stored slot ordering and mirror rules so the bridge picks them up
    // after a power cycle. Slot prefs first so LEDs settle, then mirrors.
    for (const [mac, pos] of Object.entries(slotPref)) {
      await send(encodeSetSlot(macFromString(mac), pos))
    }
    for (const [src, dst] of Object.entries(forwardPref)) {
      if (!dst) continue
      await send(encodeSetForward(macFromString(src), macFromString(dst)))
    }
  } catch (e) {
    append(`error: ${(e as Error).message}`)
  }
}

async function disconnect() {
  if (!device.value) return
  readLoopActive = false
  try { await device.value.close() } catch { /* ignore */ }
  device.value = null
  hello.value = null
  scanning.value = null
  for (const k of Object.keys(slots)) delete slots[Number(k)]
  bonds.value = []
}

async function send(bytes: Uint8Array) {
  if (!device.value) return
  try {
    await device.value.transferOut(7, bytes)
  } catch (e) {
    append(`tx error: ${(e as Error).message}`)
  }
}

async function startReadLoop() {
  if (readLoopActive) return
  readLoopActive = true
  while (readLoopActive && device.value) {
    try {
      const r = await device.value.transferIn(8, 256)
      if (!r.data) continue
      const bytes = new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength)
      for (const frame of decoder.feed(bytes)) handleFrame(frame.type, frame.payload)
    } catch (e) {
      append(`rx error: ${(e as Error).message}`)
      readLoopActive = false
    }
  }
}

function handleFrame(type: MessageType, payload: Uint8Array) {
  const head = `0x${type.toString(16).padStart(2, '0')} len=${payload.length}`
  const tail = payload.length ? ` ${hex(payload)}` : ''
  append(`◀ ${head}${tail}`)

  switch (type) {
    case MessageType.Hello:
      hello.value = decodeHello(payload)
      append(`   hello v${hello.value.protocolVersion} fw=${hello.value.firmwareMajor}.${hello.value.firmwareMinor}`)
      break
    case MessageType.Controller: {
      const c = decodeController(payload)
      slots[c.slot] = c
      const bp = batteryPercent(c.battery)
      append(`   slot ${c.slot + 1} ${c.present ? `present "${c.name}" mac=${macToString(c.mac)} batt=${bp === null ? '—' : bp + '%'}` : 'empty'}`)
      break
    }
    case MessageType.Scanning:
      scanning.value = decodeScanning(payload).enabled
      append(`   scanning ${scanning.value ? 'on' : 'off'}`)
      break
    case MessageType.BondList:
      bonds.value = decodeBondList(payload).entries
      append(`   bond list: ${bonds.value.length} entries`)
      break
    default:
      append('   unknown type')
  }
}

async function toggleScanning() {
  if (scanning.value === null) return
  await send(encodeSetScanning(!scanning.value))
}

async function forgetAll() {
  if (!confirm('Forget all paired controllers?')) return
  await send(encodeForgetAll())
}

async function refresh() {
  await send(encodeGetStatus())
}

async function identify(c: ControllerPayload) {
  await send(encodeIdentify(c.slot))
}

async function setForward(srcMac: string, targetMac: string) {
  forwardPref[srcMac] = targetMac
  const src = macFromString(srcMac)
  const dst = targetMac ? macFromString(targetMac) : new Uint8Array(6)
  await send(encodeSetForward(src, dst))
}

// --- reorder via up/down buttons -----------------------------------------
async function moveSlot(xi: number, delta: number) {
  const c = slots[xi]
  if (!c?.present) return
  const target = xi + delta
  if (target < 0 || target > 3) return

  // Move this MAC to `target`. The bridge swaps it with whatever was at the
  // target XI position; mirror that swap in our local prefs.
  const myMac = macToString(c.mac)
  const other = slots[target]
  slotPref[myMac] = target
  if (other?.present) {
    slotPref[macToString(other.mac)] = xi
  }
  await send(encodeSetSlot(c.mac, target))
}
</script>

<template>
  <main>
    <header class="top">
      <h1>Blupuck</h1>
      <div class="controls">
        <button v-if="!device" @click="connect" :disabled="!supported">
          <Plug :size="16" /><span>Connect</span>
        </button>
        <template v-else>
          <button @click="refresh"><RefreshCw :size="16" /><span>Refresh</span></button>
          <button @click="toggleScanning">
            <component :is="scanning ? SearchX : Search" :size="16" />
            <span>{{ scanning ? 'Stop scanning' : 'Start scanning' }}</span>
          </button>
          <button @click="forgetAll" class="danger"><Trash2 :size="16" /><span>Forget all</span></button>
          <button @click="disconnect"><PlugZap :size="16" /><span>Disconnect</span></button>
        </template>
      </div>
    </header>

    <p v-if="!supported" class="warn">WebUSB not available — use Chrome / Edge over HTTPS or localhost.</p>

    <section v-if="device">
      <h2>Controllers</h2>
      <ul class="slots">
        <li v-for="(c, xi) in xiSlots" :key="xi" class="slot" :class="{ vacant: !c?.present }">
          <div class="row">
            <div class="ident">
              <span class="num">{{ xi + 1 }}.</span>
              <template v-if="c?.present">
                <span class="name">{{ c.name || 'Unnamed' }}</span>
                <span v-if="c.mouseMode" class="badge" title="Mouse mode (toggle with Capture)">
                  <MousePointer2 :size="14" />Mouse
                </span>
                <span class="mac">{{ macToString(c.mac) }}</span>
              </template>
              <span v-else class="muted">Disconnected</span>
            </div>
            <div class="meta" v-if="c?.present">
              <span class="metric" :title="`Battery ${batteryPercent(c.battery)}%`">
                <component :is="batteryIcon(c.battery)" :size="18" />
                <span>{{ batteryPercent(c.battery) === null ? '—' : batteryPercent(c.battery) + '%' }}</span>
              </span>
              <button class="icon" :disabled="xi === 0" @click="moveSlot(xi, -1)" title="Move up">
                <ArrowUp :size="14" />
              </button>
              <button class="icon" :disabled="xi === 3" @click="moveSlot(xi, 1)" title="Move down">
                <ArrowDown :size="14" />
              </button>
              <button class="small" @click="identify(c)" title="Blink the home LED on this controller">
                <Lightbulb :size="14" />Identify
              </button>
            </div>
          </div>
          <div class="row sub" v-if="c?.present">
            <label class="forward">
              <Copy :size="14" />
              <span>Mirror into:</span>
              <select
                :value="forwardPref[macToString(c.mac)] || ''"
                @change="setForward(macToString(c.mac), ($event.target as HTMLSelectElement).value)"
              >
                <option value="">None</option>
                <option
                  v-for="t in xiSlots.filter(x => x?.present && macToString(x.mac) !== macToString(c.mac)) as ControllerPayload[]"
                  :key="macToString(t.mac)"
                  :value="macToString(t.mac)"
                >{{ t.name || 'Unnamed' }} — {{ macToString(t.mac) }}</option>
              </select>
            </label>
          </div>
        </li>
      </ul>
    </section>

    <details>
      <summary>Log</summary>
      <pre>{{ log.join('\n') }}</pre>
    </details>
  </main>
</template>

<style scoped>
main { max-width: 760px; margin: 2rem auto; padding: 0 1rem; font: 14px/1.5 system-ui, sans-serif; color: #1f2328; }
h1 { font-size: 1.5rem; margin: 0; }
h2 { font-size: 1.05rem; margin: 1.5rem 0 0.5rem; color: #444; font-weight: 600; }

.top { display: flex; justify-content: space-between; align-items: center; gap: 1rem; flex-wrap: wrap; }
.controls { display: flex; gap: 0.4rem; flex-wrap: wrap; }

button {
  display: inline-flex; align-items: center; gap: 0.4rem;
  padding: 0.45rem 0.8rem;
  border-radius: 6px; border: 1px solid #cfd2d5; background: #e9ebee; color: #1f2328;
  font: inherit; cursor: pointer;
  transition: background 0.12s, border-color 0.12s;
}
button:hover:not(:disabled) { background: #dde0e4; border-color: #b8bcc0; }
button:disabled { opacity: 0.5; cursor: not-allowed; }
button.danger { color: #b21f1f; border-color: #d8b0b0; background: #f3e5e5; }
button.small { padding: 0.25rem 0.55rem; font-size: 0.85rem; }
button.icon { padding: 0.3rem 0.4rem; }

.warn { color: #b21f1f; }
.empty {
  display: inline-flex; align-items: center; gap: 0.4rem;
  padding: 0.75rem 1rem; background: #f1f3f5; border: 1px dashed #cfd2d5;
  border-radius: 6px; color: #6b7178;
}

.slots { list-style: none; padding: 0; margin: 0; display: flex; flex-direction: column; gap: 0.5rem; }
.slot {
  padding: 0.75rem 1rem; background: #fff; border: 1px solid #e3e6e8;
  border-radius: 6px;
}
.slot.vacant { background: #f7f8fa; border-style: dashed; color: #8a8f95; }
.muted { color: #8a8f95; font-style: italic; }

.row { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
.row.sub { margin-top: 0.5rem; padding-top: 0.5rem; border-top: 1px solid #f0f1f2; }

.ident { display: flex; align-items: baseline; gap: 0.6rem; flex-wrap: wrap; }
.ident .num { color: #6b7178; font-variant-numeric: tabular-nums; }
.ident .name { font-weight: 600; }
.ident .mac { color: #8a8f95; font-family: ui-monospace, monospace; font-size: 0.78rem; }
.ident .badge {
  display: inline-flex; align-items: center; gap: 0.25rem;
  padding: 0.1rem 0.45rem; font-size: 0.75rem; font-weight: 500;
  background: #e7f0fd; color: #1b549d; border-radius: 999px; border: 1px solid #c2d5ef;
}

.meta { display: flex; gap: 0.75rem; align-items: center; }
.metric { display: inline-flex; align-items: center; gap: 0.3rem; color: #444; font-variant-numeric: tabular-nums; }

.forward { display: inline-flex; align-items: center; gap: 0.4rem; font-size: 0.85rem; color: #6b7178; }
.forward select {
  padding: 0.25rem 0.4rem; border-radius: 5px; border: 1px solid #cfd2d5;
  background: #fff; color: #1f2328; font: inherit;
}

details { margin-top: 2rem; color: #6b7178; }
summary { cursor: pointer; }
pre {
  background: #1f2328; color: #e3e6e8; padding: 0.75rem;
  border-radius: 6px; white-space: pre-wrap;
  max-height: 18rem; overflow-y: auto;
}
</style>
