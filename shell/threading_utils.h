#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace threading_utils
{

template <typename PathT, typename ProcessFn, typename FlushFn>
void run_ordered_file_tasks(const std::vector<PathT>& files, int thread_type, int thread_num, ProcessFn process_fn, FlushFn flush_fn)
{
    if (files.empty())
    {
        return;
    }

    if (thread_type != 1)
    {
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            flush_fn(files[index], index, process_fn(files[index], index));
        }
        return;
    }

    const std::size_t worker_count = thread_num <= 0 ? 1U : static_cast<std::size_t>(thread_num);
    if (worker_count <= 1U || files.size() <= 1U)
    {
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            flush_fn(files[index], index, process_fn(files[index], index));
        }
        return;
    }

    using Result = std::invoke_result_t<ProcessFn, const PathT&, std::size_t>;

    std::vector<std::optional<Result>> results(files.size());
    std::atomic<std::size_t> next_task_index{0};
    std::mutex result_mutex;
    std::condition_variable result_cv;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index)
    {
        workers.emplace_back([&]() {
            while (true)
            {
                const std::size_t task_index = next_task_index.fetch_add(1);
                if (task_index >= files.size())
                {
                    break;
                }

                Result result = process_fn(files[task_index], task_index);
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    results[task_index].emplace(std::move(result));
                }
                result_cv.notify_all();
            }
        });
    }

    for (std::size_t next_expected_index = 0; next_expected_index < files.size(); ++next_expected_index)
    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait(lock, [&]() {
            return results[next_expected_index].has_value();
        });

        Result result = std::move(*results[next_expected_index]);
        results[next_expected_index].reset();
        lock.unlock();

        flush_fn(files[next_expected_index], next_expected_index, result);
    }

    for (std::thread& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

} // namespace threading_utils
