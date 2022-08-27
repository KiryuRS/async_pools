#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace regit::async
{

  class simple_timer final
  {
  public:
    simple_timer(const simple_timer&) = delete;
    simple_timer(simple_timer&&) = delete;
    simple_timer& operator=(const simple_timer&) = delete;
    simple_timer& operator=(simple_timer&&) = delete;

    simple_timer();
    ~simple_timer();

    using work_t = std::function<void()>;

    void post(std::chrono::seconds interval, work_t work);

  private:
    void worker_func();

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic_bool m_stopping, m_has_job, m_ready;
    std::chrono::seconds m_interval;
    std::thread m_thread;
    work_t m_work;

    std::once_flag m_ready_flag;
  };

  simple_timer::simple_timer()
    : m_thread{[this] { worker_func(); }}
    , m_has_job{false}
    , m_stopping{false}
    , m_ready{false}
  {
  }

  simple_timer::~simple_timer()
  {
    // TODO: Should have a more elegant way of stopping the timer
    // instead of waiting until it finishes
    while (m_has_job);

    m_stopping = true;
    if (m_thread.joinable())
      m_thread.join();
  }

  void simple_timer::post(std::chrono::seconds interval, work_t work)
  {
    // "locks" function to allow first thread to begin work before post
    // not ideal and probably should be changed to be more elegant?
    while (!m_ready);

    // not accepting job when an existing is already ongoing
    if (m_has_job)
      return;

    m_work = std::move(work);
    m_interval = interval;
    m_has_job = true;
    m_condition.notify_one();
  }

  void simple_timer::worker_func()
  {
    // we just need a single thread to be ready for work
    std::call_once(
      m_ready_flag,
      [this]
      {
        bool expected = false;
        m_ready.compare_exchange_strong(expected, true);
      });

    while (!m_stopping)
    {
      {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition.wait(lock, [this] { return m_stopping || m_has_job; });
      }

      if (m_stopping)
        break;

      // TODO: handle for cancel all and stopping
      // maybe use another condition variable?
      std::this_thread::sleep_for(m_interval);
      m_work();

      bool expected = true;
      m_has_job.compare_exchange_strong(expected, false);
    }
  }

} // namespace regit::async
