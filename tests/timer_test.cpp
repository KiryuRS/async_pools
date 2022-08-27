#include "../timer.h"
#include <iostream>

namespace
{
  void func()
  {
    std::cout << "Hello World!" << std::endl;
  }
}

int main(void)
{
  regit::async::simple_timer timer;
  timer.post(
    std::chrono::seconds{1},
    func);
}