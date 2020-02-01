#include <memory>
#include <chrono>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <future>
#include <condition_variable>

#include "ts_queue.h"

class Task : public std::enable_shared_from_this<Task> {
public:
    virtual ~Task() = default;

    virtual void run() = 0;

    void addDependency(std::shared_ptr<Task> dependency);

    void addTrigger(std::shared_ptr<Task> trigger);

    void setTimeTrigger(std::chrono::system_clock::time_point at);

    bool isAwaiting();

    bool isProcessing();

    // Task::run() completed without throwing exception
    bool isCompleted();

    // Task::run() throwed exception
    bool isFailed();

    // Task was canceled
    bool isCanceled();

    // Task either completed, failed or was canceled
    bool isFinished();

    std::exception_ptr getError();

    void cancel();

    void wait();

protected:
    friend class MyExecutor;

    enum class TaskStatus {
        Awaiting,
        Processing,
        Completed,
        Failed,
        Canceled
    };

    std::atomic<TaskStatus> status{TaskStatus::Awaiting};

    std::vector<std::shared_ptr<Task>> dependencies;

    std::atomic<bool> have_trigger = false;
    std::vector<std::shared_ptr<Task>> triggers;

    std::atomic<bool> have_time_trigger;
    std::chrono::system_clock::time_point deadline;

    std::exception_ptr eptr;
};

template<class T>
class Future;

template<class T>
using FuturePtr = std::shared_ptr<Future<T>>;

// Used instead of void in generic code
struct Unit {};

class Executor {
public:
    virtual ~Executor() = default;

    virtual void submit(std::shared_ptr<Task> task) = 0;

    virtual void startShutdown() = 0;
    virtual void waitShutdown() = 0;

    template<class T>
    FuturePtr<T> invoke(std::function<T()> fn) {
        FuturePtr<T> task = std::make_shared<Future<T>>(fn);
        submit(task);
        return task;
    }

    template<class Y, class T>
    FuturePtr<Y> then(FuturePtr<T> input, std::function<Y()> fn) {
        FuturePtr<Y> task = std::make_shared<Future<Y>>(fn);
        task->addTrigger(input);
        submit(task);
        return task;
    }

    template<class T>
    FuturePtr<std::vector<T>> whenAll(std::vector<FuturePtr<T>> all);

    template<class T>
    FuturePtr<T> whenFirst(std::vector<FuturePtr<T>> all);

    template<class T>
    FuturePtr<std::vector<T>> whenAllBeforeDeadline(std::vector<FuturePtr<T>> all,
                                                    std::chrono::system_clock::time_point deadline);
};

class MyExecutor : public Executor {
    template<typename U> friend
    class ts_queue;

public:
    explicit MyExecutor(int num_threads);

    ~MyExecutor();

    void submit(std::shared_ptr<Task> task) override;

    void startShutdown() override;

    void waitShutdown() override;

private:
    void kernel();

    enum class ExecutorStatus {
        Processing,
        StartShutdown,
        WaitShutdown,
        Shutdown,
    };
    std::atomic<ExecutorStatus> status{ExecutorStatus::Processing};

    ts_queue<std::shared_ptr<Task>> queue_tasks;

    std::mutex time_priority_mutex;
    std::priority_queue<
            std::pair<std::chrono::system_clock::time_point, std::shared_ptr<Task>>,
            std::vector<std::pair<std::chrono::system_clock::time_point, std::shared_ptr<Task>>>,
            std::greater<>> time_priority;

    std::vector<std::thread> thread_pool;
};

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads);

template<class T>
class Future : public Task {
public:
    T get() {
        this->wait();

        if (this->status == TaskStatus::Failed) {
            std::rethrow_exception(eptr);
        }

        return result;
    }

    void run() override {
        result = fn();
    }

    T result;
    std::function<T()> fn;
    explicit Future(std::function<T()> fn) : fn(fn) {}
};
