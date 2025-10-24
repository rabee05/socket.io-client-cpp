//
//  sio_client_impl.cpp
//  SioChatDemo
//
//  Created by Melo Yao on 4/3/15.
//  Copyright (c) 2015 Melo Yao. All rights reserved.
//

#include "sio_client_impl.h"
#include <functional>
#include <sstream>
#include <chrono>
#include <mutex>
#include <cmath>
#if (DEBUG || _DEBUG) && !defined(SIO_DISABLE_LOGGING)
#include <iostream>
#endif
#include <iomanip>
// Comment this out to disable handshake logging to stdout
#if (DEBUG || _DEBUG) && !defined(SIO_DISABLE_LOGGING)
#define LOG(x) std::cout << x
#else
#define LOG(x)
#endif

#if SIO_TLS
// If using Asio's SSL support, you will also need to add this #include.
// Source: http://think-async.com/Asio/asio-1.10.6/doc/asio/using.html
// #include <asio/ssl/impl/src.hpp>
#endif

using std::chrono::milliseconds;
using namespace std;

namespace sio
{
    /*************************public:*************************/
    client_impl::client_impl(client_options const &options) : m_ping_interval(0),
                                                              m_ping_timeout(0),
                                                              m_network_thread(),
                                                              m_con_state(con_closed),
                                                              m_reconn_delay(5000),
                                                              m_reconn_delay_max(25000),
                                                              m_reconn_attempts(static_cast<unsigned>(-1)),
                                                              m_reconn_made(0)
    {
        using websocketpp::log::alevel;
#ifndef DEBUG
        m_client.clear_access_channels(alevel::all);
        m_client.set_access_channels(alevel::connect | alevel::disconnect | alevel::app);
#endif
        // Initialize the Asio transport policy
        if (options.io_context != nullptr)
        {
            m_client.init_asio(options.io_context);
        }
        else
        {
            m_client.init_asio();
        }

        // Set up event handlers using modern lambdas
        m_client.set_open_handler([this](auto hdl) { on_open(hdl); });
        m_client.set_close_handler([this](auto hdl) { on_close(hdl); });
        m_client.set_fail_handler([this](auto hdl) { on_fail(hdl); });
        m_client.set_message_handler([this](auto hdl, auto msg) { on_message(hdl, msg); });
#if SIO_TLS
        m_client.set_tls_init_handler([this](auto hdl) { return on_tls_init(hdl); });
#endif
        m_packet_mgr.set_decode_callback([this](auto const& p) { on_decode(p); });
        m_packet_mgr.set_encode_callback([this](auto const& p1, auto const& p2) { on_encode(p1, p2); });
    }

    client_impl::~client_impl()
    {
        this->sockets_invoke_void(&sio::socket::on_close);
        sync_close();
    }

    void client_impl::set_proxy_basic_auth(const std::string &uri, const std::string &username, const std::string &password)
    {
        m_proxy_base_url = uri;
        m_proxy_basic_username = username;
        m_proxy_basic_password = password;
    }

    void client_impl::connect(const string &uri, const map<string, string> &query, const map<string, string> &headers, const message::ptr &auth)
    {
        if (m_reconn_timer)
        {
            m_reconn_timer->cancel();
            m_reconn_timer.reset();
        }
        if (m_network_thread)
        {
            con_state state = m_con_state.load(std::memory_order_acquire);
            if (state == con_closing || state == con_closed)
            {
                // if client is closing, join to wait.
                // if client is closed, still need to join,
                // but in closed case,join will return immediately.
                m_network_thread->join();
                m_network_thread.reset(); // defensive
            }
            else
            {
                // if we are connected, do nothing.
                return;
            }
        }
        m_con_state.store(con_opening, std::memory_order_release);
        notify_state_change(client::connection_state::connecting);
        m_base_url = uri;
        m_reconn_made = 0;

        string query_str;
        // Pre-reserve to minimize reallocations (approximate)
        size_t approx = 0;
        for (auto &kv : query)
            approx += kv.first.size() + kv.second.size() + 2;
        query_str.reserve(approx + 8);
        for (map<string, string>::const_iterator it = query.begin(); it != query.end(); ++it)
        {
            query_str.append("&");
            query_str.append(it->first);
            query_str.append("=");
            string query_str_value = encode_query_string(it->second);
            query_str.append(query_str_value);
        }
        m_query_string = move(query_str);

        m_http_headers = headers;
        m_auth = auth;

        this->reset_states();
        m_abort_retries = false;
        asio::dispatch(m_client.get_io_context(), [this, uri, query_str = m_query_string]() { connect_impl(uri, query_str); });
        m_network_thread.reset(new thread([this]() { run_loop(); }));
    }

    socket::ptr const &client_impl::socket(string const &nsp)
    {
        lock_guard<mutex> guard(m_socket_mutex);
        string aux;
        if (nsp == "")
        {
            aux = "/";
        }
        else if (nsp[0] != '/')
        {
            aux.append("/", 1);
            aux.append(nsp);
        }
        else
        {
            aux = nsp;
        }

        auto it = m_sockets.find(aux);
        if (it != m_sockets.end())
        {
            LOG("socket() - Returning existing socket for namespace: " << aux << endl);
            return it->second;
        }
        else
        {
            LOG("socket() - Creating NEW socket for namespace: " << aux << endl);
            pair<const string, socket::ptr> p(aux, shared_ptr<sio::socket>(new sio::socket(this, aux, m_auth)));
            return (m_sockets.insert(p).first)->second;
        }
    }

    void client_impl::close()
    {
        m_con_state.store(con_closing, std::memory_order_release);
        notify_state_change(client::connection_state::closing);
        m_abort_retries = true;
        this->sockets_invoke_void(&sio::socket::close);
        asio::dispatch(m_client.get_io_context(), [this]() { close_impl(close::status::normal, "End by user"); });
    }

    void client_impl::sync_close()
    {
        m_con_state.store(con_closing, std::memory_order_release);
        m_abort_retries = true;
        this->sockets_invoke_void(&sio::socket::close);
        asio::dispatch(m_client.get_io_context(), [this]() { close_impl(close::status::normal, "End by user"); });
        if (m_network_thread)
        {
            m_network_thread->join();
            m_network_thread.reset();
        }
    }

    void client_impl::set_logs_default()
    {
        m_client.clear_access_channels(websocketpp::log::alevel::all);
        m_client.set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect | websocketpp::log::alevel::app);
    }

    void client_impl::set_logs_quiet()
    {
        m_client.clear_access_channels(websocketpp::log::alevel::all);
        m_client.clear_error_channels(websocketpp::log::elevel::all);
    }

    void client_impl::set_logs_verbose()
    {
        m_client.set_access_channels(websocketpp::log::alevel::all);
    }

    /*************************protected:*************************/
    void client_impl::send(packet &p)
    {
        m_packet_mgr.encode(p);
    }

    void client_impl::remove_socket(string const &nsp)
    {
        lock_guard<mutex> guard(m_socket_mutex);
        auto it = m_sockets.find(nsp);
        if (it != m_sockets.end())
        {
            m_sockets.erase(it);
        }
    }

    asio::io_context &client_impl::get_io_service()
    {
        return m_client.get_io_context();
    }

    void client_impl::on_socket_closed(string const &nsp)
    {
        client::socket_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_socket_close_listener;
        }
        if (listener)
            listener(nsp);
    }

    void client_impl::on_socket_opened(string const &nsp)
    {
        client::socket_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_socket_open_listener;
        }
        if (listener)
            listener(nsp);
    }

    void client_impl::notify_state_change(client::connection_state state)
    {
        client::state_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_state_listener;
        }
        if (listener)
            listener(state);
    }

    client::connection_state client_impl::get_connection_state() const
    {
        con_state state = m_con_state.load(std::memory_order_acquire);
        switch (state)
        {
        case con_opening:
            return client::connection_state::connecting;
        case con_opened:
            return client::connection_state::connected;
        case con_closing:
            return client::connection_state::closing;
        case con_closed:
        default:
            return client::connection_state::disconnected;
        }
    }

    /*************************private:*************************/
    void client_impl::run_loop()
    {

        m_client.run();
        m_client.reset();
        m_client.get_alog().write(websocketpp::log::alevel::devel,
                                  "run loop end");
    }

    void client_impl::connect_impl(const string &uri, const string &queryString)
    {
        do
        {
            websocketpp::uri uo(uri);
            ostringstream ss;
#if SIO_TLS
            // TLS build (Release): HTTPS/WSS only
            ss << "wss://";
#else
            // Non-TLS build (Debug): HTTP/WS only
            ss << "ws://";
#endif
            const std::string host(uo.get_host());
            // As per RFC2732, literal IPv6 address should be enclosed in "[" and "]".
            if (host.find(':') != std::string::npos)
            {
                ss << "[" << uo.get_host() << "]";
            }
            else
            {
                ss << uo.get_host();
            }

            // If a resource path was included in the URI, use that, otherwise
            // use the default /socket.io/.
            const std::string path(uo.get_resource() == "/" ? "/socket.io/" : uo.get_resource());

            ss << ":" << uo.get_port() << path << "?EIO=4&transport=websocket";
            if (m_sid.size() > 0)
            {
                ss << "&sid=" << m_sid;
            }
            ss << "&t=" << time(NULL) << queryString;
            lib::error_code ec;
            client_type::connection_ptr con = m_client.get_connection(ss.str(), ec);
            if (ec)
            {
                m_client.get_alog().write(websocketpp::log::alevel::app,
                                          "Get Connection Error: " + ec.message());
                break;
            }

            for (auto &&header : m_http_headers)
            {
                con->replace_header(header.first, header.second);
            }

            if (!m_proxy_base_url.empty())
            {
                con->set_proxy(m_proxy_base_url, ec);
                if (ec)
                {
                    m_client.get_alog().write(websocketpp::log::alevel::app,
                                              "Set Proxy Error: " + ec.message());
                    break;
                }
                if (!m_proxy_basic_username.empty())
                {
                    con->set_proxy_basic_auth(m_proxy_basic_username, m_proxy_basic_password, ec);
                    if (ec)
                    {
                        m_client.get_alog().write(websocketpp::log::alevel::app,
                                                  "Set Proxy Basic Auth Error: " + ec.message());
                        break;
                    }
                }
            }

            m_client.connect(con);
            return;
        } while (0);
        client::fail_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_fail_listener;
        }
        if (listener)
        {
            listener(client::connection_error::network_failure);
        }
    }

    void client_impl::close_impl(close::status::value const &code, string const &reason)
    {
        LOG("Close by reason:" << reason << endl);
        if (m_reconn_timer)
        {
            m_reconn_timer->cancel();
            m_reconn_timer.reset();
        }
        if (m_con.expired())
        {
            cerr << "Error: No active session" << endl;
        }
        else
        {
            lib::error_code ec;
            m_client.close(m_con, code, reason, ec);
        }
    }

    void client_impl::send_impl(shared_ptr<const string> const &payload_ptr, frame::opcode::value opcode)
    {
        if (m_con_state.load(std::memory_order_acquire) == con_opened)
        {
            lib::error_code ec;
            m_client.send(m_con, *payload_ptr, opcode, ec);
            if (ec)
            {
                cerr << "Send failed,reason:" << ec.message() << endl;
            }
        }
    }

    void client_impl::timeout_ping(const asio::error_code &ec)
    {
        if (ec)
        {
            return;
        }
        LOG("Ping timeout" << endl);
        // Mark that the next disconnect is due to ping timeout
        m_pending_disconnect_reason.store(client::disconnect_reason::ping_timeout, std::memory_order_release);
        m_has_pending_reason.store(true, std::memory_order_release);
        asio::dispatch(m_client.get_io_context(), [this]() { close_impl(close::status::policy_violation, "Ping timeout"); });
    }

    void client_impl::timeout_reconnect(asio::error_code const &ec)
    {
        if (ec)
        {
            return;
        }
        if (m_con_state.load(std::memory_order_acquire) == con_closed)
        {
            m_con_state.store(con_opening, std::memory_order_release);
            notify_state_change(client::connection_state::reconnecting);
            m_reconn_made++;
            this->reset_states();
            LOG("Reconnecting..." << endl);
            client::con_listener listener;
            {
                std::lock_guard<std::mutex> lock(m_listener_mutex);
                listener = m_reconnecting_listener;
            }
            if (listener)
                listener();
            asio::dispatch(m_client.get_io_context(), [this, url = m_base_url, query = m_query_string]() { connect_impl(url, query); });
        }
    }

    unsigned client_impl::next_delay() const
    {
        // Exponential backoff with jitter-free saturation to m_reconn_delay_max
        unsigned delay = m_reconn_delay;
        unsigned attempts = m_reconn_made;
        while (attempts-- > 0 && delay < m_reconn_delay_max)
        {
            if (delay > m_reconn_delay_max / 2)
            { // next doubling would exceed max
                delay = m_reconn_delay_max;
                break;
            }
            delay *= 2;
        }
        LOG("next_delay: attempt=" << m_reconn_made
                                   << ", base_delay=" << m_reconn_delay
                                   << ", max_delay=" << m_reconn_delay_max
                                   << ", next_delay=" << delay << endl);
        return delay;
    }

    socket::ptr client_impl::get_socket_locked(string const &nsp)
    {
        lock_guard<mutex> guard(m_socket_mutex);
        auto it = m_sockets.find(nsp);
        if (it != m_sockets.end())
        {
            return it->second;
        }
        else
        {
            return socket::ptr();
        }
    }

    void client_impl::sockets_invoke_void(void (sio::socket::*fn)(void))
    {
        std::vector<socket::ptr> sockets;
        {
            std::lock_guard<std::mutex> guard(m_socket_mutex);
            sockets.reserve(m_sockets.size());
            for (auto &kv : m_sockets)
                sockets.emplace_back(kv.second);
        }
        for (auto &s : sockets)
        {
            ((*s).*fn)();
        }
    }

    void client_impl::on_fail(connection_hdl)
    {
        if (m_con_state.load(std::memory_order_acquire) == con_closing)
        {
            LOG("Connection failed while closing." << endl);
            this->close();
            return;
        }

        m_con.reset();
        m_con_state.store(con_closed, std::memory_order_release);
        notify_state_change(client::connection_state::disconnected);
        this->sockets_invoke_void(&sio::socket::on_disconnect);
        LOG("Connection failed." << endl);
        if (m_reconn_made < m_reconn_attempts && !m_abort_retries)
        {
            LOG("Reconnect for attempt:" << m_reconn_made << endl);
            unsigned delay = this->next_delay();
            client::reconnect_listener listener;
            {
                std::lock_guard<std::mutex> lock(m_listener_mutex);
                listener = m_reconnect_listener;
            }
            if (listener)
                listener(m_reconn_made, delay);
            m_reconn_timer.reset(new asio::steady_timer(m_client.get_io_context()));
            m_reconn_timer->expires_after(milliseconds(delay));
            m_reconn_timer->async_wait([this](auto const& ec) { timeout_reconnect(ec); });
        }
        else
        {
            client::fail_listener listener;
            {
                std::lock_guard<std::mutex> lock(m_listener_mutex);
                listener = m_fail_listener;
            }
            if (listener)
                listener(client::connection_error::timeout);
        }
    }

    void client_impl::on_open(connection_hdl con)
    {
        if (m_con_state.load(std::memory_order_acquire) == con_closing)
        {
            LOG("Connection opened while closing." << endl);
            this->close();
            return;
        }

        LOG("Connected." << endl);
        m_con_state.store(con_opened, std::memory_order_release);
        notify_state_change(client::connection_state::connected);
        m_con = con;
        // Do not reset reconnection counter here; reset it only after a successful handshake.
        this->sockets_invoke_void(&sio::socket::on_open);
        // Don't auto-create root namespace socket - let user explicitly create namespaces
        // this->socket("");
        client::con_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_open_listener;
        }
        if (listener)
            listener();
    }

    void client_impl::on_close(connection_hdl con)
    {
        LOG("Client Disconnected." << endl);
        con_state m_con_state_was = m_con_state.load(std::memory_order_acquire);
        m_con_state.store(con_closed, std::memory_order_release);
        notify_state_change(client::connection_state::disconnected);
        lib::error_code ec;
        close::status::value code = close::status::normal;
        client_type::connection_ptr conn_ptr = m_client.get_con_from_hdl(con, ec);
        if (ec)
        {
            LOG("OnClose get conn failed" << ec << endl);
        }
        else
        {
            code = conn_ptr->get_local_close_code();
        }

        m_con.reset();
        this->clear_timers();
        client::disconnect_reason reason;

        // Check if a specific disconnect reason was set (e.g., ping_timeout)
        if (m_has_pending_reason.load(std::memory_order_acquire))
        {
            reason = m_pending_disconnect_reason.load(std::memory_order_acquire);
            m_has_pending_reason.store(false, std::memory_order_release);
        }
        // Determine disconnect reason based on close code and state
        else if (m_con_state_was == con_closing || m_abort_retries)
        {
            // User initiated close - don't reconnect
            reason = client::disconnect_reason::client_disconnect;
        }
        else if (code == close::status::normal || code == close::status::going_away)
        {
            // Server closed cleanly
            reason = client::disconnect_reason::server_disconnect;
        }
        else
        {
            // Abnormal close (network error, protocol error, etc.)
            reason = client::disconnect_reason::transport_error;
        }

        this->sockets_invoke_void(&sio::socket::on_disconnect);

        if (m_con_state_was == con_closing || m_abort_retries)
        {
            // Don't reconnect - user initiated or aborted
        }
        else if (m_reconn_made < m_reconn_attempts)
        {
            // Connection dropped (or server closed) - try to reconnect
            LOG("Reconnect for attempt:" << m_reconn_made << endl);
            unsigned delay = this->next_delay();
            client::reconnect_listener listener;
            {
                std::lock_guard<std::mutex> lock(m_listener_mutex);
                listener = m_reconnect_listener;
            }
            if (listener)
                listener(m_reconn_made, delay);
            m_reconn_timer.reset(new asio::steady_timer(m_client.get_io_context()));
            m_reconn_timer->expires_after(milliseconds(delay));
            m_reconn_timer->async_wait([this](auto const& ec) { timeout_reconnect(ec); });
            return;
        }
        else
        {
            // Max reconnect attempts reached
            reason = client::disconnect_reason::max_reconnect_attempts;
        }

        client::close_listener listener;
        {
            std::lock_guard<std::mutex> lock(m_listener_mutex);
            listener = m_close_listener;
        }
        if (listener)
        {
            listener(reason);
        }
    }

    void client_impl::on_message(connection_hdl, client_type::message_ptr msg)
    {
        // Parse the incoming message according to socket.IO rules
        m_packet_mgr.put_payload(msg->get_payload());
    }

    void client_impl::on_handshake(message::ptr const &message)
    {
        if (message && message->get_flag() == message::flag_object)
        {
            const object_message *obj_ptr = static_cast<object_message *>(message.get());
            const map<string, message::ptr> *values = &(obj_ptr->get_map());
            auto it = values->find("sid");
            if (it != values->end())
            {
                m_sid = static_pointer_cast<string_message>(it->second)->get_string();
            }
            else
            {
                goto failed;
            }
            it = values->find("pingInterval");
            if (it != values->end() && it->second->get_flag() == message::flag_integer)
            {
                m_ping_interval = (unsigned)static_pointer_cast<int_message>(it->second)->get_int();
            }
            else
            {
                m_ping_interval = 25000;
            }
            it = values->find("pingTimeout");

            if (it != values->end() && it->second->get_flag() == message::flag_integer)
            {
                m_ping_timeout = (unsigned)static_pointer_cast<int_message>(it->second)->get_int();
            }
            else
            {
                m_ping_timeout = 60000;
            }

            // Start ping timeout
            update_ping_timeout_timer();

            // Reset reconnection counter after successful handshake completes.
            m_reconn_made = 0;
            LOG("Handshake successful, reconnection counter reset" << endl);

            return;
        }
    failed:
        // just close it.
        asio::dispatch(m_client.get_io_context(), [this]() { close_impl(close::status::policy_violation, "Handshake error"); });
    }

    void client_impl::on_ping()
    {
        auto ping_received = std::chrono::steady_clock::now();

        // Reply with pong packet.
        packet p(packet::frame_pong);
        m_packet_mgr.encode(p, [&](bool /*isBin*/, shared_ptr<const string> payload)
                            { this->m_client.send(this->m_con, *payload, frame::opcode::text); });

        // Calculate round-trip latency if we have a previous ping timestamp
        if (m_last_ping_sent.time_since_epoch().count() > 0)
        {
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(ping_received - m_last_ping_sent);
            m_last_ping_latency_ms.store(latency.count(), std::memory_order_relaxed);
        }

        // Store this ping time for next calculation
        m_last_ping_sent = ping_received;

        // Reset the ping timeout.
        update_ping_timeout_timer();
    }

    void client_impl::on_decode(packet const &p)
    {
        switch (p.get_frame())
        {
        case packet::frame_message:
        {
            // Special event for sid sync
            if (p.get_type() == packet::type_connect)
            {
                auto message = p.get_message();
                if (message && message->get_flag() == message::flag_object)
                {
                    const object_message *obj_ptr = static_cast<object_message *>(message.get());
                    const std::map<std::string, message::ptr> *values = &(obj_ptr->get_map());
                    auto it = values->find("sid");
                    if (it != values->end())
                    {
                        m_sid = std::static_pointer_cast<string_message>(it->second)->get_string();
                    }
                }
            }
            socket::ptr so_ptr = get_socket_locked(p.get_nsp());
            if (so_ptr)
                so_ptr->on_message_packet(p);
            break;
        }
        case packet::frame_open:
            this->on_handshake(p.get_message());
            break;
        case packet::frame_close:
            // FIXME how to deal?
            this->close_impl(close::status::abnormal_close, "End by server");
            break;
        case packet::frame_ping:
            this->on_ping();
            break;

        default:
            break;
        }
    }

    void client_impl::on_encode(bool isBinary, shared_ptr<const string> const &payload)
    {
        LOG("encoded payload length:" << payload->length() << endl);
        auto opcode = isBinary ? frame::opcode::binary : frame::opcode::text;
        asio::dispatch(m_client.get_io_context(), [this, payload, opcode]() { send_impl(payload, opcode); });
    }

    void client_impl::clear_timers()
    {
        LOG("clear timers" << endl);
        asio::error_code ec;
        if (m_ping_timeout_timer)
        {
            m_ping_timeout_timer->cancel();
            m_ping_timeout_timer.reset();
        }
    }

    void client_impl::update_ping_timeout_timer()
    {
        if (!m_ping_timeout_timer)
        {
            m_ping_timeout_timer = std::unique_ptr<asio::steady_timer>(new asio::steady_timer(get_io_service()));
        }

        asio::error_code ec;
        m_ping_timeout_timer->expires_after(milliseconds(m_ping_interval + m_ping_timeout));
        m_ping_timeout_timer->async_wait([this](auto const& ec) { timeout_ping(ec); });
    }

    void client_impl::reset_states()
    {
        m_client.reset();
        m_sid.clear();
        m_packet_mgr.reset();
    }

#if SIO_TLS
    client_impl::context_ptr client_impl::on_tls_init(connection_hdl conn)
    {
        context_ptr ctx = context_ptr(new asio::ssl::context(asio::ssl::context::tls));
        asio::error_code ec;
        ctx->set_options(asio::ssl::context::default_workarounds |
                             asio::ssl::context::no_tlsv1 |
                             asio::ssl::context::no_tlsv1_1 |
                             asio::ssl::context::single_dh_use,
                         ec);
        if (ec)
        {
            cerr << "Init tls failed,reason:" << ec.message() << endl;
        }

        return ctx;
    }
#endif

    std::string client_impl::encode_query_string(const std::string &query)
    {
        ostringstream ss;
        ss << std::hex;
        // Percent-encode (RFC3986) non-alphanumeric characters.
        for (const char c : query)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            {
                ss << c;
            }
            else
            {
                ss << '%' << std::uppercase << std::setw(2) << int((unsigned char)c) << std::nouppercase;
            }
        }
        ss << std::dec;
        return ss.str();
    }
}
