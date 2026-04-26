# SPDX-License-Identifier: MIT OR Apache-2.0
## MVGAL Nim language binding
##
## Connects to the mvgald IPC socket, queries device capabilities,
## submits a test workload, and reads telemetry.
##
## Usage:
##   nim c -r mvgal.nim

import std/[net, json, strformat, os]

const
  MvgalSocketPath* = "/run/mvgal/mvgal.sock"
  MvgalIpcMagic*   = 0x4D564741'u32

type
  MvgalMsgType* = enum
    QueryDevices      = 0x0001
    SubmitWorkload    = 0x0002
    AllocMemory       = 0x0003
    FreeMemory        = 0x0004
    QueryCapabilities = 0x0005
    SetStrategy       = 0x0006
    SubscribeTelemetry = 0x0007
    Telemetry         = 0x0008

  MvgalIpcHeader* = object
    magic*:  uint32
    ver*:    uint16
    msgType*: uint16
    length*: uint32
    seq*:    uint32

  MvgalClient* = ref object
    socket: Socket
    seq:    uint32

proc newMvgalClient*(): MvgalClient =
  ## Create a new MVGAL IPC client.
  result = MvgalClient(socket: newSocket(AF_UNIX, SOCK_STREAM, IPPROTO_IP))

proc connect*(client: MvgalClient) =
  ## Connect to the mvgald IPC socket.
  client.socket.connectUnix(MvgalSocketPath)

proc disconnect*(client: MvgalClient) =
  ## Disconnect from the daemon.
  client.socket.close()

proc sendMessage*(client: MvgalClient, msgType: MvgalMsgType,
                  payload: string = ""): JsonNode =
  ## Send a message and return the parsed JSON response.
  inc client.seq

  # Build header (little-endian)
  var hdr: array[16, byte]
  let magic = MvgalIpcMagic
  copyMem(addr hdr[0], unsafeAddr magic, 4)
  let ver: uint16 = 1
  copyMem(addr hdr[4], unsafeAddr ver, 2)
  let mt = uint16(msgType)
  copyMem(addr hdr[6], unsafeAddr mt, 2)
  let plen = uint32(payload.len)
  copyMem(addr hdr[8], unsafeAddr plen, 4)
  copyMem(addr hdr[12], unsafeAddr client.seq, 4)

  client.socket.send(cast[string](hdr))
  if payload.len > 0:
    client.socket.send(payload)

  # Receive response header
  var respHdr: array[16, byte]
  discard client.socket.recv(cast[var string](respHdr), 16)
  var respLen: uint32
  copyMem(addr respLen, addr respHdr[8], 4)

  if respLen == 0:
    return parseJson("{}")

  var respPayload = newString(int(respLen))
  discard client.socket.recv(respPayload, int(respLen))
  return parseJson(respPayload)

proc queryDevices*(client: MvgalClient): JsonNode =
  ## Query all GPU devices.
  client.sendMessage(QueryDevices)

proc queryCapabilities*(client: MvgalClient): JsonNode =
  ## Query aggregate capabilities.
  client.sendMessage(QueryCapabilities)

proc submitTestWorkload*(client: MvgalClient): JsonNode =
  ## Submit a test compute workload.
  let payload = """{"workload_type":"compute","priority":50,"gpu_mask":0,"estimated_bytes":1048576,"api":"opencl"}"""
  client.sendMessage(SubmitWorkload, payload)

proc subscribeTelemetry*(client: MvgalClient): JsonNode =
  ## Subscribe to telemetry at 1-second intervals.
  let payload = """{"interval_ms":1000,"events":["gpu_utilization","temperature"]}"""
  client.sendMessage(SubscribeTelemetry, payload)

when isMainModule:
  let client = newMvgalClient()
  try:
    client.connect()
    echo "[mvgal-nim] Connected to mvgald"

    let caps = client.queryCapabilities()
    echo fmt"[mvgal-nim] GPU count: {caps[\"gpu_count\"]}"
    echo fmt"[mvgal-nim] Total VRAM: {caps[\"total_vram_bytes\"]} bytes"

    let devices = client.queryDevices()
    echo "[mvgal-nim] Devices: ", devices.pretty()

    let workload = client.submitTestWorkload()
    echo "[mvgal-nim] Workload: ", workload.pretty()

    client.disconnect()
    echo "[mvgal-nim] Disconnected"
  except:
    echo "[mvgal-nim] Error: ", getCurrentExceptionMsg()
