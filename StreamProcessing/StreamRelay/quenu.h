#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

template<typename T>
class SafeQueue 
{
    std::queue<T> q;
    mutable std::mutex mtx;
    std::condition_variable cv;
    size_t max_size;

public:
    explicit SafeQueue(size_t size) : max_size(size) {}

    // 禁止拷贝
    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    // 允许移动
    SafeQueue(SafeQueue&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mtx);
        q = std::move(other.q);
        max_size = other.max_size;
    }

    SafeQueue& operator=(SafeQueue&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(mtx, other.mtx);
            q = std::move(other.q);
            max_size = other.max_size;
        }
        return *this;
    }

    // push 左值
    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mtx);
        if (q.size() >= max_size) {
            q.pop();  // 丢弃最老的元素
        }
        q.push(item);
        cv.notify_one();
    }

    // push 右值（避免多余拷贝）
    void push(T&& item) {
        std::unique_lock<std::mutex> lock(mtx);
        if (q.size() >= max_size) {
            q.pop();
        }
        q.push(std::move(item));
        cv.notify_one();
    }

    // pop，如果 block=true 阻塞等待，否则不阻塞
    bool pop(T& item, bool block = true, int timeout_ms = 0) 
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (block) 
        {
            if (timeout_ms > 0) 
            {
                if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]{ return !q.empty(); })) 
                {
                    return false; // 超时
                }
            } 
            else 
            {
                cv.wait(lock, [this]{ return !q.empty(); });
            }
        } 
        else if (q.empty()) {
            return false;
        }

        if (!q.empty()) {
            item = std::move(q.front());
            q.pop();
            return true;
        }
        return false;
    }

    bool empty() const {
        std::unique_lock<std::mutex> lock(mtx);
        return q.empty();
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(mtx);
        return q.size();
    }
};
