#include "swarm_coordinator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>

#include "agent.h"

SwarmCoordinator::SwarmCoordinator(ThreadPool& pool, AgentFactory factory)
    : pool_(pool), agent_factory_(std::move(factory)) {}

SwarmCoordinator::SubTaskResult SwarmCoordinator::run_single_task(
    const SubTask& task) {
    try {
        std::unique_ptr<Agent> worker = agent_factory_();
        if (!worker) {
            return {task.description, "Failed to create worker agent", false};
        }

        std::string prompt = task.description;
        if (!task.workspace_scope.empty()) {
            prompt = "Working in subdirectory: " + task.workspace_scope +
                     "\n\n" + prompt;
        }

        const std::string result = worker->run_turn(prompt);
        return {task.description, result, true};
    } catch (const std::exception& e) {
        return {task.description, std::string("Worker error: ") + e.what(), false};
    }
}

std::vector<SwarmCoordinator::SubTaskResult> SwarmCoordinator::execute_parallel(
    const std::vector<SubTask>& tasks) {
    if (tasks.empty()) return {};

    // Limit concurrency
    const std::size_t batch_size = std::min(tasks.size(), max_workers_);
    std::vector<SubTaskResult> all_results;
    all_results.reserve(tasks.size());

    for (std::size_t start = 0; start < tasks.size(); start += batch_size) {
        const std::size_t end = std::min(start + batch_size, tasks.size());
        std::vector<std::future<SubTaskResult>> futures;

        for (std::size_t i = start; i < end; ++i) {
            const SubTask task = tasks[i];
            futures.push_back(pool_.submit(
                [this, task]() { return run_single_task(task); }));
        }

        for (auto& future : futures) {
            all_results.push_back(future.get());
        }
    }

    return all_results;
}

std::vector<SwarmCoordinator::SubTaskResult> SwarmCoordinator::execute_sequential(
    const std::vector<SubTask>& tasks) {
    std::vector<SubTaskResult> results;
    results.reserve(tasks.size());

    for (const auto& task : tasks) {
        results.push_back(run_single_task(task));
    }

    return results;
}

std::string SwarmCoordinator::generate_branch_name(const std::string& task) const {
    // Generate: bolt-team-<slug>-<timestamp>
    // Slug from first few words of the task description
    std::string slug;
    int words = 0;
    for (char c : task) {
        if (c == ' ' || c == '\n') {
            if (!slug.empty() && slug.back() != '-') slug += '-';
            if (++words >= 4) break;
        } else if (std::isalnum(static_cast<unsigned char>(c))) {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (slug.empty()) slug = "task";
    // Trim trailing dash
    if (!slug.empty() && slug.back() == '-') slug.pop_back();
    // Add timestamp for uniqueness
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "bolt-team-" + slug + "-" + std::to_string(ms % 10000);
}

std::string SwarmCoordinator::create_worktree(
    const std::filesystem::path& workspace_root,
    const std::string& branch_name,
    std::shared_ptr<ICommandRunner> command_runner) const {

    std::string worktree_path = "/tmp/bolt-worktree-" + branch_name;

    // Create worktree with a new branch based on HEAD
    std::string cmd = "git worktree add -b " + branch_name +
                      " " + worktree_path + " HEAD 2>&1";
    auto result = command_runner->run(cmd, workspace_root, 30000);

    if (!result.success) {
        return "";  // Failed to create worktree
    }

    return worktree_path;
}

bool SwarmCoordinator::remove_worktree(
    const std::string& worktree_path,
    std::shared_ptr<ICommandRunner> command_runner) const {

    // Run git worktree remove from the parent directory (not inside the worktree)
    auto parent = std::filesystem::path(worktree_path).parent_path();
    auto result = command_runner->run(
        "git worktree remove --force " + worktree_path + " 2>&1",
        parent, 15000);
    return result.success;
}

std::vector<SwarmCoordinator::WorkerResult> SwarmCoordinator::execute_team(
    const std::vector<std::string>& tasks,
    const std::filesystem::path& workspace_root,
    std::shared_ptr<ICommandRunner> command_runner) {

    std::vector<WorkerResult> results;
    results.resize(tasks.size());

    // Create worktrees for each task
    struct WorkerInfo {
        std::string branch;
        std::string worktree;
        std::size_t index;
    };
    std::vector<WorkerInfo> workers;

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        std::string branch = generate_branch_name(tasks[i]);
        std::string wt = create_worktree(workspace_root, branch, command_runner);

        if (wt.empty()) {
            results[i].task = tasks[i];
            results[i].success = false;
            results[i].summary = "Failed to create git worktree";
            continue;
        }

        workers.push_back({branch, wt, i});
    }

    // Execute tasks in parallel using thread pool
    std::vector<std::future<WorkerResult>> futures;

    for (const auto& worker : workers) {
        auto task_copy = tasks[worker.index];
        auto branch = worker.branch;
        auto wt_path = worker.worktree;
        auto runner = command_runner;

        futures.push_back(pool_.submit([task_copy, branch, wt_path, runner]() -> WorkerResult {
            WorkerResult result;
            result.task = task_copy;
            result.branch_name = branch;

            // Execute the task in the worktree context.
            // In a full implementation this would spin up a sub-agent;
            // for now, run the task description as a shell command.
            auto exec = runner->run(task_copy, std::filesystem::path(wt_path), 120000);

            result.success = exec.success;
            result.summary = exec.stdout_output.substr(
                0, std::min<std::size_t>(exec.stdout_output.size(), 500));

            // Get the diff relative to HEAD
            auto diff_result = runner->run(
                "git diff HEAD 2>&1",
                std::filesystem::path(wt_path), 10000);
            result.diff = diff_result.stdout_output;

            // Count changed files via git diff --stat
            auto stat = runner->run(
                "git diff HEAD --stat 2>&1",
                std::filesystem::path(wt_path), 10000);
            int count = 0;
            for (char ch : stat.stdout_output) {
                if (ch == '|') ++count;
            }
            result.files_changed = count;

            return result;
        }));
    }

    // Collect results
    for (std::size_t i = 0; i < futures.size(); ++i) {
        try {
            results[workers[i].index] = futures[i].get();
        } catch (const std::exception& e) {
            results[workers[i].index].task = tasks[workers[i].index];
            results[workers[i].index].success = false;
            results[workers[i].index].summary =
                std::string("Worker failed: ") + e.what();
        }
    }

    // Cleanup worktrees
    for (const auto& worker : workers) {
        remove_worktree(worker.worktree, command_runner);
        // Delete the branch if the task failed or produced no changes
        if (!results[worker.index].success ||
            results[worker.index].diff.empty()) {
            command_runner->run(
                "git branch -D " + worker.branch + " 2>/dev/null",
                workspace_root, 5000);
        }
    }

    return results;
}
