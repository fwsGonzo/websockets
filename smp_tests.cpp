#define DEBUG_SMP 1
#include <smp>
#include <cstdio>

struct alignas(SMP_ALIGN) taskdata_t
{
  int count = 0;
};
static SMP_ARRAY<taskdata_t> taskdata;

void recursive_task()
{
  SMP::global_lock();
  printf("Starting recurring tasks on %d\n", SMP::cpu_id());
  SMP::global_unlock();

  SMP::add_bsp_task(
    [x = SMP::cpu_id()] () {
      SMP::global_lock();
      printf("%d: Back on main CPU!\n", x);
      SMP::global_unlock();
      SMP::add_task(
        [x] () {
          SMP::global_lock();
          printf("%d: Back on my CPU (%d)!\n", x, SMP::cpu_id());
          SMP::global_unlock();
          // go back to main CPU
          recursive_task();
        }, x);
      SMP::signal(x);
    });
}

static const int ALLOC_LEN = 1024*1024*1;

void allocating_task()
{
  // alloc data with cpuid as member
  auto* y = new char[ALLOC_LEN];
  for (int i = 0; i < ALLOC_LEN; i++) y[i] = SMP::cpu_id();

  SMP::add_bsp_task(
    [x = SMP::cpu_id(), y] ()
    {
      // verify and delete data
      for (int i = 0; i < ALLOC_LEN; i++)
        assert(y[i] == x);
      delete[] y;
      // reallocate, do it again
      auto* y = new char[ALLOC_LEN];
      memset(y, x, ALLOC_LEN);

      SMP::add_task(
        [x, y] () {
          assert(x == SMP::cpu_id());
          // verify and deallocate data
          for (int i = 0; i < ALLOC_LEN; i++)
            assert(y[i] == x);
          delete[] y;
          // show the task finished successfully
          PER_CPU(taskdata).count++;
          SMP::global_lock();
          printf("%d: Finished task successfully %d times!\n",
                SMP::cpu_id(), PER_CPU(taskdata).count);
          SMP::global_unlock();
          // go back to main CPU
          allocating_task();
        }, x);
      SMP::signal(x);
    });
}

static spinlock_t testlock = 0;
void per_cpu_task()
{
  SMP::add_bsp_task(
    [x = SMP::cpu_id()] () {
      // verify and delete data
      lock(testlock);
      assert(&PER_CPU(taskdata) == &taskdata[0]);
      unlock(testlock);

      SMP::add_task(
        [] {
          lock(testlock);
          assert(&PER_CPU(taskdata) == &taskdata[SMP::cpu_id()]);
          unlock(testlock);
          // show the task finished successfully
          PER_CPU(taskdata).count++;
          SMP::global_lock();
          printf("%d: Finished task successfully %d times!\n",
                SMP::cpu_id(), PER_CPU(taskdata).count);
          SMP::global_unlock();
          // go back to main CPU
          per_cpu_task();
        }, x);
      SMP::signal(x);
    });
}
