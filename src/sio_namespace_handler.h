#ifndef SIO_NAMESPACE_HANDLER_H
#define SIO_NAMESPACE_HANDLER_H

#include "sio_client.h"
#include "sio_socket.h"
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace sio
{
    /**
     * Helper class to manage events for a specific namespace.
     * Simplifies working with a single namespace by caching the socket
     * and providing convenient methods.
     */
    class namespace_handler
    {
    public:
        namespace_handler(client& cli, const std::string& nsp)
            : client_(cli), namespace_(nsp)
        {
            socket_ = client_.socket(namespace_);
        }

        /**
         * Register an event listener
         */
        void on(const std::string& event_name, socket::event_listener const& listener)
        {
            if (socket_) {
                socket_->on(event_name, listener);
                registered_events_.push_back(event_name);
            }
        }

        /**
         * Register an event listener (aux version)
         */
        void on(const std::string& event_name, socket::event_listener_aux const& listener)
        {
            if (socket_) {
                socket_->on(event_name, listener);
                registered_events_.push_back(event_name);
            }
        }

        /**
         * Register a "catch-all" event listener
         */
        void on_any(socket::event_listener const& listener)
        {
            if (socket_) {
                socket_->on_any(listener);
            }
        }

        /**
         * Unregister an event listener
         */
        void off(const std::string& event_name)
        {
            if (socket_) {
                socket_->off(event_name);
                registered_events_.erase(
                    std::remove(registered_events_.begin(), registered_events_.end(), event_name),
                    registered_events_.end()
                );
            }
        }

        /**
         * Unregister all event listeners
         */
        void off_all()
        {
            if (socket_) {
                socket_->off_all();
                registered_events_.clear();
            }
        }

        /**
         * Emit an event
         */
        void emit(const std::string& event_name, const message::ptr& message)
        {
            if (socket_) {
                socket_->emit(event_name, message);
            }
        }

        /**
         * Emit an event with acknowledgment callback
         */
        void emit(const std::string& event_name,
                 const message::ptr& message,
                 std::function<void(const message::list&)> const& ack)
        {
            if (socket_) {
                socket_->emit(event_name, message, ack);
            }
        }

        /**
         * Emit an event with message list
         */
        void emit(const std::string& event_name, const message::list& messages)
        {
            if (socket_) {
                socket_->emit(event_name, messages);
            }
        }

        /**
         * Get the namespace string
         */
        const std::string& get_namespace() const
        {
            return namespace_;
        }

        /**
         * Get the underlying socket (for advanced usage)
         */
        socket::ptr get_socket() const
        {
            return socket_;
        }

        /**
         * Check if socket is valid
         */
        bool is_valid() const
        {
            return socket_ != nullptr;
        }

    private:
        client& client_;
        std::string namespace_;
        socket::ptr socket_;
        std::vector<std::string> registered_events_;
    };

    /**
     * Create a namespace handler (convenience function)
     */
    inline namespace_handler create_namespace_handler(client& cli, const std::string& nsp)
    {
        return namespace_handler(cli, nsp);
    }

} // namespace sio

#endif // SIO_NAMESPACE_HANDLER_H
