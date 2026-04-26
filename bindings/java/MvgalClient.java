// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * MVGAL Java binding (JNI-based)
 *
 * Connects to the mvgald IPC socket via JNI to libmvgal.so,
 * queries device capabilities, submits a test workload, and reads telemetry.
 *
 * Build:
 *   mvn package
 *   java -jar target/mvgal-client-0.2.0.jar
 */

package org.mvgal;

import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.channels.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;

/**
 * MVGAL IPC client for Java.
 *
 * <p>Communicates with the mvgald daemon via its Unix domain socket.
 * Requires Java 16+ for {@code UnixDomainSocketAddress}.
 */
public class MvgalClient implements AutoCloseable {

    /** MVGAL IPC socket path. */
    public static final String SOCKET_PATH = "/run/mvgal/mvgal.sock";

    /** IPC message magic number. */
    public static final int IPC_MAGIC = 0x4D564741;

    /** Message types. */
    public static final int MSG_QUERY_DEVICES       = 0x0001;
    public static final int MSG_SUBMIT_WORKLOAD     = 0x0002;
    public static final int MSG_ALLOC_MEMORY        = 0x0003;
    public static final int MSG_FREE_MEMORY         = 0x0004;
    public static final int MSG_QUERY_CAPABILITIES  = 0x0005;
    public static final int MSG_SET_STRATEGY        = 0x0006;
    public static final int MSG_SUBSCRIBE_TELEMETRY = 0x0007;
    public static final int MSG_TELEMETRY           = 0x0008;

    private SocketChannel channel;
    private int seq = 0;

    /**
     * Connect to the mvgald IPC socket.
     *
     * @throws IOException if the connection fails
     */
    public void connect() throws IOException {
        UnixDomainSocketAddress addr = UnixDomainSocketAddress.of(SOCKET_PATH);
        channel = SocketChannel.open(StandardProtocolFamily.UNIX);
        channel.connect(addr);
    }

    /**
     * Disconnect from the daemon.
     */
    @Override
    public void close() throws IOException {
        if (channel != null && channel.isOpen()) {
            channel.close();
        }
    }

    /**
     * Send a message and return the response payload as a JSON string.
     *
     * @param msgType message type constant
     * @param payload JSON payload string (may be empty)
     * @return JSON response string
     * @throws IOException on I/O error
     */
    public String sendMessage(int msgType, String payload) throws IOException {
        seq++;
        byte[] payloadBytes = payload.isEmpty()
            ? new byte[0]
            : payload.getBytes(StandardCharsets.UTF_8);

        // Build 16-byte header (little-endian)
        ByteBuffer hdr = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN);
        hdr.putInt(IPC_MAGIC);
        hdr.putShort((short) 1);           // version
        hdr.putShort((short) msgType);
        hdr.putInt(payloadBytes.length);
        hdr.putInt(seq);
        hdr.flip();

        channel.write(hdr);
        if (payloadBytes.length > 0) {
            channel.write(ByteBuffer.wrap(payloadBytes));
        }

        // Read response header
        ByteBuffer respHdr = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN);
        while (respHdr.hasRemaining()) {
            channel.read(respHdr);
        }
        respHdr.flip();
        respHdr.position(8);
        int respLen = respHdr.getInt();

        if (respLen == 0) {
            return "{}";
        }

        ByteBuffer respPayload = ByteBuffer.allocate(respLen);
        while (respPayload.hasRemaining()) {
            channel.read(respPayload);
        }
        return new String(respPayload.array(), StandardCharsets.UTF_8);
    }

    /** Query all GPU devices. */
    public String queryDevices() throws IOException {
        return sendMessage(MSG_QUERY_DEVICES, "");
    }

    /** Query aggregate capabilities. */
    public String queryCapabilities() throws IOException {
        return sendMessage(MSG_QUERY_CAPABILITIES, "");
    }

    /** Submit a test compute workload. */
    public String submitTestWorkload() throws IOException {
        String payload = "{\"workload_type\":\"compute\",\"priority\":50,"
            + "\"gpu_mask\":0,\"estimated_bytes\":1048576,\"api\":\"opencl\"}";
        return sendMessage(MSG_SUBMIT_WORKLOAD, payload);
    }

    /** Subscribe to telemetry at 1-second intervals. */
    public String subscribeTelemetry() throws IOException {
        String payload = "{\"interval_ms\":1000,"
            + "\"events\":[\"gpu_utilization\",\"temperature\"]}";
        return sendMessage(MSG_SUBSCRIBE_TELEMETRY, payload);
    }

    /** Entry point for standalone test. */
    public static void main(String[] args) {
        try (MvgalClient client = new MvgalClient()) {
            client.connect();
            System.out.println("[mvgal-java] Connected to mvgald");

            String caps = client.queryCapabilities();
            System.out.println("[mvgal-java] Capabilities: " + caps);

            String devices = client.queryDevices();
            System.out.println("[mvgal-java] Devices: " + devices);

            String workload = client.submitTestWorkload();
            System.out.println("[mvgal-java] Workload: " + workload);

            System.out.println("[mvgal-java] Disconnected");
        } catch (IOException e) {
            System.err.println("[mvgal-java] Error: " + e.getMessage());
            System.exit(1);
        }
    }
}
