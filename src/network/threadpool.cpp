#include "threadpool.h"
#include <iostream>
#include <thread>

ThreadPool::ThreadPool(int threadNum) {
  for (int i = 0; i < threadNum; ++i) {
    m_workers.emplace_back([this]() {
      function<void()> task;
      while (m_queue.Pop(task)) {
        cout << "Worker Thread ID:" << this_thread::get_id() << endl;
        task();
      }
      cout << "Thread exit, ID:" << this_thread::get_id() << endl;
    });
  }
}

ThreadPool::~ThreadPool() {
  m_queue.Stop();
  for (auto &t : m_workers) {
    if (t.joinable())
      t.join();
  }
}