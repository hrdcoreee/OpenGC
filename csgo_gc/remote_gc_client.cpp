#include "stdafx.h"
#include "remote_gc_client.h"
#include "platform.h"

#include <chrono>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace
{

#ifdef _WIN32
using SocketType = SOCKET;
constexpr SocketType InvalidSocket = INVALID_SOCKET;
#else
using SocketType = int;
constexpr SocketType InvalidSocket = -1;
#endif

SocketType FromHandle(uintptr_t handle)
{
    return static_cast<SocketType>(handle);
}

uintptr_t ToHandle(SocketType socket)
{
    return static_cast<uintptr_t>(socket);
}

void CloseSocket(SocketType socket)
{
    if (socket == InvalidSocket)
    {
        return;
    }

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

constexpr uint8_t WsOpcodeContinuation = 0x00;
constexpr uint8_t WsOpcodeText         = 0x01;
constexpr uint8_t WsOpcodeBinary       = 0x02;
constexpr uint8_t WsOpcodeClose        = 0x08;
constexpr uint8_t WsOpcodePing         = 0x09;
constexpr uint8_t WsOpcodePong         = 0x0A;

} // namespace

RemoteGCClient::RemoteGCClient(std::string host, uint16_t port, std::string token, uint64_t steamId)
    : m_host{ std::move(host) }
    , m_port{ port }
    , m_token{ std::move(token) }
    , m_steamId{ steamId }
{
}

RemoteGCClient::~RemoteGCClient()
{
    Stop();
}

void RemoteGCClient::Start()
{
    if (m_running.exchange(true))
    {
        return;
    }

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    m_thread = std::thread{ &RemoteGCClient::WorkerThread, this };
    Platform::Print("RemoteGCClient: started for %s:%u (SteamID: %llu)\n",
        m_host.c_str(), m_port, m_steamId);
}

void RemoteGCClient::Stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }

    DisconnectSocket();

    if (m_thread.joinable())
    {
        m_thread.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    Platform::Print("RemoteGCClient: stopped\n");
}

bool RemoteGCClient::SendGCMessage(uint32_t type, const void *data, uint32_t size)
{
    if (!data || size == 0)
    {
        return false;
    }

    if (!m_connected.load())
    {
        std::lock_guard<std::recursive_mutex> lock{ m_sendMutex };
        Message &msg = m_pendingOutgoing.emplace_back();
        msg.type = type;
        const auto *bytes = static_cast<const uint8_t *>(data);
        msg.buffer.assign(bytes, bytes + size);
        Platform::Print("RemoteGCClient: queued message %u while connecting\n", type);
        return true;
    }

    return SendWebSocketFrame(WsOpcodeBinary, data, size);
}

bool RemoteGCClient::IsMessageAvailable(uint32_t &size)
{
    std::lock_guard<std::mutex> lock{ m_queueMutex };
    if (m_messages.empty())
    {
        return false;
    }

    size = static_cast<uint32_t>(m_messages.front().buffer.size());
    return true;
}

bool RemoteGCClient::RetrieveMessage(uint32_t &type, void *buffer, uint32_t bufferSize, uint32_t &size)
{
    std::lock_guard<std::mutex> lock{ m_queueMutex };
    if (m_messages.empty())
    {
        size = 0;
        return false;
    }

    Message &front = m_messages.front();
    type = front.type;
    size = static_cast<uint32_t>(front.buffer.size());

    if (bufferSize < front.buffer.size())
    {
        return false;
    }

    if (!front.buffer.empty() && buffer)
    {
        std::memcpy(buffer, front.buffer.data(), front.buffer.size());
    }

    m_messages.pop();
    return true;
}

bool RemoteGCClient::ConnectSocket()
{
    DisconnectSocket();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(m_port);
    addrinfo *resultList = nullptr;

    int error = getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &resultList);
    if (error != 0 || !resultList)
    {
        Platform::Print("RemoteGCClient: getaddrinfo failed for %s:%s\n", m_host.c_str(), portStr.c_str());
        return false;
    }

    SocketType sock = InvalidSocket;
    for (addrinfo *ptr = resultList; ptr != nullptr; ptr = ptr->ai_next)
    {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == InvalidSocket)
        {
            continue;
        }

        // Set reasonable timeouts so network stalls don't hang frames
#ifdef _WIN32
        DWORD timeoutMs = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
#else
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0)
        {
            break;
        }

        CloseSocket(sock);
        sock = InvalidSocket;
    }

    freeaddrinfo(resultList);

    if (sock == InvalidSocket)
    {
        return false;
    }

    m_socket = ToHandle(sock);
    return true;
}

void RemoteGCClient::DisconnectSocket()
{
    m_connected.store(false);

    if (m_socket != UINTPTR_MAX)
    {
        CloseSocket(FromHandle(m_socket));
        m_socket = UINTPTR_MAX;
    }
}

bool RemoteGCClient::PerformWebSocketHandshake()
{
    SocketType sock = FromHandle(m_socket);
    if (sock == InvalidSocket)
    {
        return false;
    }

    // Build standard RFC 6455 handshake request
    std::string request;
    request.reserve(512);
    request.append("GET /ws/gc HTTP/1.1\r\n");
    request.append("Host: ").append(m_host).append(":").append(std::to_string(m_port)).append("\r\n");
    request.append("Upgrade: websocket\r\n");
    request.append("Connection: Upgrade\r\n");
    request.append("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
    request.append("Sec-WebSocket-Version: 13\r\n");
    request.append("X-Steam-Id: ").append(std::to_string(m_steamId)).append("\r\n");

    if (!m_token.empty())
    {
        request.append("Authorization: Bearer ").append(m_token).append("\r\n");
    }

    request.append("\r\n");

    // Send HTTP upgrade request
    int sent = send(sock, request.data(), static_cast<int>(request.size()), 0);
    if (sent != static_cast<int>(request.size()))
    {
        Platform::Print("RemoteGCClient: failed to send handshake request\n");
        return false;
    }

    // Read response until double CRLF
    std::string response;
    char buffer[512];
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        int bytes = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes <= 0)
        {
            Platform::Print("RemoteGCClient: handshake read failed\n");
            return false;
        }
        response.append(buffer, bytes);
        if (response.size() > 4096)
        {
            Platform::Print("RemoteGCClient: handshake response too large\n");
            return false;
        }
    }

    // Check for HTTP 101 Switching Protocols
    if (response.find("101") == std::string::npos)
    {
        Platform::Print("RemoteGCClient: handshake rejected (not 101):\n%s\n", response.c_str());
        return false;
    }

    Platform::Print("RemoteGCClient: WebSocket handshake completed successfully!\n");
    return true;
}

bool RemoteGCClient::SendWebSocketFrame(uint8_t opcode, const void *data, size_t size)
{
    std::lock_guard<std::recursive_mutex> lock{ m_sendMutex };

    SocketType sock = FromHandle(m_socket);
    if (sock == InvalidSocket || !m_connected.load())
    {
        return false;
    }

    std::vector<uint8_t> frame;
    frame.reserve(size + 14);

    // Byte 0: FIN bit (0x80) | opcode
    frame.push_back(0x80 | (opcode & 0x0F));

    // Client-to-server frames MUST be masked (0x80 bit)
    if (size <= 125)
    {
        frame.push_back(0x80 | static_cast<uint8_t>(size));
    }
    else if (size <= 65535)
    {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((size >> 8) & 0xff));
        frame.push_back(static_cast<uint8_t>(size & 0xff));
    }
    else
    {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i)
        {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(size) >> (i * 8)) & 0xff));
        }
    }

    // 4-byte random masking key
    static thread_local std::mt19937 rng(std::random_device{}());
    uint32_t maskKey = rng();
    const uint8_t *maskBytes = reinterpret_cast<const uint8_t *>(&maskKey);
    frame.insert(frame.end(), maskBytes, maskBytes + 4);

    // Mask the payload data
    const uint8_t *src = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
    {
        frame.push_back(src[i] ^ maskBytes[i % 4]);
    }

    // Send complete frame
    size_t totalSent = 0;
    while (totalSent < frame.size())
    {
        int bytes = send(sock, reinterpret_cast<const char *>(frame.data() + totalSent),
            static_cast<int>(frame.size() - totalSent), 0);
        if (bytes <= 0)
        {
            Platform::Print("RemoteGCClient: send frame error\n");
            DisconnectSocket();
            return false;
        }
        totalSent += bytes;
    }

    return true;
}

bool RemoteGCClient::ReadExact(void *buffer, size_t bytesToRead)
{
    SocketType sock = FromHandle(m_socket);
    if (sock == InvalidSocket)
    {
        return false;
    }

    uint8_t *dest = static_cast<uint8_t *>(buffer);
    size_t totalReceived = 0;

    while (totalReceived < bytesToRead)
    {
        if (!m_running.load() || !m_connected.load())
        {
            return false;
        }

        int bytes = recv(sock, reinterpret_cast<char *>(dest + totalReceived),
            static_cast<int>(bytesToRead - totalReceived), 0);
        if (bytes <= 0)
        {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (bytes < 0 && (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) && totalReceived == 0)
            {
                // Idle timeout waiting for next frame, keep connection open and continue!
                continue;
            }
#else
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && totalReceived == 0)
            {
                continue;
            }
#endif
            return false;
        }
        totalReceived += bytes;
    }

    return true;
}

bool RemoteGCClient::ReadWebSocketFrame(uint8_t &opcode, std::vector<uint8_t> &payload)
{
    uint8_t header[2];
    if (!ReadExact(header, 2))
    {
        return false;
    }

    opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payloadLength = header[1] & 0x7F;

    if (payloadLength == 126)
    {
        uint8_t extLen[2];
        if (!ReadExact(extLen, 2))
        {
            return false;
        }
        payloadLength = (static_cast<uint64_t>(extLen[0]) << 8) | extLen[1];
    }
    else if (payloadLength == 127)
    {
        uint8_t extLen[8];
        if (!ReadExact(extLen, 8))
        {
            return false;
        }
        payloadLength = 0;
        for (int i = 0; i < 8; ++i)
        {
            payloadLength = (payloadLength << 8) | extLen[i];
        }
    }

    uint8_t maskKey[4] = { 0, 0, 0, 0 };
    if (masked)
    {
        if (!ReadExact(maskKey, 4))
        {
            return false;
        }
    }

    // Safety guard against malicious payload sizes (max 64MB)
    if (payloadLength > 64 * 1024 * 1024)
    {
        Platform::Print("RemoteGCClient: received abnormally large frame (%llu bytes)\n", payloadLength);
        return false;
    }

    payload.resize(static_cast<size_t>(payloadLength));
    if (payloadLength > 0)
    {
        if (!ReadExact(payload.data(), static_cast<size_t>(payloadLength)))
        {
            return false;
        }

        if (masked)
        {
            for (size_t i = 0; i < payloadLength; ++i)
            {
                payload[i] ^= maskKey[i % 4];
            }
        }
    }

    return true;
}

void RemoteGCClient::WorkerThread()
{
    while (m_running.load())
    {
        try
        {
            Platform::Print("RemoteGCClient: attempting connection to %s:%u...\n",
                m_host.c_str(), m_port);

            if (ConnectSocket() && PerformWebSocketHandshake())
            {
                m_connected.store(true);
                Platform::Print("RemoteGCClient: connected to remote GC server!\n");

                // Flush any messages queued before connection established
                std::vector<Message> toSend;
                {
                    std::lock_guard<std::recursive_mutex> lock{ m_sendMutex };
                    toSend.swap(m_pendingOutgoing);
                }

                for (const Message &msg : toSend)
                {
                    if (!msg.buffer.empty())
                    {
                        SendWebSocketFrame(WsOpcodeBinary, msg.buffer.data(), msg.buffer.size());
                        Platform::Print("RemoteGCClient: flushed queued message %u (%zu bytes)\n", msg.type, msg.buffer.size());
                    }
                }

                while (m_running.load() && m_connected.load())
                {
                    uint8_t opcode = 0;
                    std::vector<uint8_t> payload;

                    if (!ReadWebSocketFrame(opcode, payload))
                    {
                        Platform::Print("RemoteGCClient: connection lost or closed by server\n");
                        break;
                    }

                    if (opcode == WsOpcodePing)
                    {
                        // Respond with Pong
                        SendWebSocketFrame(WsOpcodePong, payload.data(), payload.size());
                    }
                    else if (opcode == WsOpcodeClose)
                    {
                        Platform::Print("RemoteGCClient: received Close frame from server\n");
                        break;
                    }
                    else if (opcode == WsOpcodeBinary)
                    {
                        if (payload.size() >= sizeof(uint32_t))
                        {
                            uint32_t msgType = static_cast<uint32_t>(payload[0])
                                | (static_cast<uint32_t>(payload[1]) << 8)
                                | (static_cast<uint32_t>(payload[2]) << 16)
                                | (static_cast<uint32_t>(payload[3]) << 24);

                            {
                                std::lock_guard<std::mutex> lock{ m_queueMutex };
                                Message &msg = m_messages.emplace();
                                msg.type = msgType;
                                msg.buffer = std::move(payload);
                            }
                        }
                    }
                }
            }
            else
            {
                Platform::Print("RemoteGCClient: connection attempt failed\n");
            }

            DisconnectSocket();
        }
        catch (const std::exception &e)
        {
            Platform::Print("RemoteGCClient: exception in WorkerThread: %s\n", e.what());
            DisconnectSocket();
        }
        catch (...)
        {
            Platform::Print("RemoteGCClient: unknown exception in WorkerThread\n");
            DisconnectSocket();
        }

        // Wait before reconnecting
        for (int i = 0; i < 30 && m_running.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
