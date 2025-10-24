#include "sio_socket.h"
#include "internal/sio_packet.h"
#include "internal/sio_client_impl.h"
#include <asio/steady_timer.hpp>
#include <asio/error_code.hpp>
#include <queue>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <functional>
#include <unordered_map>

#if (DEBUG || _DEBUG) && !defined(SIO_DISABLE_LOGGING)
#define LOG(x) std::cout << x
#else
#define LOG(x)
#endif

#define NULL_GUARD(_x_) \
    if (_x_ == nullptr) \
    return

namespace sio
{
    class event_adapter
    {
    public:
        static void adapt_func(socket::event_listener_aux const &func, event &event)
        {
            func(event.get_name(), event.get_message(), event.need_ack(), event.get_ack_message_impl());
        }

        static inline socket::event_listener do_adapt(socket::event_listener_aux const &func)
        {
            return [func](event &ev) { adapt_func(func, ev); };
        }

        static inline event create_event(std::string const &nsp, std::string const &name, message::list &&message, bool need_ack)
        {
            return event(nsp, name, message, need_ack);
        }
    };

    const std::string &event::get_nsp() const
    {
        return m_nsp;
    }

    const std::string &event::get_name() const
    {
        return m_name;
    }

    const message::ptr &event::get_message() const
    {
        if (m_messages.size() > 0)
            return m_messages[0];
        else
        {
            static message::ptr null_ptr;
            return null_ptr;
        }
    }

    const message::list &event::get_messages() const
    {
        return m_messages;
    }

    bool event::need_ack() const
    {
        return m_need_ack;
    }

    void event::put_ack_message(message::list const &ack_message)
    {
        if (m_need_ack)
            m_ack_message = std::move(ack_message);
    }

    inline event::event(std::string const &nsp, std::string const &name, message::list &&messages, bool need_ack) : m_nsp(nsp),
                                                                                                                    m_name(name),
                                                                                                                    m_messages(std::move(messages)),
                                                                                                                    m_need_ack(need_ack)
    {
    }

    inline event::event(std::string const &nsp, std::string const &name, message::list const &messages, bool need_ack) : m_nsp(nsp),
                                                                                                                         m_name(name),
                                                                                                                         m_messages(messages),
                                                                                                                         m_need_ack(need_ack)
    {
    }

    message::list const &event::get_ack_message() const
    {
        return m_ack_message;
    }

    inline message::list &event::get_ack_message_impl()
    {
        return m_ack_message;
    }

    class socket::impl
    {
    public:
        impl(client_impl *, std::string const &, message::ptr const &);
        ~impl();

        void on(std::string const &event_name, event_listener_aux const &func);

        void on(std::string const &event_name, event_listener const &func);

        void on_any(event_listener_aux const &func);

        void on_any(event_listener const &func);

        void off(std::string const &event_name);

        void off_all();

#define SYNTHESIS_SETTER(__TYPE__, __FIELD__) \
    void set_##__FIELD__(__TYPE__ const &l)   \
    {                                         \
        m_##__FIELD__ = l;                    \
    }

        SYNTHESIS_SETTER(error_listener, error_listener) // socket io errors

#undef SYNTHESIS_SETTER

        void on_error(error_listener const &l);

        void off_error();

        void close();

        void emit(std::string const &event_name, message::list const &msglist, std::function<void(message::list const &)> const &ack);

        void emit(std::string const &event_name, message::list const &msglist, std::function<void(message::list const &)> const &ack, unsigned timeout_ms, std::function<void()> const &timeout_callback);

        std::string const &get_namespace() const { return m_nsp; }

        connection_metrics get_metrics() const;

    protected:
        void on_connected();

        void on_close();

        void on_open();

        void on_message_packet(packet const &packet);

        void on_disconnect();

    private:
        // Message Parsing callbacks.
        void on_socketio_event(const std::string &nsp, int msgId, const std::string &name, message::list &&message);
        void on_socketio_ack(int msgId, message::list const &message);
        void on_socketio_error(message::ptr const &err_message);

        event_listener get_bind_listener_locked(string const &event);

        void ack(int msgId, string const &name, message::list const &ack_message);

        void timeout_connection(const asio::error_code &ec);

        void send_connect();

        void send_packet(packet &p);

        static event_listener s_null_event_listener;

        static std::atomic<unsigned int> s_global_event_id;

        sio::client_impl *m_client;

        std::atomic<bool> m_connected;
        std::string m_nsp;
        message::ptr m_auth;

        std::unordered_map<unsigned int, std::function<void(message::list const &)>> m_acks;

        std::unordered_map<unsigned int, std::shared_ptr<asio::steady_timer>> m_ack_timers;

        std::unordered_map<std::string, event_listener> m_event_binding;

        event_listener m_event_listener;

        error_listener m_error_listener;

        std::unique_ptr<asio::steady_timer> m_connection_timer;

        std::queue<packet> m_packet_queue;

        std::mutex m_event_mutex;

        std::mutex m_packet_mutex;

        // Metrics tracking
        std::atomic<size_t> m_packets_sent{0};
        std::atomic<size_t> m_packets_received{0};
        std::chrono::system_clock::time_point m_connected_at;
        std::mutex m_metrics_mutex;

        friend class socket;
    };

    void socket::impl::on(std::string const &event_name, event_listener_aux const &func)
    {
        this->on(event_name, event_adapter::do_adapt(func));
    }

    void socket::impl::on(std::string const &event_name, event_listener const &func)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_binding[event_name] = func;
    }

    void socket::impl::on_any(event_listener_aux const &func)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_listener = event_adapter::do_adapt(func);
    }

    void socket::impl::on_any(event_listener const &func)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_listener = func;
    }

    void socket::impl::off(std::string const &event_name)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        auto it = m_event_binding.find(event_name);
        if (it != m_event_binding.end())
        {
            m_event_binding.erase(it);
        }
    }

    void socket::impl::off_all()
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        m_event_binding.clear();
    }

    void socket::impl::on_error(error_listener const &l)
    {
        m_error_listener = l;
    }

    void socket::impl::off_error()
    {
        m_error_listener = nullptr;
    }

    socket::impl::impl(client_impl *client, std::string const &nsp, message::ptr const &auth) : m_client(client),
                                                                                                m_connected(false),
                                                                                                m_nsp(nsp),
                                                                                                m_auth(auth)
    {
        NULL_GUARD(client);
        // Only send connect if client is opened
        // This ensures namespace connections happen after the transport is ready
        if (m_client->opened())
        {
            send_connect();
        }
    }

    socket::impl::~impl()
    {
    }

    std::atomic<unsigned int> socket::impl::s_global_event_id{1};

    void socket::impl::emit(std::string const &event_name, message::list const &msglist, std::function<void(message::list const &)> const &ack)
    {
        NULL_GUARD(m_client);
        message::ptr msg_ptr = msglist.to_array_message(event_name);
        int pack_id;
        if (ack)
        {
            pack_id = s_global_event_id.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> guard(m_event_mutex);
            m_acks[pack_id] = ack;
        }
        else
        {
            pack_id = -1;
        }
        packet p(m_nsp, msg_ptr, pack_id);
        send_packet(p);
    }

    void socket::impl::emit(std::string const &event_name,
                            message::list const &msglist,
                            std::function<void(message::list const &)> const &ack,
                            unsigned timeout_ms,
                            std::function<void()> const &timeout_callback)
    {
        NULL_GUARD(m_client);
        message::ptr msg_ptr = msglist.to_array_message(event_name);
        int pack_id;
        if (ack)
        {
            pack_id = s_global_event_id.fetch_add(1, std::memory_order_relaxed);

            // Create timeout timer
            auto timer = std::make_shared<asio::steady_timer>(m_client->get_io_service());
            timer->expires_after(std::chrono::milliseconds(timeout_ms));

            // Capture pack_id for the timeout handler
            timer->async_wait([this, pack_id, timeout_callback](const asio::error_code &ec)
                              {
                if (!ec) // Timer expired (not cancelled)
                {
                    std::function<void(message::list const &)> removed_ack;
                    {
                        std::lock_guard<std::mutex> guard(m_event_mutex);
                        auto ack_it = m_acks.find(pack_id);
                        if (ack_it != m_acks.end())
                        {
                            removed_ack = ack_it->second;
                            m_acks.erase(ack_it);
                        }
                        auto timer_it = m_ack_timers.find(pack_id);
                        if (timer_it != m_ack_timers.end())
                        {
                            m_ack_timers.erase(timer_it);
                        }
                    }

                    // Call timeout callback if ack was still pending
                    if (removed_ack && timeout_callback)
                    {
                        timeout_callback();
                    }
                } });

            {
                std::lock_guard<std::mutex> guard(m_event_mutex);
                m_acks[pack_id] = ack;
                m_ack_timers[pack_id] = timer;
            }
        }
        else
        {
            pack_id = -1;
        }
        packet p(m_nsp, msg_ptr, pack_id);
        send_packet(p);
    }

    void socket::impl::send_connect()
    {
        NULL_GUARD(m_client);
        packet p(packet::type_connect, m_nsp, m_auth);
        m_client->send(p);
        m_connection_timer.reset(new asio::steady_timer(m_client->get_io_service()));
        m_connection_timer->expires_after(std::chrono::milliseconds(20000));
        m_connection_timer->async_wait([this](auto const& ec) { timeout_connection(ec); });
    }

    void socket::impl::close()
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            packet p(packet::type_disconnect, m_nsp);
            send_packet(p);

            if (!m_connection_timer)
            {
                m_connection_timer.reset(new asio::steady_timer(m_client->get_io_service()));
            }
            m_connection_timer->expires_after(std::chrono::milliseconds(3000));
            m_connection_timer->async_wait([this](auto const&) { on_close(); });
        }
    }

    void socket::impl::on_connected()
    {
        if (m_connection_timer)
        {
            m_connection_timer->cancel();
            m_connection_timer.reset();
        }
        if (!m_connected)
        {
            m_connected = true;
            m_connected_at = std::chrono::system_clock::now();
            m_client->on_socket_opened(m_nsp);

            // Extract all queued packets under lock, then send without holding lock
            std::vector<sio::packet> packets_to_send;
            {
                std::lock_guard<std::mutex> guard(m_packet_mutex);
                while (!m_packet_queue.empty())
                {
                    packets_to_send.push_back(std::move(m_packet_queue.front()));
                    m_packet_queue.pop();
                }
            }
            // Send packets without holding the lock
            for (auto &packet : packets_to_send)
            {
                m_client->send(packet);
            }
        }
    }

    void socket::impl::on_close()
    {
        NULL_GUARD(m_client);

        if (m_connection_timer)
        {
            m_connection_timer->cancel();
            m_connection_timer.reset();
        }
        m_connected = false;
        {
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            while (!m_packet_queue.empty())
            {
                m_packet_queue.pop();
            }
        }

        // Save client pointer and namespace before clearing to prevent use-after-free
        sio::client_impl *client = m_client;
        std::string nsp = m_nsp;

        // Clear m_client to prevent reuse
        m_client = nullptr;

        // Notify and remove - don't use 'this' after remove_socket as it may delete this object
        client->on_socket_closed(nsp);
        client->remove_socket(nsp);
    }

    void socket::impl::on_open()
    {
        send_connect();
    }

    void socket::impl::on_disconnect()
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            m_connected = false;
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            while (!m_packet_queue.empty())
            {
                m_packet_queue.pop();
            }
        }
    }

    void socket::impl::on_message_packet(packet const &p)
    {
        NULL_GUARD(m_client);
        if (p.get_nsp() == m_nsp)
        {
            m_packets_received.fetch_add(1, std::memory_order_relaxed);
            switch (p.get_type())
            {
            // Connect open
            case packet::type_connect:
            {
                LOG("Received Message type (Connect)" << std::endl);

                this->on_connected();
                break;
            }
            case packet::type_disconnect:
            {
                LOG("Received Message type (Disconnect)" << std::endl);
                this->on_close();
                break;
            }
            case packet::type_event:
            case packet::type_binary_event:
            {
                LOG("Received Message type (Event)" << std::endl);
                const message::ptr ptr = p.get_message();
                if (ptr->get_flag() == message::flag_array)
                {
                    const array_message *array_ptr = static_cast<const array_message *>(ptr.get());
                    if (array_ptr->get_vector().size() >= 1 && array_ptr->get_vector()[0]->get_flag() == message::flag_string)
                    {
                        const string_message *name_ptr = static_cast<const string_message *>(array_ptr->get_vector()[0].get());
                        message::list mlist;
                        for (size_t i = 1; i < array_ptr->get_vector().size(); ++i)
                        {
                            mlist.push(array_ptr->get_vector()[i]);
                        }
                        this->on_socketio_event(p.get_nsp(), p.get_pack_id(), name_ptr->get_string(), std::move(mlist));
                    }
                }

                break;
            }
                // Ack
            case packet::type_ack:
            case packet::type_binary_ack:
            {
                LOG("Received Message type (ACK)" << std::endl);
                const message::ptr ptr = p.get_message();
                if (ptr->get_flag() == message::flag_array)
                {
                    message::list msglist(ptr->get_vector());
                    this->on_socketio_ack(p.get_pack_id(), msglist);
                }
                else
                {
                    this->on_socketio_ack(p.get_pack_id(), message::list(ptr));
                }
                break;
            }
                // Error
            case packet::type_error:
            {
                LOG("Received Message type (ERROR)" << std::endl);
                this->on_socketio_error(p.get_message());
                break;
            }
            default:
                break;
            }
        }
    }

    void socket::impl::on_socketio_event(const std::string &nsp, int msgId, const std::string &name, message::list &&message)
    {
        bool needAck = msgId >= 0;
        event ev = event_adapter::create_event(nsp, name, std::move(message), needAck);
        event_listener func = this->get_bind_listener_locked(name);
        if (func)
            func(ev);

        event_listener any_listener;
        {
            std::lock_guard<std::mutex> guard(m_event_mutex);
            any_listener = m_event_listener;
        }
        if (any_listener)
            any_listener(ev);

        if (needAck)
        {
            this->ack(msgId, name, ev.get_ack_message());
        }
    }

    void socket::impl::ack(int msgId, const string &, const message::list &ack_message)
    {
        packet p(m_nsp, ack_message.to_array_message(), msgId, true);
        send_packet(p);
    }

    void socket::impl::on_socketio_ack(int msgId, message::list const &message)
    {
        std::function<void(message::list const &)> l;
        std::shared_ptr<asio::steady_timer> timer;
        {
            std::lock_guard<std::mutex> guard(m_event_mutex);
            auto it = m_acks.find(msgId);
            if (it != m_acks.end())
            {
                l = it->second;
                m_acks.erase(it);
            }
            // Cancel and remove timeout timer if exists
            auto timer_it = m_ack_timers.find(msgId);
            if (timer_it != m_ack_timers.end())
            {
                timer = timer_it->second;
                m_ack_timers.erase(timer_it);
            }
        }
        // Cancel timer outside of lock
        if (timer)
        {
            timer->cancel();
        }
        if (l)
            l(message);
    }

    void socket::impl::on_socketio_error(message::ptr const &err_message)
    {
        if (m_error_listener)
            m_error_listener(err_message);
    }

    void socket::impl::timeout_connection(const asio::error_code &ec)
    {
        NULL_GUARD(m_client);
        if (ec)
        {
            return;
        }
        m_connection_timer.reset();
        LOG("Connection timeout,close socket." << std::endl);
        // Should close socket if no connected message arrive.Otherwise we'll never ask for open again.
        this->on_close();
    }

    void socket::impl::send_packet(sio::packet &p)
    {
        NULL_GUARD(m_client);
        if (m_connected)
        {
            // Extract all queued packets under lock, then send without holding lock
            std::vector<sio::packet> packets_to_send;
            {
                std::lock_guard<std::mutex> guard(m_packet_mutex);
                while (!m_packet_queue.empty())
                {
                    packets_to_send.push_back(std::move(m_packet_queue.front()));
                    m_packet_queue.pop();
                }
            }
            // Send queued packets without holding the lock
            for (auto &packet : packets_to_send)
            {
                m_client->send(packet);
                m_packets_sent.fetch_add(1, std::memory_order_relaxed);
            }
            // Send the new packet
            m_client->send(p);
            m_packets_sent.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            std::lock_guard<std::mutex> guard(m_packet_mutex);
            m_packet_queue.push(p);
        }
    }

    socket::event_listener socket::impl::get_bind_listener_locked(const string &event)
    {
        std::lock_guard<std::mutex> guard(m_event_mutex);
        auto it = m_event_binding.find(event);
        if (it != m_event_binding.end())
        {
            return it->second;
        }
        return socket::event_listener();
    }

    connection_metrics socket::impl::get_metrics() const
    {
        connection_metrics metrics;
        metrics.packets_sent = m_packets_sent.load(std::memory_order_relaxed);
        metrics.packets_received = m_packets_received.load(std::memory_order_relaxed);
        metrics.connected_at = m_connected_at;

        // Get client-level metrics
        if (m_client)
        {
            metrics.reconnection_count = m_client->m_reconn_made;
            auto latency_ms = m_client->m_last_ping_latency_ms.load(std::memory_order_relaxed);
            metrics.last_ping_latency = std::chrono::milliseconds(latency_ms);
        }
        else
        {
            metrics.reconnection_count = 0;
            metrics.last_ping_latency = std::chrono::milliseconds(0);
        }

        return metrics;
    }

    void socket::impl_deleter::operator()(impl *p) const
    {
        delete p;
    }

    socket::socket(client_impl *client, std::string const &nsp, message::ptr const &auth) : m_impl(new impl(client, nsp, auth))
    {
    }

    socket::~socket()
    {
    }

    void socket::on(std::string const &event_name, event_listener const &func)
    {
        m_impl->on(event_name, func);
    }

    void socket::on(std::string const &event_name, event_listener_aux const &func)
    {
        m_impl->on(event_name, func);
    }

    void socket::on_any(event_listener_aux const &func)
    {
        m_impl->on_any(func);
    }

    void socket::on_any(event_listener const &func)
    {
        m_impl->on_any(func);
    }

    void socket::on_with_ack(std::string const &event_name, event_handler_with_ack const &handler)
    {
        // Wrap the handler to automatically handle ack with custom data
        event_listener wrapper = [handler](event &ev)
        {
            message::list ack_msg;
            // Always call the handler - it can populate ack_msg if needed
            handler(ev.get_message(), ack_msg);

            // If ack is needed and handler provided data, send it
            if (ev.need_ack() && ack_msg.size() > 0)
            {
                ev.put_ack_message(ack_msg);
            }
        };
        m_impl->on(event_name, wrapper);
    }

    void socket::on_with_ack(std::string const &event_name, simple_event_handler const &handler)
    {
        // Adapt old API: handler returns bool, which we convert to [true|false] ack payload
        event_handler_with_ack adapter = [handler](message::ptr const &msg, message::list &ack_msg)
        {
            bool success = false;
            try
            {
                success = handler(msg);
            }
            catch (...)
            {
                success = false;
            }
            ack_msg.push(bool_message::create(success));
        };
        on_with_ack(event_name, adapter);
    }

    void socket::off(std::string const &event_name)
    {
        m_impl->off(event_name);
    }

    void socket::off_all()
    {
        m_impl->off_all();
    }

    void socket::close()
    {
        m_impl->close();
    }

    void socket::on_error(error_listener const &l)
    {
        m_impl->on_error(l);
    }

    void socket::off_error()
    {
        m_impl->off_error();
    }

    void socket::emit(std::string const &event_name, message::list const &msglist)
    {
        m_impl->emit(event_name, msglist, nullptr);
    }

    void socket::emit_with_ack(std::string const &event_name, message::list const &msglist, std::function<void(message::list const &)> const &ack)
    {
        m_impl->emit(event_name, msglist, ack);
    }

    void socket::emit_with_ack(std::string const &event_name,
                               message::list const &msglist,
                               std::function<void(message::list const &)> const &ack,
                               unsigned timeout_ms,
                               std::function<void()> const &timeout_callback)
    {
        m_impl->emit(event_name, msglist, ack, timeout_ms, timeout_callback);
    }

    // C++20 Coroutine support - emit_async without timeout
    emit_task socket::emit_async(std::string const &event_name,
                                 message::list const &msglist)
    {
        // Create awaiter object that will be suspended and resumed
        auto awaiter = std::make_shared<emit_awaiter>();

        // Create the callback that will resume the coroutine
        auto ack_callback = [awaiter](message::list const &response)
        {
            awaiter->set_result(response);
        };

        // Start the async operation
        m_impl->emit(event_name, msglist, ack_callback);

        // Suspend until the callback resumes us and return the result
        co_return co_await *awaiter;
    }

    // C++20 Coroutine support - emit_async with timeout
    emit_task socket::emit_async(std::string const &event_name,
                                 message::list const &msglist,
                                 unsigned timeout_ms)
    {
        // Create awaiter object that will be suspended and resumed
        auto awaiter = std::make_shared<emit_awaiter>();

        // Create the ack callback that will resume with result
        auto ack_callback = [awaiter](message::list const &response)
        {
            awaiter->set_result(response);
        };

        // Create the timeout callback that will resume with exception
        auto timeout_callback = [awaiter]()
        {
            auto ex = std::make_exception_ptr(timeout_exception());
            awaiter->set_exception(ex);
        };

        // Start the async operation with timeout
        m_impl->emit(event_name, msglist, ack_callback, timeout_ms, timeout_callback);

        // Suspend until one of the callbacks resumes us and return the result
        co_return co_await *awaiter;
    }

    std::string const &socket::get_namespace() const
    {
        return m_impl->get_namespace();
    }

    connection_metrics socket::get_metrics() const
    {
        return m_impl->get_metrics();
    }

    void socket::on_connected()
    {
        m_impl->on_connected();
    }

    void socket::on_close()
    {
        m_impl->on_close();
    }

    void socket::on_open()
    {
        m_impl->on_open();
    }

    void socket::on_message_packet(packet const &p)
    {
        m_impl->on_message_packet(p);
    }

    void socket::on_disconnect()
    {
        m_impl->on_disconnect();
    }
}
