#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace ff7r::piano::core {

enum class JoinedWorkerCode {
    Completed,
    StartFailed,
    WorkerFailed,
};

struct JoinedWorkerResult {
    JoinedWorkerCode code = JoinedWorkerCode::Completed;
    std::size_t started = 0;
    std::size_t joined = 0;
};

struct StandardThreadFactory {
    template <typename Function>
    std::thread operator()(Function&& function) const
    {
        return std::thread(std::forward<Function>(function));
    }
};

class JoiningWorkerSet final {
public:
    JoiningWorkerSet() = default;
    JoiningWorkerSet(const JoiningWorkerSet&) = delete;
    JoiningWorkerSet& operator=(const JoiningWorkerSet&) = delete;

    ~JoiningWorkerSet() noexcept
    {
        (void)join_all();
    }

    void reserve(const std::size_t count)
    {
        workers_.reserve(count);
    }

    void add(std::thread worker) noexcept
    {
        workers_.push_back(std::move(worker));
    }

    std::size_t size() const noexcept
    {
        return workers_.size();
    }

    std::size_t join_all() noexcept
    {
        std::size_t joined = 0;
        for (std::thread& worker : workers_) {
            if (!worker.joinable()) continue;
            try {
                worker.join();
                ++joined;
            } catch (...) {
                try {
                    worker.detach();
                } catch (...) {
                }
            }
        }
        workers_.clear();
        return joined;
    }

private:
    std::vector<std::thread> workers_;
};

template <typename WorkerBody, typename ThreadFactory, typename Admission>
JoinedWorkerResult run_joined_workers(
    const std::size_t worker_count,
    WorkerBody&& worker_body,
    ThreadFactory&& thread_factory,
    Admission&& admission) noexcept
{
    JoiningWorkerSet workers;
    std::atomic<bool> cancel{false};
    std::atomic<bool> worker_failed{false};
    try {
        workers.reserve(worker_count);
        for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
            workers.add(thread_factory(
                [&, worker_index]() noexcept {
                    try {
                        worker_body(worker_index, cancel);
                    } catch (...) {
                        worker_failed.store(true, std::memory_order_release);
                        cancel.store(true, std::memory_order_release);
                    }
                }));
        }
    } catch (...) {
        cancel.store(true, std::memory_order_release);
        admission(false);
        const std::size_t started = workers.size();
        const std::size_t joined = workers.join_all();
        return {JoinedWorkerCode::StartFailed, started, joined};
    }

    admission(true);

    const std::size_t started = workers.size();
    const std::size_t joined = workers.join_all();
    if (worker_failed.load(std::memory_order_acquire)) {
        return {JoinedWorkerCode::WorkerFailed, started, joined};
    }
    return {JoinedWorkerCode::Completed, started, joined};
}

template <typename WorkerBody, typename ThreadFactory>
JoinedWorkerResult run_joined_workers(
    const std::size_t worker_count,
    WorkerBody&& worker_body,
    ThreadFactory&& thread_factory) noexcept
{
    return run_joined_workers(worker_count, std::forward<WorkerBody>(worker_body),
        std::forward<ThreadFactory>(thread_factory), [](bool) noexcept {});
}

template <typename WorkerBody>
JoinedWorkerResult run_joined_workers(
    const std::size_t worker_count,
    WorkerBody&& worker_body) noexcept
{
    return run_joined_workers(
        worker_count,
        std::forward<WorkerBody>(worker_body),
        StandardThreadFactory{});
}

} // namespace ff7r::piano::core
