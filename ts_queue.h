#ifndef CPP_COURSE_TS_QUEUE_H
#define CPP_COURSE_TS_QUEUE_H

#include <queue>
#include <mutex>

template<typename T>
class ts_queue {
public:
    bool empty() {
        std::lock_guard<std::mutex> guard(mtx);
        return que.empty();
    }

    template <typename U>
    void push(U&& val) {
        std::lock_guard<std::mutex> guard(mtx);
        que.push(std::forward<U>(val));
    }

    T pop() {
        std::lock_guard<std::mutex> guard(mtx);
        if (que.empty()) {
            return T();
        }
        auto store = que.front();
        que.pop();
        return store;
    }

private:
    std::queue<T> que;
    std::mutex mtx;
};

#endif //CPP_COURSE_TS_QUEUE_H
