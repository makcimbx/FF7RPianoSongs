#include "core/startup_thread.h"
#include "core/fail_closed_boundary.h"
#include "core/joining_workers.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

using ff7r::piano::core::StartupThreadResult;

constexpr auto kSynchronizationTimeout = std::chrono::seconds(5);

struct ThreadAdapterFixture {
    std::mutex mutex;
    std::condition_variable create_entered_condition;
    std::condition_variable create_release_condition;
    bool block_create = false;
    bool create_entered = false;
    bool allow_create_return = false;
    HANDLE create_result = nullptr;
    BOOL close_result = TRUE;
    int create_calls = 0;
    int close_calls = 0;
    LPSECURITY_ATTRIBUTES security_attributes = nullptr;
    SIZE_T stack_size = 0;
    LPTHREAD_START_ROUTINE worker = nullptr;
    LPVOID context = nullptr;
    DWORD creation_flags = 0;
    LPDWORD thread_id = nullptr;
    HANDLE closed_handle = nullptr;
};

ThreadAdapterFixture* g_fixture = nullptr;

void require(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "startup_thread_selftest: " << message << '\n';
        std::exit(1);
    }
}

DWORD WINAPI test_worker(LPVOID)
{
    return 0;
}

HANDLE WINAPI fake_create_thread(
    LPSECURITY_ATTRIBUTES security_attributes,
    const SIZE_T stack_size,
    const LPTHREAD_START_ROUTINE worker,
    const LPVOID context,
    const DWORD creation_flags,
    LPDWORD thread_id)
{
    ThreadAdapterFixture& fixture = *g_fixture;
    std::unique_lock<std::mutex> lock(fixture.mutex);
    ++fixture.create_calls;
    fixture.security_attributes = security_attributes;
    fixture.stack_size = stack_size;
    fixture.worker = worker;
    fixture.context = context;
    fixture.creation_flags = creation_flags;
    fixture.thread_id = thread_id;
    fixture.create_entered = true;
    fixture.create_entered_condition.notify_all();
    require(fixture.create_release_condition.wait_for(
                lock,
                kSynchronizationTimeout,
                [&fixture] { return !fixture.block_create || fixture.allow_create_return; }),
        "timed out waiting to release the create adapter");
    return fixture.create_result;
}

BOOL WINAPI fake_close_handle(const HANDLE handle)
{
    ThreadAdapterFixture& fixture = *g_fixture;
    std::lock_guard<std::mutex> lock(fixture.mutex);
    ++fixture.close_calls;
    fixture.closed_handle = handle;
    return fixture.close_result;
}

void test_create_failure_reopens_gate_and_retry_succeeds()
{
    ThreadAdapterFixture fixture;
    g_fixture = &fixture;
    std::atomic<bool> claimed{false};

    const StartupThreadResult failed = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(failed == StartupThreadResult::CreateFailed, "null create did not report CreateFailed");
    require(!claimed.load(std::memory_order_acquire), "null create did not reopen the gate");
    require(fixture.create_calls == 1, "null create did not call the create adapter exactly once");
    require(fixture.close_calls == 0, "null create called the close adapter");

    const HANDLE expected_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1234));
    fixture.create_result = expected_handle;
    const StartupThreadResult retried = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(retried == StartupThreadResult::Started, "retry did not start the worker thread");
    require(claimed.load(std::memory_order_acquire), "successful retry did not retain the claim");
    require(fixture.create_calls == 2, "retry did not call the create adapter exactly once");
    require(fixture.close_calls == 1, "successful retry did not close exactly once");
    require(fixture.closed_handle == expected_handle, "successful retry closed the wrong handle");
}

void test_already_claimed_skips_adapters()
{
    ThreadAdapterFixture fixture;
    g_fixture = &fixture;
    std::atomic<bool> claimed{true};

    const StartupThreadResult result = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(result == StartupThreadResult::AlreadyClaimed, "claimed gate did not report AlreadyClaimed");
    require(claimed.load(std::memory_order_acquire), "AlreadyClaimed changed the gate");
    require(fixture.create_calls == 0, "AlreadyClaimed called the create adapter");
    require(fixture.close_calls == 0, "AlreadyClaimed called the close adapter");
}

void test_blocked_create_failure_reopens_gate_and_retry_succeeds()
{
    ThreadAdapterFixture fixture;
    fixture.block_create = true;
    g_fixture = &fixture;
    std::atomic<bool> claimed{false};
    StartupThreadResult first_result = StartupThreadResult::Started;

    std::thread first([&] {
        first_result = ff7r::piano::core::start_thread_once(
            claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    });

    {
        std::unique_lock<std::mutex> lock(fixture.mutex);
        require(fixture.create_entered_condition.wait_for(
                    lock,
                    kSynchronizationTimeout,
                    [&fixture] { return fixture.create_entered; }),
            "timed out waiting for the create adapter to block");
    }

    const StartupThreadResult second_result = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(second_result == StartupThreadResult::AlreadyClaimed,
        "concurrent call did not observe the in-progress claim");
    {
        std::lock_guard<std::mutex> lock(fixture.mutex);
        require(fixture.create_calls == 1, "concurrent call invoked the create adapter");
        require(fixture.close_calls == 0, "handle closed before blocked creation returned");
        fixture.allow_create_return = true;
    }
    fixture.create_release_condition.notify_all();
    first.join();

    require(first_result == StartupThreadResult::CreateFailed,
        "blocked null creation did not report CreateFailed");
    require(!claimed.load(std::memory_order_acquire),
        "blocked null creation did not reopen the gate");
    require(fixture.create_calls == 1, "blocked null creation count changed after release");
    require(fixture.close_calls == 0, "blocked null creation called the close adapter");

    const HANDLE expected_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x5678));
    fixture.create_result = expected_handle;
    const StartupThreadResult retried = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(retried == StartupThreadResult::Started, "retry after blocked failure did not start");
    require(claimed.load(std::memory_order_acquire),
        "retry after blocked failure did not retain the claim");
    require(fixture.create_calls == 2, "retry after blocked failure did not create exactly once");
    require(fixture.close_calls == 1, "retry after blocked failure did not close exactly once");
    require(fixture.closed_handle == expected_handle,
        "retry after blocked failure closed the wrong handle");
}

void test_close_failure_retains_claim()
{
    ThreadAdapterFixture fixture;
    fixture.create_result = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x9abc));
    fixture.close_result = FALSE;
    g_fixture = &fixture;
    std::atomic<bool> claimed{false};

    const StartupThreadResult result = ff7r::piano::core::start_thread_once(
        claimed, test_worker, nullptr, fake_create_thread, fake_close_handle);
    require(result == StartupThreadResult::Started, "close failure changed successful creation result");
    require(claimed.load(std::memory_order_acquire), "close failure reopened the gate");
    require(fixture.create_calls == 1, "close failure path created more than once");
    require(fixture.close_calls == 1, "close failure path did not close exactly once");
    require(fixture.closed_handle == fixture.create_result, "close failure path received the wrong handle");
}

void test_adapter_arguments_are_forwarded_exactly()
{
    ThreadAdapterFixture fixture;
    fixture.create_result = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xdef0));
    g_fixture = &fixture;
    std::atomic<bool> claimed{false};
    int context_value = 42;

    const StartupThreadResult result = ff7r::piano::core::start_thread_once(
        claimed, test_worker, &context_value, fake_create_thread, fake_close_handle);
    require(result == StartupThreadResult::Started, "argument forwarding setup did not start");
    require(fixture.security_attributes == nullptr, "security attributes were not null");
    require(fixture.stack_size == 0, "stack size was not zero");
    require(fixture.worker == test_worker, "worker argument changed");
    require(fixture.context == &context_value, "context argument changed");
    require(fixture.creation_flags == 0, "creation flags were not zero");
    require(fixture.thread_id == nullptr, "thread-id output was not null");
    require(fixture.close_calls == 1 && fixture.closed_handle == fixture.create_result,
        "created handle was not forwarded exactly to close");
}

void test_partial_worker_construction_joins_started_worker()
{
    std::mutex mutex;
    std::condition_variable entered_condition;
    std::condition_variable release_condition;
    bool entered = false;
    bool release = false;
    int bodies_completed = 0;
    std::size_t factory_calls = 0;

    const auto factory = [&](auto&& body) -> std::thread {
        if (factory_calls++ == 1u) {
            std::unique_lock<std::mutex> lock(mutex);
            require(entered_condition.wait_for(lock, kSynchronizationTimeout, [&] { return entered; }),
                "timed out waiting for the first joined worker to start");
            release = true;
            lock.unlock();
            release_condition.notify_all();
            throw std::runtime_error("injected worker construction failure");
        }
        return std::thread(std::forward<decltype(body)>(body));
    };
    const auto result = ff7r::piano::core::run_joined_workers(
        2u,
        [&](std::size_t, const std::atomic<bool>&) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            entered_condition.notify_all();
            require(release_condition.wait_for(lock, kSynchronizationTimeout, [&] { return release; }),
                "timed out waiting to release the first joined worker");
            ++bodies_completed;
        },
        factory);

    require(result.code == ff7r::piano::core::JoinedWorkerCode::StartFailed,
        "partial construction did not report StartFailed");
    require(result.started == 1u && result.joined == 1u,
        "partial construction did not join exactly the one started worker");
    require(bodies_completed == 1, "joined worker did not finish before failure returned");
}

void test_worker_body_exception_is_captured_after_join()
{
    const auto result = ff7r::piano::core::run_joined_workers(
        1u,
        [](std::size_t, const std::atomic<bool>&) { throw 42; });
    require(result.code == ff7r::piano::core::JoinedWorkerCode::WorkerFailed,
        "unknown worker exception did not report WorkerFailed");
    require(result.started == 1u && result.joined == 1u,
        "worker exception did not join exactly the started worker");
}

void test_fail_closed_boundary_captures_all_exceptions()
{
    ff7r::piano::core::FailClosedException observed =
        ff7r::piano::core::FailClosedException::Unknown;
    const int standard_result = ff7r::piano::core::invoke_fail_closed<int>(
        []() -> int { throw std::runtime_error("injected startup failure"); },
        [&](const ff7r::piano::core::FailClosedException exception) { observed = exception; },
        17);
    require(standard_result == 17 && observed == ff7r::piano::core::FailClosedException::Standard,
        "standard startup exception escaped or was misclassified");

    const int unknown_result = ff7r::piano::core::invoke_fail_closed<int>(
        []() -> int { throw 42; },
        [](ff7r::piano::core::FailClosedException) { throw std::runtime_error("logging failed"); },
        23);
    require(unknown_result == 23,
        "unknown startup exception or failure-handler exception escaped the boundary");
}

} // namespace

int main()
{
    test_create_failure_reopens_gate_and_retry_succeeds();
    test_already_claimed_skips_adapters();
    test_blocked_create_failure_reopens_gate_and_retry_succeeds();
    test_close_failure_retains_claim();
    test_adapter_arguments_are_forwarded_exactly();
    test_partial_worker_construction_joins_started_worker();
    test_worker_body_exception_is_captured_after_join();
    test_fail_closed_boundary_captures_all_exceptions();
    return 0;
}
