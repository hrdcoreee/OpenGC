#pragma once

#include "gc_message.h"

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

class RemoteGCClient
{
public:
    RemoteGCClient(std::string host, uint16_t port, std::string token, uint64_t steamId);
    ~RemoteGCClient();

    void Start();
    void Stop();

    bool SendGCMessage(uint32_t type, const void *data, uint32_t size);
    bool IsMessageAvailable(uint32_t &size);
    bool RetrieveMessage(uint32_t &type, void *buffer, uint32_t bufferSize, uint32_t &size);

    bool IsConnected() const { return m_connected.load(); }
    uint64_t GetSteamId() const { return m_steamId; }

private:
    struct Message
    {
        uint32_t type{};
        std::vector<uint8_t> buffer;
    };

    void WorkerThread();
    bool ConnectSocket();
    void DisconnectSocket();
    bool PerformWebSocketHandshake();
    bool SendWebSocketFrame(uint8_t opcode, const void *data, size_t size);
    bool ReadExact(void *buffer, size_t bytesToRead);
    bool ReadWebSocketFrame(uint8_t &opcode, std::vector<uint8_t> &payload);

    std::string m_host;
    uint16_t m_port;
    std::string m_token;
    uint64_t m_steamId;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_connected{ false };
    std::thread m_thread;

    uintptr_t m_socket{ UINTPTR_MAX };

    std::mutex m_queueMutex;
    std::queue<Message> m_messages;

    std::recursive_mutex m_sendMutex;
    std::vector<Message> m_pendingOutgoing;
};
