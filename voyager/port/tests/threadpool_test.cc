#include "voyager/port/threadpool.h"
#include <stdio.h>
#include <inttypes.h>
#include "voyager/port/currentthread.h"
#include "voyager/port/countdownlatch.h"

namespace voyager {
namespace port {

void Print() {
  printf("tid=%" PRIu64", threadname=%s\n", 
         CurrentThread::Tid(), CurrentThread::ThreadName());
}

void Test(int poolsize) {
  ThreadPool pool(poolsize);
  pool.Start();
  for (int i = 0; i < 1000; ++i) {
    pool.AddTask(Print);
  }
  CountDownLatch latch(1);
  pool.AddTask(std::bind(&CountDownLatch::CountDown, &latch));
  latch.Wait();
  pool.Stop();
  printf("test end!\n");
  printf("%zd\n", pool.TaskSize());
}

}  // namespace port
}  // namespace voyager

int main(int argc, char** argv) {
  voyager::port::Test(4);
  return 0;
}
