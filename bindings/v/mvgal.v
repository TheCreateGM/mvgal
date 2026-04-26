// SPDX-License-Identifier: MIT OR Apache-2.0
// MVGAL V language binding
//
// Connects to the mvgald IPC socket, queries device capabilities,
// submits a test workload, and reads telemetry.
//
// Usage:
//   v run mvgal.v

module main

import net.unix
import json
import encoding.binary

const mvgal_socket_path = '/run/mvgal/mvgal.sock'
const mvgal_ipc_magic = u32(0x4D564741)

enum MvgalMsgType as u16 {
	query_devices       = 0x0001
	submit_workload     = 0x0002
	alloc_memory        = 0x0003
	free_memory         = 0x0004
	query_capabilities  = 0x0005
	set_strategy        = 0x0006
	subscribe_telemetry = 0x0007
	telemetry           = 0x0008
}

struct MvgalIpcHeader {
	magic    u32
	ver      u16
	msg_type u16
	length   u32
	seq      u32
}

struct MvgalClient {
mut:
	conn unix.StreamConn
	seq  u32
}

fn new_mvgal_client() !MvgalClient {
	conn := unix.connect_stream(mvgal_socket_path)!
	return MvgalClient{
		conn: conn
	}
}

fn (mut c MvgalClient) disconnect() {
	c.conn.close() or {}
}

fn (mut c MvgalClient) send_message(msg_type MvgalMsgType, payload string) !string {
	c.seq++

	// Build 16-byte header (little-endian)
	mut hdr := []u8{len: 16}
	binary.little_endian_put_u32(mut hdr[0..4], mvgal_ipc_magic)
	binary.little_endian_put_u16(mut hdr[4..6], u16(1))
	binary.little_endian_put_u16(mut hdr[6..8], u16(msg_type))
	binary.little_endian_put_u32(mut hdr[8..12], u32(payload.len))
	binary.little_endian_put_u32(mut hdr[12..16], c.seq)

	c.conn.write(hdr)!
	if payload.len > 0 {
		c.conn.write(payload.bytes())!
	}

	// Read response header
	mut resp_hdr := []u8{len: 16}
	c.conn.read(mut resp_hdr)!
	resp_len := binary.little_endian_u32(resp_hdr[8..12])

	if resp_len == 0 {
		return '{}'
	}

	mut resp_payload := []u8{len: int(resp_len)}
	c.conn.read(mut resp_payload)!
	return resp_payload.bytestr()
}

fn (mut c MvgalClient) query_devices() !map[string]json.Any {
	resp := c.send_message(.query_devices, '')!
	return json.decode(map[string]json.Any, resp)!
}

fn (mut c MvgalClient) query_capabilities() !map[string]json.Any {
	resp := c.send_message(.query_capabilities, '')!
	return json.decode(map[string]json.Any, resp)!
}

fn (mut c MvgalClient) submit_test_workload() !map[string]json.Any {
	payload := '{"workload_type":"compute","priority":50,"gpu_mask":0,"estimated_bytes":1048576,"api":"opencl"}'
	resp := c.send_message(.submit_workload, payload)!
	return json.decode(map[string]json.Any, resp)!
}

fn main() {
	mut client := new_mvgal_client() or {
		eprintln('[mvgal-v] Failed to connect: ${err}')
		return
	}
	defer {
		client.disconnect()
	}

	println('[mvgal-v] Connected to mvgald')

	caps := client.query_capabilities() or {
		eprintln('[mvgal-v] query_capabilities failed: ${err}')
		return
	}
	println('[mvgal-v] GPU count: ${caps["gpu_count"]}')
	println('[mvgal-v] Total VRAM: ${caps["total_vram_bytes"]} bytes')

	devices := client.query_devices() or {
		eprintln('[mvgal-v] query_devices failed: ${err}')
		return
	}
	println('[mvgal-v] Devices: ${devices}')

	workload := client.submit_test_workload() or {
		eprintln('[mvgal-v] submit_workload failed: ${err}')
		return
	}
	println('[mvgal-v] Workload: ${workload}')

	println('[mvgal-v] Disconnected')
}
