#include "pch.h"
#include "NetworkEngine.h"
#include "CertHelper.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class WssSession : public std::enable_shared_from_this<WssSession>
{
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer buffer_;
    std::string remote_endpoint_str_;
    INetworkObserver *observer_;
    std::deque<std::string> write_queue_;
    bool is_closed_ = false;

public:
    explicit WssSession(tcp::socket &&socket, ssl::context &ctx, INetworkObserver *obs)
        : ws_(std::move(socket), ctx), observer_(obs)
    {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    }

    explicit WssSession(net::io_context &ioc, ssl::context &ctx, INetworkObserver *obs)
        : ws_(net::make_strand(ioc), ctx), observer_(obs)
    {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    }

    ~WssSession()
    {
    }

    void run_accept()
    {
        try
        {
            remote_endpoint_str_ = ws_.next_layer().next_layer().socket().remote_endpoint().address().to_string();
        }
        catch (...)
        {
            remote_endpoint_str_ = "Unknown";
        }

        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
        ws_.next_layer().async_handshake(ssl::stream_base::server,
                                         beast::bind_front_handler(&WssSession::on_ssl_handshake, shared_from_this(), true));
    }

    void run_client(const std::string &host, const std::string &port)
    {
        remote_endpoint_str_ = host;
        auto resolver = std::make_shared<tcp::resolver>(ws_.get_executor());
        resolver->async_resolve(host, port,
                                [self = shared_from_this(), resolver](beast::error_code ec, tcp::resolver::results_type results)
                                {
                                    if (ec)
                                        return self->fail(ec, "resolve");
                                    beast::get_lowest_layer(self->ws_).expires_after(std::chrono::seconds(30));
                                    beast::get_lowest_layer(self->ws_).async_connect(results,
                                                                                     beast::bind_front_handler(&WssSession::on_connect, self));
                                });
    }

    void send(const std::string &msg)
    {
        net::post(ws_.get_executor(), beast::bind_front_handler(&WssSession::on_send, shared_from_this(), msg));
    }

    std::string getRemoteEndpoint() const { return remote_endpoint_str_; }
    void close()
    {
        is_closed_ = true;
    }

private:
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
    {
        if (ec)
            return fail(ec, "connect");

        beast::get_lowest_layer(ws_).expires_never();

        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), remote_endpoint_str_.c_str()))
        {
            ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
            return fail(ec, "ssl_sni");
        }
        ws_.next_layer().async_handshake(ssl::stream_base::client,
                                         beast::bind_front_handler(&WssSession::on_ssl_handshake, shared_from_this(), false));
    }

    void on_ssl_handshake(bool is_server, beast::error_code ec)
    {
        if (ec)
            return fail(ec, "ssl_handshake");
        beast::get_lowest_layer(ws_).expires_never();

        ws_.set_option(websocket::stream_base::decorator([](websocket::request_type &req)
                                                         { req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " P2P-Chat"); }));

        if (is_server)
        {
            ws_.async_accept(beast::bind_front_handler(&WssSession::on_handshake_complete, shared_from_this(), true));
        }
        else
        {
            ws_.async_handshake(remote_endpoint_str_, "/",
                                beast::bind_front_handler(&WssSession::on_handshake_complete, shared_from_this(), false));
        }
    }

    void on_handshake_complete(bool is_server, beast::error_code ec)
    {
        if (ec)
            return fail(ec, "ws_handshake");
        if (observer_)
            observer_->onConnectionEstablished(remote_endpoint_str_, is_server);
        do_read();
    }

    void do_read()
    {
        ws_.async_read(buffer_, beast::bind_front_handler(&WssSession::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec == websocket::error::closed)
        {
            if (observer_)
                observer_->onDisconnect(remote_endpoint_str_);
            return;
        }
        if (ec)
            return fail(ec, "read");

        std::string msg = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        if (observer_ && !is_closed_)
            observer_->onMessageReceived(msg);
        do_read();
    }

    void on_send(std::string msg)
    {
        write_queue_.push_back(msg);
        if (write_queue_.size() == 1)
            do_write();
    }

    void do_write()
    {
        ws_.async_write(net::buffer(write_queue_.front()),
                        beast::bind_front_handler(&WssSession::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec)
            return fail(ec, "write");
        write_queue_.pop_front();
        if (!write_queue_.empty())
            do_write();
    }

    void fail(beast::error_code ec, char const *what)
    {
        if (observer_ && !is_closed_)
            observer_->onDisconnect(remote_endpoint_str_);
    }
};

class PeerConnector : public std::enable_shared_from_this<PeerConnector>
{
    net::io_context &ioc_;
    ssl::context &ctx_;
    NetworkEngine *engine_;
    std::string ip_;
    int port_;
    std::shared_ptr<net::steady_timer> timer_;
    bool active_ = true;

public:
    PeerConnector(net::io_context &ioc, ssl::context &ctx, NetworkEngine *eng, std::string ip, int port)
        : ioc_(ioc), ctx_(ctx), engine_(eng), ip_(ip), port_(port)
    {
        timer_ = std::make_shared<net::steady_timer>(ioc);
    }

    void start()
    {
        attempt_connect();
    }

    void stop()
    {
        active_ = false;
        timer_->cancel();
    }

    void on_session_disconnected()
    {
        if (!active_)
            return;

        timer_->expires_after(std::chrono::seconds(3));
        timer_->async_wait([self = shared_from_this()](beast::error_code ec)
                           {
            if (!ec) self->attempt_connect(); });
    }

private:
    void attempt_connect()
    {
        if (!active_)
            return;

        engine_->connectToPeer(ip_, port_);
    }
};

struct NetworkEngine::Impl
{
    net::io_context ioc_;
    ssl::context ctx_;
    std::shared_ptr<tcp::acceptor> acceptor_;
    net::executor_work_guard<net::io_context::executor_type> work_guard_;
    std::thread io_thread_;

    std::vector<std::shared_ptr<WssSession>> sessions_;
    std::mutex session_mutex_;

    std::vector<std::shared_ptr<PeerConnector>> connectors_;
    std::mutex connector_mutex_;

    std::mutex cb_mutex_;
    std::function<void(std::string)> ui_msg_callback_;
    std::function<void(std::string, long long)> ui_ping_callback_;

    INetworkObserver *owner_;

    Impl(INetworkObserver *owner)
        : ctx_(ssl::context::tlsv12),
          work_guard_(net::make_work_guard(ioc_)),
          owner_(owner)
    {
        CertHelper::load_self_signed_cert(ctx_);
        ctx_.set_verify_mode(ssl::verify_none);
    }

    void do_accept()
    {
        //super good refaxtor :)
        acceptor_->async_accept(net::make_strand(ioc_),
                                [this](beast::error_code ec, tcp::socket socket)
                                {
                                    if (!ec)
                                    {
                                        auto session = std::make_shared<WssSession>(std::move(socket), ctx_, owner_);
                                        {
                                            std::lock_guard<std::mutex> lock(session_mutex_);
                                            sessions_.push_back(session);
                                        }
                                        session->run_accept();
                                    }
                                    do_accept();
                                });
    }
};

NetworkEngine::NetworkEngine() : m_impl(std::make_unique<Impl>(this)) {}
NetworkEngine::~NetworkEngine() { stop(); }

void NetworkEngine::start()
{
    if (!m_impl->io_thread_.joinable())
    {
        m_impl->io_thread_ = std::thread([this]
                                         { m_impl->ioc_.run(); });
    }
}

void NetworkEngine::stop()
{

    {
        std::lock_guard<std::mutex> lock(m_impl->connector_mutex_);
        for (auto &c : m_impl->connectors_)
            c->stop();
        m_impl->connectors_.clear();
    }

    m_impl->ioc_.stop();
    if (m_impl->io_thread_.joinable())
        m_impl->io_thread_.join();

    std::lock_guard<std::mutex> lock(m_impl->session_mutex_);
    m_impl->sessions_.clear();
}

void NetworkEngine::clearCallbacks()
{
    std::lock_guard<std::mutex> lock(m_impl->cb_mutex_);
    m_impl->ui_msg_callback_ = nullptr;
    m_impl->ui_ping_callback_ = nullptr;
}

void NetworkEngine::setUiCallback(std::function<void(std::string)> cb)
{
    std::lock_guard<std::mutex> lock(m_impl->cb_mutex_);
    m_impl->ui_msg_callback_ = cb;
}
void NetworkEngine::setPingCallback(std::function<void(std::string, long long)> cb)
{
    std::lock_guard<std::mutex> lock(m_impl->cb_mutex_);
    m_impl->ui_ping_callback_ = cb;
}

void NetworkEngine::startListening(int port)
{
    auto endpoint = tcp::endpoint(tcp::v4(), port);
    net::post(m_impl->ioc_, [this, endpoint]()
              {
        beast::error_code ec;
        m_impl->acceptor_ = std::make_shared<tcp::acceptor>(m_impl->ioc_);
        m_impl->acceptor_->open(endpoint.protocol(), ec);
        if (ec) return;
        m_impl->acceptor_->set_option(net::socket_base::reuse_address(true), ec);
        m_impl->acceptor_->bind(endpoint, ec);
        if (ec) return;
        m_impl->acceptor_->listen(net::socket_base::max_connections, ec);
        if (ec) return;
        m_impl->do_accept(); });
}

void NetworkEngine::addPersistentPeer(const std::string &ip, int port)
{

    net::post(m_impl->ioc_, [this, ip, port]()
              {
        auto connector = std::make_shared<PeerConnector>(m_impl->ioc_, m_impl->ctx_, this, ip, port);
        {
            std::lock_guard<std::mutex> lock(m_impl->connector_mutex_);
            m_impl->connectors_.push_back(connector);
        }
        connector->start(); });
}

void NetworkEngine::connectToPeer(const std::string &ip, int port)
{
    net::post(m_impl->ioc_, [this, ip, port]()
              {
        auto session = std::make_shared<WssSession>(m_impl->ioc_, m_impl->ctx_, this);
        {
            std::lock_guard<std::mutex> lock(m_impl->session_mutex_);
            m_impl->sessions_.push_back(session);
        }
        session->run_client(ip, std::to_string(port)); });
}

void NetworkEngine::broadcast(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(m_impl->session_mutex_);

    std::set<std::string> sent_ips;

    for (auto &s : m_impl->sessions_)
    {
        std::string ip = s->getRemoteEndpoint();

        if (sent_ips.count(ip))
        {
            continue;
        }

        s->send(msg);
        sent_ips.insert(ip);
    }
}

void NetworkEngine::onMessageReceived(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(m_impl->cb_mutex_);
    if (m_impl->ui_msg_callback_)
        m_impl->ui_msg_callback_(msg);
}

void NetworkEngine::onConnectionEstablished(const std::string &ip, bool is_incoming)
{
}

void NetworkEngine::onDisconnect(const std::string &ip)
{

    {
        std::lock_guard<std::mutex> lock(m_impl->session_mutex_);
        m_impl->sessions_.erase(
            std::remove_if(m_impl->sessions_.begin(), m_impl->sessions_.end(),
                           [&ip](const std::shared_ptr<WssSession> &s)
                           {
                               return s->getRemoteEndpoint() == ip;
                           }),
            m_impl->sessions_.end());
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->connector_mutex_);
        for (auto &c : m_impl->connectors_)
        {

            c->on_session_disconnected();
        }
    }
}

void NetworkEngine::onPingResult(const std::string &ip, long long ms)
{
    std::lock_guard<std::mutex> lock(m_impl->cb_mutex_);
    if (m_impl->ui_ping_callback_)
        m_impl->ui_ping_callback_(ip, ms);
}