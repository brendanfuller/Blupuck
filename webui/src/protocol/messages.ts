// Binary frame codec for the WebUSB control channel.
// Frame layout: [type:u8][len:u16 LE][payload].

import { CONTROLLER_FLAG_MOUSE, MessageType, PROTOCOL_MAC_LEN } from './types'
import type {
  BondEntry,
  BondListPayload,
  ControllerPayload,
  HelloPayload,
  ScanningPayload,
} from './types'

const HEADER_LEN = 3

export interface Frame {
  type: MessageType
  payload: Uint8Array
}

export function encodeFrame(type: MessageType, payload: Uint8Array = new Uint8Array(0)): Uint8Array {
  const buf = new Uint8Array(HEADER_LEN + payload.length)
  buf[0] = type
  buf[1] = payload.length & 0xff
  buf[2] = (payload.length >> 8) & 0xff
  buf.set(payload, HEADER_LEN)
  return buf
}

// Decoder is stream-oriented — feed bytes as they arrive, pull complete frames out.
export class FrameDecoder {
  private buf = new Uint8Array(0)

  feed(chunk: Uint8Array): Frame[] {
    const merged = new Uint8Array(this.buf.length + chunk.length)
    merged.set(this.buf, 0)
    merged.set(chunk, this.buf.length)
    this.buf = merged

    const frames: Frame[] = []
    while (this.buf.length >= HEADER_LEN) {
      const len = this.buf[1] | (this.buf[2] << 8)
      if (this.buf.length < HEADER_LEN + len) break
      frames.push({
        type: this.buf[0] as MessageType,
        payload: this.buf.slice(HEADER_LEN, HEADER_LEN + len),
      })
      this.buf = this.buf.slice(HEADER_LEN + len)
    }
    return frames
  }
}

// ----- payload codecs -------------------------------------------------------

export function decodeHello(p: Uint8Array): HelloPayload {
  return {
    protocolVersion: p[0],
    maxSlots: p[1],
    firmwareMajor: p[2],
    firmwareMinor: p[3],
  }
}

export function decodeController(p: Uint8Array): ControllerPayload {
  const slot = p[0]
  const present = !!p[1]
  const battery = p[2]
  const rssi = (p[3] << 24) >> 24  // sign-extend i8
  const flags = p[4]
  const mouseMode = (flags & CONTROLLER_FLAG_MOUSE) !== 0
  const mac = p.slice(5, 5 + PROTOCOL_MAC_LEN)
  const nameLen = p[11]
  const name = new TextDecoder().decode(p.slice(12, 12 + nameLen))
  return { slot, present, battery, rssi, mouseMode, mac, name }
}

export function decodeScanning(p: Uint8Array): ScanningPayload {
  return { enabled: !!p[0] }
}

export function decodeBondList(p: Uint8Array): BondListPayload {
  const count = p[0]
  const entries: BondEntry[] = []
  for (let i = 0; i < count; i++) {
    const off = 1 + i * 7
    entries.push({
      mac: p.slice(off, off + PROTOCOL_MAC_LEN),
      preferredSlot: p[off + PROTOCOL_MAC_LEN],
    })
  }
  return { entries }
}

export function encodeSetScanning(enabled: boolean): Uint8Array {
  return encodeFrame(MessageType.SetScanning, new Uint8Array([enabled ? 1 : 0]))
}

export function encodeForgetMac(mac: Uint8Array): Uint8Array {
  return encodeFrame(MessageType.ForgetMac, mac)
}

export function encodeForgetAll(): Uint8Array {
  return encodeFrame(MessageType.ForgetAll)
}

export function encodeGetStatus(): Uint8Array {
  return encodeFrame(MessageType.GetStatus)
}

export function encodeSetSlot(mac: Uint8Array, slot: number): Uint8Array {
  const payload = new Uint8Array(PROTOCOL_MAC_LEN + 1)
  payload.set(mac, 0)
  payload[PROTOCOL_MAC_LEN] = slot
  return encodeFrame(MessageType.SetSlot, payload)
}
