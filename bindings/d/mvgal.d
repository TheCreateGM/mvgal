// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * MVGAL D language binding
 *
 * Connects to the mvgald IPC socket, queries device capabilities,
 * submits a test workload, and reads telemetry.
 *
 * Usage:
 *   dmd mvgal.d -of=mvgal_d_test
 *   ./mvgal_d_test
 */

module mvgal;

import std.socket;
import std.json;
import std.stdio;
import std.conv;
import std.string;
import core.sys.posix.sys.un;

/// MVGAL IPC socket path
enum MVGAL_SOCKET_PATH = "/run/mvgal/mvgal.sock";

/// IPC message magic number
enum uint MVGAL_IPC_MAGIC = 0x4D564741;

/// IPC message header (matches C struct mvgal_ipc_header_t)
struct MvgalIpcHeader {
    uint   magic;
    ushort ver;
    ushort type_;
    uint   length;
    uint   seq;
}

/// Message types
enum MvgalMsgType : ushort {
    QueryDevices     = 0x0001,
    SubmitWorkload   = 0x0002,
    AllocMemory      = 0x0003,
    FreeMemory       = 0x0004,
    QueryCapabilities = 0x0005,
    SetStrategy      = 0x0006,
    SubscribeTelemetry = 0x0007,
    Telemetry        = 0x0008,
}

/// MVGAL IPC client
class MvgalClient {
    private Socket sock;
    private uint seq;

    /// Connect to the mvgald IPC socket.
    void connect() {
        sock = new Socket(AddressFamily.UNIX, SocketType.STREAM);
        auto addr = new UnixAddress(MVGAL_SOCKET_PATH);
        sock.connect(addr);
    }

    /// Disconnect from the daemon.
    void disconnect() {
        if (sock !is null) {
            sock.close();
            sock = null;
        }
    }

    /// Send a message and receive the response payload as a JSON string.
    string sendMessage(MvgalMsgType type_, string payload = "") {
        import std.bitmanip : nativeToLittleEndian;

        ubyte[] payloadBytes = cast(ubyte[]) payload;
        MvgalIpcHeader hdr;
        hdr.magic  = MVGAL_IPC_MAGIC;
        hdr.ver    = 1;
        hdr.type_  = cast(ushort) type_;
        hdr.length = cast(uint) payloadBytes.length;
        hdr.seq    = ++seq;

        // Send header
        ubyte[MvgalIpcHeader.sizeof] hdrBytes;
        hdrBytes[0..4]  = (cast(ubyte*) &hdr.magic)[0..4];
        hdrBytes[4..6]  = (cast(ubyte*) &hdr.ver)[0..2];
        hdrBytes[6..8]  = (cast(ubyte*) &hdr.type_)[0..2];
        hdrBytes[8..12] = (cast(ubyte*) &hdr.length)[0..4];
        hdrBytes[12..16] = (cast(ubyte*) &hdr.seq)[0..4];
        sock.send(hdrBytes);

        // Send payload
        if (payloadBytes.length > 0) {
            sock.send(payloadBytes);
        }

        // Receive response header
        ubyte[MvgalIpcHeader.sizeof] respHdrBytes;
        sock.receive(respHdrBytes);

        MvgalIpcHeader respHdr;
        respHdr.length = *cast(uint*) &respHdrBytes[8];

        // Receive response payload
        if (respHdr.length == 0) {
            return "{}";
        }
        ubyte[] respPayload = new ubyte[respHdr.length];
        sock.receive(respPayload);
        return cast(string) respPayload;
    }

    /// Query all GPU devices.
    JSONValue queryDevices() {
        string resp = sendMessage(MvgalMsgType.QueryDevices);
        return parseJSON(resp);
    }

    /// Query aggregate capabilities.
    JSONValue queryCapabilities() {
        string resp = sendMessage(MvgalMsgType.QueryCapabilities);
        return parseJSON(resp);
    }

    /// Submit a test workload.
    JSONValue submitTestWorkload() {
        string payload = `{"workload_type":"compute","priority":50,"gpu_mask":0,"estimated_bytes":1048576,"api":"opencl"}`;
        string resp = sendMessage(MvgalMsgType.SubmitWorkload, payload);
        return parseJSON(resp);
    }

    /// Subscribe to telemetry at 1-second intervals.
    JSONValue subscribeTelemetry() {
        string payload = `{"interval_ms":1000,"events":["gpu_utilization","temperature"]}`;
        string resp = sendMessage(MvgalMsgType.SubscribeTelemetry, payload);
        return parseJSON(resp);
    }
}

/// Entry point for standalone test.
void main() {
    auto client = new MvgalClient();

    try {
        client.connect();
        writeln("[mvgal-d] Connected to mvgald");

        auto caps = client.queryCapabilities();
        writefln("[mvgal-d] GPU count: %s", caps["gpu_count"]);
        writefln("[mvgal-d] Total VRAM: %s bytes", caps["total_vram_bytes"]);

        auto devices = client.queryDevices();
        writefln("[mvgal-d] Devices: %s", devices.toPrettyString());

        auto workload = client.submitTestWorkload();
        writefln("[mvgal-d] Workload submitted: %s", workload.toPrettyString());

        client.disconnect();
        writeln("[mvgal-d] Disconnected");
    } catch (Exception e) {
        writefln("[mvgal-d] Error: %s", e.msg);
    }
}
