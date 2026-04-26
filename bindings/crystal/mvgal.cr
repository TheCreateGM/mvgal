# SPDX-License-Identifier: MIT OR Apache-2.0
# MVGAL Crystal language binding
#
# Connects to the mvgald IPC socket, queries device capabilities,
# submits a test workload, and reads telemetry.
#
# Usage:
#   crystal run mvgal.cr

require "socket"
require "json"

MVGAL_SOCKET_PATH = "/run/mvgal/mvgal.sock"
MVGAL_IPC_MAGIC   = 0x4D564741_u32

enum MvgalMsgType : UInt16
  QueryDevices       = 0x0001
  SubmitWorkload     = 0x0002
  AllocMemory        = 0x0003
  FreeMemory         = 0x0004
  QueryCapabilities  = 0x0005
  SetStrategy        = 0x0006
  SubscribeTelemetry = 0x0007
  Telemetry          = 0x0008
end

class MvgalClient
  @socket : UNIXSocket
  @seq : UInt32 = 0_u32

  def initialize
    @socket = UNIXSocket.new(MVGAL_SOCKET_PATH)
  end

  def disconnect
    @socket.close
  end

  def send_message(msg_type : MvgalMsgType, payload : String = "") : JSON::Any
    @seq += 1

    # Build 16-byte header (little-endian)
    io = IO::Memory.new(16)
    io.write_bytes(MVGAL_IPC_MAGIC, IO::ByteFormat::LittleEndian)
    io.write_bytes(1_u16, IO::ByteFormat::LittleEndian)
    io.write_bytes(msg_type.value, IO::ByteFormat::LittleEndian)
    io.write_bytes(payload.bytesize.to_u32, IO::ByteFormat::LittleEndian)
    io.write_bytes(@seq, IO::ByteFormat::LittleEndian)

    @socket.write(io.to_slice)
    @socket.write(payload.to_slice) unless payload.empty?

    # Read response header
    resp_hdr = Bytes.new(16)
    @socket.read_fully(resp_hdr)
    resp_len = IO::ByteFormat::LittleEndian.decode(UInt32, resp_hdr[8, 4])

    return JSON.parse("{}") if resp_len == 0

    resp_payload = Bytes.new(resp_len)
    @socket.read_fully(resp_payload)
    JSON.parse(String.new(resp_payload))
  end

  def query_devices : JSON::Any
    send_message(MvgalMsgType::QueryDevices)
  end

  def query_capabilities : JSON::Any
    send_message(MvgalMsgType::QueryCapabilities)
  end

  def submit_test_workload : JSON::Any
    payload = %({"workload_type":"compute","priority":50,"gpu_mask":0,"estimated_bytes":1048576,"api":"opencl"})
    send_message(MvgalMsgType::SubmitWorkload, payload)
  end

  def subscribe_telemetry : JSON::Any
    payload = %({"interval_ms":1000,"events":["gpu_utilization","temperature"]})
    send_message(MvgalMsgType::SubscribeTelemetry, payload)
  end
end

# Entry point
begin
  client = MvgalClient.new
  puts "[mvgal-crystal] Connected to mvgald"

  caps = client.query_capabilities
  puts "[mvgal-crystal] GPU count: #{caps["gpu_count"]}"
  puts "[mvgal-crystal] Total VRAM: #{caps["total_vram_bytes"]} bytes"

  devices = client.query_devices
  puts "[mvgal-crystal] Devices: #{devices.to_pretty_json}"

  workload = client.submit_test_workload
  puts "[mvgal-crystal] Workload: #{workload.to_pretty_json}"

  client.disconnect
  puts "[mvgal-crystal] Disconnected"
rescue ex
  STDERR.puts "[mvgal-crystal] Error: #{ex.message}"
  exit 1
end
