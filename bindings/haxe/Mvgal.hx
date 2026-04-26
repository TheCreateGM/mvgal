// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * MVGAL Haxe language binding
 *
 * Connects to the mvgald IPC socket, queries device capabilities,
 * submits a test workload, and reads telemetry.
 *
 * Targets: sys (cpp, neko, hl)
 *
 * Usage (HashLink):
 *   haxe -main Mvgal -hl mvgal.hl
 *   hl mvgal.hl
 */

import sys.net.Socket;
import haxe.Json;
import haxe.io.Bytes;
import haxe.io.BytesOutput;

class Mvgal {
    static final SOCKET_PATH = "/run/mvgal/mvgal.sock";
    static final IPC_MAGIC:Int = 0x4D564741;

    var socket:Socket;
    var seq:Int = 0;

    public function new() {
        socket = new Socket();
    }

    public function connect():Void {
        // Unix domain socket connection via sys.net.Socket
        socket.connect(new sys.net.Host(SOCKET_PATH), 0);
    }

    public function disconnect():Void {
        socket.close();
    }

    function sendMessage(msgType:Int, payload:String = ""):Dynamic {
        seq++;

        var payloadBytes = Bytes.ofString(payload);
        var out = new BytesOutput();
        out.bigEndian = false; // little-endian

        // Header: magic(4) + ver(2) + type(2) + length(4) + seq(4) = 16 bytes
        out.writeInt32(IPC_MAGIC);
        out.writeUInt16(1);       // version
        out.writeUInt16(msgType);
        out.writeInt32(payloadBytes.length);
        out.writeInt32(seq);

        var hdrBytes = out.getBytes();
        socket.output.write(hdrBytes);
        if (payloadBytes.length > 0) {
            socket.output.write(payloadBytes);
        }
        socket.output.flush();

        // Read response header (16 bytes)
        var respHdr = socket.input.read(16);
        var respLen = respHdr.getInt32(8); // little-endian length at offset 8

        if (respLen == 0) {
            return {};
        }

        var respPayload = socket.input.read(respLen);
        return Json.parse(respPayload.toString());
    }

    public function queryDevices():Dynamic {
        return sendMessage(0x0001);
    }

    public function queryCapabilities():Dynamic {
        return sendMessage(0x0005);
    }

    public function submitTestWorkload():Dynamic {
        var payload = '{"workload_type":"compute","priority":50,"gpu_mask":0,"estimated_bytes":1048576,"api":"opencl"}';
        return sendMessage(0x0002, payload);
    }

    public function subscribeTelemetry():Dynamic {
        var payload = '{"interval_ms":1000,"events":["gpu_utilization","temperature"]}';
        return sendMessage(0x0007, payload);
    }

    static function main():Void {
        var client = new Mvgal();
        try {
            client.connect();
            trace("[mvgal-haxe] Connected to mvgald");

            var caps = client.queryCapabilities();
            trace("[mvgal-haxe] GPU count: " + caps.gpu_count);
            trace("[mvgal-haxe] Total VRAM: " + caps.total_vram_bytes + " bytes");

            var devices = client.queryDevices();
            trace("[mvgal-haxe] Devices: " + Json.stringify(devices, null, "  "));

            var workload = client.submitTestWorkload();
            trace("[mvgal-haxe] Workload: " + Json.stringify(workload, null, "  "));

            client.disconnect();
            trace("[mvgal-haxe] Disconnected");
        } catch (e:Dynamic) {
            trace("[mvgal-haxe] Error: " + e);
        }
    }
}
