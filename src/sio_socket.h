#ifndef SIO_SOCKET_H
#define SIO_SOCKET_H
#include "sio_message.h"
#include "sio_awaitable.h"
#include <functional>
#include <memory>
#include <chrono>
namespace sio
{
    class event_adapter;

    struct connection_metrics
    {
        // Traffic counters
        size_t packets_sent;
        size_t packets_received;

        // Connection health
        size_t reconnection_count;
        std::chrono::milliseconds last_ping_latency;

        // Session info
        std::chrono::system_clock::time_point connected_at;
    };

    class event
    {
    public:
        const std::string &get_nsp() const;

        const std::string &get_name() const;

        const message::ptr &get_message() const;

        const message::list &get_messages() const;

        bool need_ack() const;

        void put_ack_message(message::list const &ack_message);

        message::list const &get_ack_message() const;

    protected:
        event(std::string const &nsp, std::string const &name, message::list const &messages, bool need_ack);
        event(std::string const &nsp, std::string const &name, message::list &&messages, bool need_ack);

        message::list &get_ack_message_impl();

    private:
        const std::string m_nsp;
        const std::string m_name;
        const message::list m_messages;
        const bool m_need_ack;
        message::list m_ack_message;

        friend class event_adapter;
    };

    class client_impl;
    class packet;

    // The name 'socket' is taken from concept of official socket.io.
    class socket
    {
    public:
        typedef std::function<void(const std::string &name, message::ptr const &message, bool need_ack, message::list &ack_message)> event_listener_aux;

        typedef std::function<void(event &event)> event_listener;

        // Backward-compatible simple handler (returns bool to indicate success)
        typedef std::function<bool(message::ptr const &message)> simple_event_handler;

        // New handler signature that provides ack_message to populate
        typedef std::function<void(message::ptr const &message, message::list &ack_message)> event_handler_with_ack;

        typedef std::function<void(message::ptr const &message)> error_listener;

        typedef std::shared_ptr<socket> ptr;

        ~socket();

        void on(std::string const &event_name, event_listener const &func);

        void on(std::string const &event_name, event_listener_aux const &func);

        // New API: handler can fill ack payload directly
        void on_with_ack(std::string const &event_name, event_handler_with_ack const &handler);

        // Old API: handler returns success boolean; we translate to ack [true/false]
        void on_with_ack(std::string const &event_name, simple_event_handler const &handler);

        void off(std::string const &event_name);

        void on_any(event_listener const &func);

        void on_any(event_listener_aux const &func);

        void off_all();

        void close();

        void on_error(error_listener const &l);

        void off_error();

        void emit(std::string const &event_name, message::list const &msglist = nullptr);

        void emit_with_ack(std::string const &event_name, message::list const &msglist, std::function<void(message::list const &)> const &ack);

        void emit_with_ack(std::string const &event_name,
                          message::list const &msglist,
                          std::function<void(message::list const &)> const &ack,
                          unsigned timeout_ms,
                          std::function<void()> const &timeout_callback);

        // C++20 Coroutine support - async emit with co_await
        emit_task emit_async(std::string const &event_name,
                            message::list const &msglist = nullptr);

        emit_task emit_async(std::string const &event_name,
                            message::list const &msglist,
                            unsigned timeout_ms);

        std::string const &get_namespace() const;

        connection_metrics get_metrics() const;

    protected:
        socket(client_impl *, std::string const &, message::ptr const &);

        void on_connected();

        void on_close();

        void on_open();

        void on_disconnect();

        void on_message_packet(packet const &p);

        friend class client_impl;

    private:
        // disable copy constructor and assign operator.
        socket(socket const &) {}
        void operator=(socket const &) {}

        class impl;
        struct impl_deleter
        {
            void operator()(impl *p) const;
        };
        std::unique_ptr<impl, impl_deleter> m_impl;
    };
}
#endif // SIO_SOCKET_H
