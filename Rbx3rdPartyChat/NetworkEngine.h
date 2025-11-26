#pragma once
#include <string>
#include <memory>
#include <functional>
#include <vector>

class PeerConnector;

class INetworkObserver
{
public:
    virtual void onMessageReceived(const std::string &msg) = 0;
    virtual void onConnectionEstablished(const std::string &remote_endpoint, bool is_incoming) = 0;
    virtual void onPingResult(const std::string &remote_endpoint, long long ms) = 0;
    virtual void onDisconnect(const std::string &remote_endpoint) = 0;
    virtual ~INetworkObserver() = default;
};

class NetworkEngine : public INetworkObserver, public std::enable_shared_from_this<NetworkEngine>
{
public:
    NetworkEngine();
    ~NetworkEngine();

    void start();
    void stop();

    void startListening(int port);

    void addPersistentPeer(const std::string &ip, int port);

    void broadcast(const std::string &msg);

    void setUiCallback(std::function<void(std::string)> cb);
    void setPingCallback(std::function<void(std::string, long long)> cb);

    void clearCallbacks();

    void onMessageReceived(const std::string &msg) override;
    void onConnectionEstablished(const std::string &ip, bool is_incoming) override;
    void onPingResult(const std::string &ip, long long ms) override;
    void onDisconnect(const std::string &ip) override;

private:
    friend class PeerConnector;
    void connectToPeer(const std::string &ip, int port);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};