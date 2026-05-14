// Authoritative wire format spec: ../../../docs/protocol.md.
// Must stay in sync with firmware/include/protocol.h.

export const PROTOCOL_VERSION = 1
export const PROTOCOL_MAC_LEN = 6
export const PROTOCOL_NAME_MAX = 63

export enum MessageType {
  // Bridge → host
  Hello = 0x10,
  Controller = 0x11,
  Scanning = 0x12,
  BondList = 0x13,

  // Host → bridge
  GetStatus = 0x80,
  SetScanning = 0x81,
  ForgetMac = 0x82,
  ForgetAll = 0x83,
  SetSlot = 0x84,
  Identify = 0x85,
  SetForward = 0x86,
}

export interface HelloPayload {
  protocolVersion: number
  maxSlots: number
  firmwareMajor: number
  firmwareMinor: number
}

export interface ControllerPayload {
  slot: number
  present: boolean
  battery: number          // bluepad32: 0 = unavailable, 1 = empty, 255 = full
  rssi: number             // signed BT-Classic relative; 0 ≈ ok
  mouseMode: boolean       // mirror of CONTROLLER_FLAG_MOUSE bit
  mac: Uint8Array          // length 6
  name: string
}

export const CONTROLLER_FLAG_MOUSE = 0x01

export interface ScanningPayload {
  enabled: boolean
}

export interface BondEntry {
  mac: Uint8Array          // length 6
  preferredSlot: number    // 0..3, 0xFF = no preference yet
}

export interface BondListPayload {
  entries: BondEntry[]
}

// Pretty-print a MAC: 8C:85:90:AB:CD:EF
export function macToString(mac: Uint8Array): string {
  return Array.from(mac).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(':')
}
