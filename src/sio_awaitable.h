#ifndef SIO_AWAITABLE_H
#define SIO_AWAITABLE_H

#include "sio_message.h"
#include <coroutine>
#include <exception>
#include <memory>

namespace sio
{
    // Exception thrown when emit_async times out
    class timeout_exception : public std::runtime_error
    {
    public:
        timeout_exception() : std::runtime_error("Socket.IO emit timeout") {}
    };

    // Awaitable for emit_async operations
    class emit_awaiter
    {
    public:
        emit_awaiter() = default;

        bool await_ready() const noexcept
        {
            return false; // Always suspend
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            continuation_ = handle;
        }

        message::list await_resume()
        {
            if (exception_)
            {
                std::rethrow_exception(exception_);
            }
            return result_;
        }

        // Called by callback when response arrives
        void set_result(message::list const &result)
        {
            result_ = std::move(const_cast<message::list&>(result));
            if (continuation_)
            {
                continuation_.resume();
            }
        }

        // Called by callback when timeout occurs
        void set_exception(std::exception_ptr ex)
        {
            exception_ = ex;
            if (continuation_)
            {
                continuation_.resume();
            }
        }

    private:
        std::coroutine_handle<> continuation_;
        message::list result_;
        std::exception_ptr exception_;
    };

    // Task type for coroutine functions
    class emit_task
    {
    public:
        struct promise_type
        {
            message::list result_value;
            std::exception_ptr exception_ptr;

            emit_task get_return_object()
            {
                return emit_task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void return_value(message::list const &value)
            {
                result_value = std::move(const_cast<message::list&>(value));
            }

            void unhandled_exception()
            {
                exception_ptr = std::current_exception();
            }
        };

        explicit emit_task(std::coroutine_handle<promise_type> h) : handle_(h) {}

        emit_task(emit_task &&other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        emit_task &operator=(emit_task &&other) noexcept
        {
            if (this != &other)
            {
                if (handle_)
                    handle_.destroy();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        ~emit_task()
        {
            if (handle_)
                handle_.destroy();
        }

        // Make task awaitable
        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> continuation) noexcept
        {
            continuation_ = continuation;
            handle_.resume();
        }

        message::list await_resume()
        {
            if (handle_.promise().exception_ptr)
            {
                std::rethrow_exception(handle_.promise().exception_ptr);
            }
            return handle_.promise().result_value;
        }

        message::list get_result()
        {
            if (!handle_.done())
            {
                handle_.resume();
            }
            return await_resume();
        }

    private:
        std::coroutine_handle<promise_type> handle_;
        std::coroutine_handle<> continuation_;
    };

} // namespace sio

#endif // SIO_AWAITABLE_H
