// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * MVGAL C# binding (P/Invoke-based)
 *
 * Connects to the mvgald IPC socket, queries device capabilities,
 * submits a test workload, and reads telemetry.
 *
 * Build:
 *   dotnet build
 *   dotnet run
 */

using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Runtime.InteropServices;

namespace Mvgal
{
    /// <summary>
    /// MVGAL IPC client for C#.
    /// Communicates with the mvgald daemon via its Unix domain socket.
    /// </summary>
    public sealed class MvgalClient : IDisposable
    {
        /// <summary>MVGAL IPC socket path.</summary>
        public const string SocketPath = "/run/mvgal/mvgal.sock";

        /// <summary>IPC message magic number.</summary>
        public const uint IpcMagic = 0x4D564741;

        /// <summary>Message type constants.</summary>
        public static class MsgType
        {
            public const ushort QueryDevices       = 0x0001;
            public const ushort SubmitWorkload     = 0x0002;
            public const ushort AllocMemory        = 0x0003;
            public const ushort FreeMemory         = 0x0004;
            public const ushort QueryCapabilities  = 0x0005;
            public const ushort SetStrategy        = 0x0006;
            public const ushort SubscribeTelemetry = 0x0007;
            public const ushort Telemetry          = 0x0008;
        }

        private Socket? _socket;
        private NetworkStream? _stream;
        private uint _seq;

        /// <summary>Connect to the mvgald IPC socket.</summary>
        public void Connect()
        {
            _socket = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
            var endpoint = new UnixDomainSocketEndPoint(SocketPath);
            _socket.Connect(endpoint);
            _stream = new NetworkStream(_socket, ownsSocket: false);
        }

        /// <summary>Disconnect from the daemon.</summary>
        public void Dispose()
        {
            _stream?.Dispose();
            _socket?.Dispose();
        }

        /// <summary>
        /// Send a message and return the response payload as a JSON string.
        /// </summary>
        public string SendMessage(ushort msgType, string payload = "")
        {
            if (_stream is null) throw new InvalidOperationException("Not connected");

            _seq++;
            byte[] payloadBytes = payload.Length > 0
                ? Encoding.UTF8.GetBytes(payload)
                : Array.Empty<byte>();

            // Build 16-byte header (little-endian)
            byte[] hdr = new byte[16];
            BitConverter.TryWriteBytes(hdr.AsSpan(0, 4), IpcMagic);
            BitConverter.TryWriteBytes(hdr.AsSpan(4, 2), (ushort)1);
            BitConverter.TryWriteBytes(hdr.AsSpan(6, 2), msgType);
            BitConverter.TryWriteBytes(hdr.AsSpan(8, 4), (uint)payloadBytes.Length);
            BitConverter.TryWriteBytes(hdr.AsSpan(12, 4), _seq);

            _stream.Write(hdr);
            if (payloadBytes.Length > 0)
                _stream.Write(payloadBytes);

            // Read response header
            byte[] respHdr = new byte[16];
            _stream.ReadExactly(respHdr);
            uint respLen = BitConverter.ToUInt32(respHdr, 8);

            if (respLen == 0) return "{}";

            byte[] respPayload = new byte[respLen];
            _stream.ReadExactly(respPayload);
            return Encoding.UTF8.GetString(respPayload);
        }

        /// <summary>Query all GPU devices.</summary>
        public JsonDocument QueryDevices()
            => JsonDocument.Parse(SendMessage(MsgType.QueryDevices));

        /// <summary>Query aggregate capabilities.</summary>
        public JsonDocument QueryCapabilities()
            => JsonDocument.Parse(SendMessage(MsgType.QueryCapabilities));

        /// <summary>Submit a test compute workload.</summary>
        public JsonDocument SubmitTestWorkload()
        {
            const string payload = """
                {"workload_type":"compute","priority":50,"gpu_mask":0,
                 "estimated_bytes":1048576,"api":"opencl"}
                """;
            return JsonDocument.Parse(SendMessage(MsgType.SubmitWorkload, payload));
        }

        /// <summary>Subscribe to telemetry at 1-second intervals.</summary>
        public JsonDocument SubscribeTelemetry()
        {
            const string payload = """
                {"interval_ms":1000,"events":["gpu_utilization","temperature"]}
                """;
            return JsonDocument.Parse(SendMessage(MsgType.SubscribeTelemetry, payload));
        }

        /// <summary>Entry point for standalone test.</summary>
        public static void Main(string[] args)
        {
            using var client = new MvgalClient();
            try
            {
                client.Connect();
                Console.WriteLine("[mvgal-csharp] Connected to mvgald");

                using var caps = client.QueryCapabilities();
                Console.WriteLine($"[mvgal-csharp] GPU count: {caps.RootElement.GetProperty("gpu_count")}");
                Console.WriteLine($"[mvgal-csharp] Total VRAM: {caps.RootElement.GetProperty("total_vram_bytes")} bytes");

                using var devices = client.QueryDevices();
                Console.WriteLine($"[mvgal-csharp] Devices: {JsonSerializer.Serialize(devices.RootElement, new JsonSerializerOptions { WriteIndented = true })}");

                using var workload = client.SubmitTestWorkload();
                Console.WriteLine($"[mvgal-csharp] Workload: {JsonSerializer.Serialize(workload.RootElement, new JsonSerializerOptions { WriteIndented = true })}");

                Console.WriteLine("[mvgal-csharp] Disconnected");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[mvgal-csharp] Error: {ex.Message}");
                Environment.Exit(1);
            }
        }
    }
}
