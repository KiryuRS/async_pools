#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

// Compiled with gcc 9.4.0
// Flags: -std=c++17 -pthread -O3

namespace regit::async
{
  namespace detail
  {
    using work_t = std::function<void()>;

    class naive_thread_wrapper final
    {
    public:
      template <
        typename ... ArgsT,
        // prevent anther instance of naive_thread_wrapper from calling this constructor
        std::enable_if_t<std::is_constructible_v<std::thread, ArgsT...>, int> = 0>
      naive_thread_wrapper(ArgsT&& ... work) noexcept
        : m_thread{std::forward<ArgsT>(work)...}
      {
      }

      naive_thread_wrapper(naive_thread_wrapper&&) noexcept = default;

      ~naive_thread_wrapper()
      {
        if (m_thread.joinable())
          m_thread.join();
      }

    private:
      std::thread m_thread;
    };

    class work_policy
    {
    public:
      void begin_work(const work_t& work) noexcept
      {
        try
        {
          if (work)
            work();
        }
        catch (const std::exception&)
        {
          // work failed for some reason, but let's just move on
        }
      }
    };
  } // namespace detail

  template <typename ThreadT = detail::naive_thread_wrapper, typename WorkPolicyT = detail::work_policy>
  class generic_thread_pool final : private WorkPolicyT
  {
  public:
    // Generally thread pools shouldnt be moved or copyable to prevent unecessary undefined behaviors
    generic_thread_pool(const generic_thread_pool&) = delete;
    generic_thread_pool(generic_thread_pool&&) = delete;
    generic_thread_pool& operator=(const generic_thread_pool&) = delete;
    generic_thread_pool& operator=(generic_thread_pool&&) = delete;

    using worker_t = std::function<void()>;
    using thread_factory_t = std::function<ThreadT(worker_t)>;
    using thread_t = ThreadT;

    generic_thread_pool(size_t size) noexcept;
    template <typename ThreadFactoryT>
    generic_thread_pool(size_t size, ThreadFactoryT&& threadFactory) noexcept;
    ~generic_thread_pool();

    void start();
    void stop();
    void post(detail::work_t work);

  private:
    void worker_func();

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic_bool m_stopping;
    std::queue<detail::work_t> m_jobs;
    std::vector<ThreadT> m_threads;
    thread_factory_t m_threadFactory;

    const size_t m_poolSize;
    std::once_flag m_init_flag, m_deinit_flag, m_ready_flag;
  };

  template <typename ThreadT, typename WorkPolicyT>
  generic_thread_pool<ThreadT, WorkPolicyT>::generic_thread_pool(size_t size) noexcept
    : generic_thread_pool{
        size,
        [](worker_t joinPool) { return ThreadT{std::move(joinPool)}; }}
  {
  }

  template <typename ThreadT, typename WorkPolicyT>
  template <typename ThreadFactoryT>
  generic_thread_pool<ThreadT, WorkPolicyT>::generic_thread_pool(size_t size, ThreadFactoryT&& threadFactory) noexcept
    : m_poolSize{size}
    , m_threadFactory{std::forward<ThreadFactoryT>(threadFactory)}
    , m_stopping{false}
  {
    static_assert(std::is_same_v<ThreadT, std::result_of_t<ThreadFactoryT(std::function<void()>)>>);
  }

  template <typename ThreadT, typename WorkPolicyT>
  generic_thread_pool<ThreadT, WorkPolicyT>::~generic_thread_pool()
  {
    stop();
  }

  template <typename ThreadT, typename WorkPolicyT>
  void generic_thread_pool<ThreadT, WorkPolicyT>::start()
  {
    std::call_once(
      m_init_flag,
      [this] ()
      {
        m_threads.resize(m_poolSize);
        for (auto i = 0; i != m_poolSize; ++i)
          m_threads.emplace_back(m_threadFactory([this] { worker_func(); }));
      });
  }

  template <typename ThreadT, typename WorkPolicyT>
  void generic_thread_pool<ThreadT, WorkPolicyT>::stop()
  {
    std::call_once(
      m_deinit_flag,
      [this] ()
      {
        m_stopping = true;
        m_condition.notify_all();

        m_threads.clear();
      });
  }

  template <typename ThreadT, typename WorkPolicyT>
  void generic_thread_pool<ThreadT, WorkPolicyT>::post(detail::work_t work)
  {
    {
      std::lock_guard<std::mutex> lock{m_mutex};
      m_jobs.emplace(std::move(work));
    }
    m_condition.notify_one();
  }

  template <typename ThreadT, typename WorkPolicyT>
  void generic_thread_pool<ThreadT, WorkPolicyT>::worker_func()
  {
    while (!m_stopping)
    {
      detail::work_t work;
      {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });

        if (m_stopping)
          break;

        work = m_jobs.front();
        m_jobs.pop();
      }

      WorkPolicyT::begin_work(work);
    }
  }

  // Class Template Argument Deduction (CTAD)
  // https://en.cppreference.com/w/cpp/language/class_template_argument_deduction
  template <typename ThreadT, typename WorkPolicyT>
  generic_thread_pool(size_t) -> generic_thread_pool<ThreadT, WorkPolicyT>;

} // namespace regit::async
