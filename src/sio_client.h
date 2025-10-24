//
//  sio_client.h
//
//  Created by Melo Yao on 3/25/15.
//

#ifndef SIO_CLIENT_H
#define SIO_CLIENT_H
#include <string>
#include <functional>
#include <memory>
#include "sio_message.h"
#include "sio_socket.h"

namespace asio
{
    class io_context;
}

namespace sio
{
    class client_impl;

    struct client_options
    {
        asio::io_context *io_context = nullptr;
    };

    struct reconnect_config
    {
        unsigned attempts = static_cast<unsigned>(-1); // Infinite by default
        unsigned delay = 5000;                       // 5 seconds
        unsigned delay_max = 25000;                  // 25 seconds
        bool enabled = true;

        reconnect_config() = default;

        // Convenience constructor
        reconnect_config(unsigned attempts_, unsigned delay_, unsigned delay_max_)
            : attempts(attempts_), delay(delay_), delay_max(delay_max_), enabled(true) {}

        // Disable reconnection
        static reconnect_config disabled()
        {
            reconnect_config config;
            config.enabled = false;
            return config;
        }
    };

    class client
    {
    public:
        enum class disconnect_reason
        {
            client_disconnect,      // User called close()
            server_disconnect,      // Server sent disconnect packet
            transport_close,        // WebSocket closed cleanly
            transport_error,        // WebSocket error
            ping_timeout,          // Server stopped responding to pings
            namespace_disconnect,   // Namespace-specific disconnect
            max_reconnect_attempts  // Reconnection attempts exhausted
        };

        enum class connection_error
        {
            timeout,                  // Connection timeout
            network_failure,          // Network unreachable, DNS failure
            protocol_error,           // Invalid socket.io protocol
            authentication_failed,    // Auth rejected by server
            transport_open_failed,   // WebSocket handshake failed
            ssl_error,                // TLS/SSL error (Release builds)
            unknown                   // Unknown error
        };

        enum class connection_state
        {
            disconnected,
            connecting,
            connected,
            reconnecting,
            closing
        };

        typedef std::function<void(void)> con_listener;

        typedef std::function<void(disconnect_reason reason)> close_listener;

        typedef std::function<void(connection_error error)> fail_listener;

        typedef std::function<void(unsigned, unsigned)> reconnect_listener;

        typedef std::function<void(connection_state)> state_listener;

        typedef std::function<void(std::string const &nsp)> socket_listener;

        client();
        client(client_options const &options);
        ~client();

        // set listeners and event bindings.
        void set_open_listener(con_listener const &l);

        void set_fail_listener(fail_listener const &l);

        void set_reconnecting_listener(con_listener const &l);

        void set_reconnect_listener(reconnect_listener const &l);

        void set_close_listener(close_listener const &l);

        void set_socket_open_listener(socket_listener const &l);

        void set_socket_close_listener(socket_listener const &l);

        void set_state_listener(state_listener const &l);

        void clear_con_listeners();

        void clear_socket_listeners();

        // Client Functions - such as send, etc.
        void connect(const std::string &uri);

        void connect(const std::string &uri, const message::ptr &auth);

        void connect(const std::string &uri, const std::map<std::string, std::string> &query);

        void connect(const std::string &uri, const std::map<std::string, std::string> &query, const message::ptr &auth);

        void connect(const std::string &uri, const std::map<std::string, std::string> &query,
                     const std::map<std::string, std::string> &http_extra_headers);

        void connect(const std::string &uri, const std::map<std::string, std::string> &query,
                     const std::map<std::string, std::string> &http_extra_headers, const message::ptr &auth);

        void set_reconnect_attempts(int attempts);

        void set_reconnect_delay(unsigned millis);

        void set_reconnect_delay_max(unsigned millis);

        // Convenient method to configure all reconnection parameters at once
        void set_reconnect_config(const reconnect_config &config);

        void set_logs_default();

        void set_logs_quiet();

        void set_logs_verbose();

        sio::socket::ptr const &socket(const std::string &nsp = "");

        // Closes the connection
        void close();

        void sync_close();

        void set_proxy_basic_auth(const std::string &uri, const std::string &username, const std::string &password);

        bool opened() const;

        connection_state get_connection_state() const;

        std::string const &get_sessionid() const;

    private:
        // disable copy constructor and assign operator.
        client(client const &) {}
        void operator=(client const &) {}

        struct impl_deleter
        {
            void operator()(client_impl *p) const;
        };
        std::unique_ptr<client_impl, impl_deleter> m_impl;
    };

}

#endif // __SIO_CLIENT__H__
