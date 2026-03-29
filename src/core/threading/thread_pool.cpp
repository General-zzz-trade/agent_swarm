#include "thread_pool.h"

#include <algorithm>

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::max(2u, std::thread::hardware_concurrency());
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this] {
                        return stop_.load(std::memory_order_relaxed) || !tasks_.empty();
                    });
                    if (stop_.load(std::memory_order_relaxed) && tasks_.empty()) {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_release);
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t ThreadPool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}
