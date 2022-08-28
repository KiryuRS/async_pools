#include "../generic_pool.h"
#include <iostream>
#include <chrono>

using namespace std::chrono_literals;

namespace
{
  void func()
  {
    std::this_thread::sleep_for(1s);
    std::cout << "Hello World" << std::endl;
  }
}

int main(void)
{
  size_t num_threads = 3;
  regit::async::generic_thread_pool gp1{num_threads};
  gp1.start();

  for (auto i = 0; i != num_threads * 2; ++i)
    gp1.post(func);
  std::this_thread::sleep_for(10s);
}