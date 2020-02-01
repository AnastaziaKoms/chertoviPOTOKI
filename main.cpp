#include "executors.h"

bool Task::isCompleted() {
    return (status == TaskStatus::Completed);
}

bool Task::isFailed() {
    return (status == TaskStatus::Failed);
}

bool Task::isCanceled() {
    return (status == TaskStatus::Canceled);
}

bool Task::isFinished() {
    return (isCompleted() || isFailed() || isCanceled());
}

void Task::cancel() {
    TaskStatus temp_status = TaskStatus::Awaiting;
    if (status.compare_exchange_strong(temp_status, TaskStatus::Canceled)) {}
}

void Task::wait() {
    while (!isFinished()) {}
}

void Task::addDependency(std::shared_ptr<Task> dependency) {
    dependencies.push_back(dependency);
}

void Task::addTrigger(std::shared_ptr<Task> trigger) {
    have_trigger = true;
    trigger->triggers.push_back(shared_from_this());
}

bool Task::isProcessing() {
    return (status == TaskStatus::Processing);
}

bool Task::isAwaiting() {
    return (status == TaskStatus::Awaiting);
}

std::exception_ptr Task::getError() {
    return eptr;
}

void Task::setTimeTrigger(std::chrono::system_clock::time_point at) {
    if (!have_time_trigger) {
        have_time_trigger = true;
        deadline = at;
    }
}

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads) {
    return std::make_shared<MyExecutor>(num_threads);
}

void MyExecutor::kernel() {
    while (status != ExecutorStatus::Shutdown) {
        // shutdown all kernels
        if (status == ExecutorStatus::StartShutdown && queue_tasks.empty()) {
            status = ExecutorStatus::Shutdown;
            return;
        }

        std::shared_ptr<Task> task;
        {
            std::lock_guard<std::mutex> g(time_priority_mutex);
            std::chrono::system_clock::time_point now_point = std::chrono::system_clock::now();
            if (!time_priority.empty() && time_priority.top().first < now_point) {
                task = time_priority.top().second;
                time_priority.pop();
            }
        }

        if (!task) task = queue_tasks.pop();

        if (task) {
            if (task->status == Task::TaskStatus::Awaiting) {
                task->status = Task::TaskStatus::Processing;
                try {
                    bool success = true;
                    for (auto &t : task->dependencies) {
                        if (t->isFinished()) {
                            if (!t->isCompleted()) {
                                throw std::logic_error("dependency isn't completed");
                            }
                        } else {
                            task->status = Task::TaskStatus::Awaiting;
                            queue_tasks.push(task);
                            success = false;
                            break;
                        }
                    }

                    if (!success) {
                        continue;
                    }

                    task->run();
                    task->status = Task::TaskStatus::Completed;

                    for (auto &t : task->triggers) {
                        if (!t->isFinished()) {
                            queue_tasks.push(t);
                        }
                    }
                } catch (...) {
                    task->eptr = std::current_exception();
                    task->status = Task::TaskStatus::Failed;
                }
            }
        }
    }
}

MyExecutor::MyExecutor(int num_threads) : status(ExecutorStatus::Processing) {
    for (int i = 0; i < num_threads; i++) {
        thread_pool.emplace_back(&MyExecutor::kernel, this);
    }
}

void MyExecutor::submit(std::shared_ptr<Task> task) {
    if (status == ExecutorStatus::StartShutdown) {
        task->cancel();
    } else {
        if (!task->have_trigger) {
            if (task->have_time_trigger) {
                std::lock_guard<std::mutex> g(time_priority_mutex);
                time_priority.emplace(task->deadline, task);
            } else {
                queue_tasks.push(task);
            }
        }
    }
}

void MyExecutor::waitShutdown() {
    status = ExecutorStatus::WaitShutdown;
    while (true) {
        if (queue_tasks.empty()) {
            status = ExecutorStatus::Shutdown;
            for (auto &t : thread_pool) {
                if (t.joinable()) {
                    t.join();
                }
            }
            break;
        }
    }
}

void MyExecutor::startShutdown() {
    status = ExecutorStatus::StartShutdown;
}

MyExecutor::~MyExecutor() {
    status = ExecutorStatus::Shutdown;
    for (auto &t : thread_pool) {
        if (t.joinable()) {
            t.join();
        }
    }
}
