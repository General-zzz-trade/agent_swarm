#ifndef CORE_THREADING_THREAD_POOL_H
#define CORE_THREADING_THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

/// Lock-free-ish thread pool with fixed worker count.
/// Avoids per-task thread creation overhead of std::async.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submit a callable and receive a future for its result.
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /// Number of worker threads.
    std::size_t size() const { return workers_.size(); }

    /// Number of pending tasks in the queue.
    std::size_t pending() const;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
};

// --- Template implementation ---

template <typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return result;
}

#endif
