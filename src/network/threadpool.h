#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
using namespace std;

// 阻塞队列模板类
template <typename T> class BlockQueue {
public:
  void Push(T task);
  bool Pop(T &task);
  void Stop();

private:
  queue<T> m_queue;
  mutex m_mtx;
  condition_variable m_cv;
  bool m_stop = false;
};

template <typename T> void BlockQueue<T>::Push(T task) {
  lock_guard<mutex> lock(m_mtx);
  m_queue.push(task);
  m_cv.notify_one();
}

template <typename T> bool BlockQueue<T>::Pop(T &task) {
  unique_lock<mutex> lock(m_mtx);
  m_cv.wait(lock, [this]() { return !m_queue.empty() || m_stop; });
  if (m_stop && m_queue.empty()) {
    return false;
  }
  task = m_queue.front();
  m_queue.pop();
  return true;
}

template <typename T> void BlockQueue<T>::Stop() {
  lock_guard<mutex> lock(m_mtx);
  m_stop = true;
  m_cv.notify_all();
}

// 线程池类声明
class ThreadPool {
public:
  ThreadPool(int threadNum);
  ~ThreadPool();
  template <typename Func> void Submit(Func func);

private:
  vector<thread> m_workers;
  BlockQueue<function<void()>> m_queue;
};

// Submit模板函数，保留在头文件
template <typename Func> void ThreadPool::Submit(Func func) {
  m_queue.Push(func);
}

#endif