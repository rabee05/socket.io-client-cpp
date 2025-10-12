#include <catch2/catch_test_macros.hpp>
#include "sio_client.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

TEST_CASE("Thread Safety - Concurrent listener modifications", "[thread][safety]") {
    sio::client client;
    std::atomic<int> counter{0};
    const int num_threads = 10;
    const int iterations = 100;

    SECTION("Concurrent set_open_listener calls") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&client, &counter, iterations]() {
                for (int j = 0; j < iterations; ++j) {
                    client.set_open_listener([&counter]() {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Test passes if no crashes occurred
        REQUIRE(true);
    }

    SECTION("Concurrent listener clear and set") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&client, i, iterations]() {
                for (int j = 0; j < iterations; ++j) {
                    if (i % 2 == 0) {
                        client.clear_con_listeners();
                    } else {
                        client.set_fail_listener([]() {});
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }
}

TEST_CASE("Thread Safety - Concurrent socket operations", "[thread][safety]") {
    sio::client client;
    const int num_threads = 10;
    const int iterations = 50;

    SECTION("Concurrent socket() calls") {
        std::vector<std::thread> threads;
        std::atomic<int> socket_count{0};

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&client, &socket_count, iterations]() {
                for (int j = 0; j < iterations; ++j) {
                    auto sock = client.socket("/test");
                    if (sock) {
                        socket_count.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Should have created sockets successfully
        REQUIRE(socket_count.load() > 0);
    }

    SECTION("Concurrent socket operations on different namespaces") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&client, i, iterations]() {
                std::string nsp = "/test" + std::to_string(i);
                for (int j = 0; j < iterations; ++j) {
                    auto sock = client.socket(nsp);
                    sock->on("event", [](sio::event& e) {});
                    sock->off("event");
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }
}

TEST_CASE("Thread Safety - Concurrent socket event bindings", "[thread][safety]") {
    sio::client client;
    auto sock = client.socket("/test");
    const int num_threads = 10;
    const int iterations = 100;

    SECTION("Concurrent on() calls") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&sock, i, iterations]() {
                std::string event_name = "event" + std::to_string(i);
                for (int j = 0; j < iterations; ++j) {
                    sock->on(event_name, [](sio::event& e) {});
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }

    SECTION("Concurrent on() and off() calls") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&sock, i, iterations]() {
                std::string event_name = "event" + std::to_string(i % 5);
                for (int j = 0; j < iterations; ++j) {
                    if (j % 2 == 0) {
                        sock->on(event_name, [](sio::event& e) {});
                    } else {
                        sock->off(event_name);
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }

    SECTION("Concurrent on_any() calls") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&sock, iterations]() {
                for (int j = 0; j < iterations; ++j) {
                    sock->on_any([](sio::event& e) {});
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }
}

TEST_CASE("Thread Safety - Client state changes", "[thread][safety]") {
    const int num_clients = 5;
    const int num_operations = 20;

    SECTION("Concurrent client creation and destruction") {
        std::vector<std::thread> threads;

        for (int i = 0; i < num_clients; ++i) {
            threads.emplace_back([num_operations]() {
                for (int j = 0; j < num_operations; ++j) {
                    sio::client client;
                    client.set_open_listener([]() {});
                    auto sock = client.socket("/test");
                    sock->on("event", [](sio::event& e) {});
                    // Client destroyed at end of scope
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(true);
    }
}

TEST_CASE("Memory Safety - Listener lifecycle", "[memory][safety]") {
    SECTION("Listeners with captured shared_ptr") {
        auto shared_data = std::make_shared<std::atomic<int>>(0);

        {
            sio::client client;
            client.set_open_listener([shared_data]() {
                shared_data->fetch_add(1, std::memory_order_relaxed);
            });
            // Client destroyed here, listener should be cleaned up
        }

        // shared_data should still be valid
        REQUIRE(shared_data.use_count() == 1);
    }

    SECTION("Socket with event handlers") {
        auto shared_data = std::make_shared<std::atomic<int>>(0);

        {
            sio::client client;
            auto sock = client.socket("/test");
            sock->on("event", [shared_data](sio::event& e) {
                shared_data->fetch_add(1, std::memory_order_relaxed);
            });
            // Socket and client destroyed here
        }

        REQUIRE(shared_data.use_count() == 1);
    }
}
