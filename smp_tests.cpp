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

#include <stdexcept>
void exceptions_task()
{
  // verify and delete data
  bool VVV = false;

  try
  {
    SMP_PRINT("CPU %d throwing exception...\n", SMP::cpu_id());
    throw std::runtime_error("A massive failure happened");
  }
  catch (std::exception& e)
  {
    SMP_PRINT("CPU %d caught exception: %s\n", SMP::cpu_id(), e.what());
    VVV = true;
  }
  catch (...)
  {
    SMP_PRINT("CPU %d caught unknown exception\n", SMP::cpu_id());
    VVV = true;
  }
  assert(VVV);
  SMP_PRINT("Success on CPU %d\n", SMP::cpu_id());
}

void tls_task()
{
  SMP_PRINT("Work starting on CPU %d\n", SMP::cpu_id());
  thread_local int tdata_value = 1;
  thread_local int tbss_value = 0;
  for (int i = 0; i < 100; i++)
  {
    tdata_value++;
  }
  for (int i = 0; i < 100; i++)
  {
    tbss_value++;
  }
  SMP_PRINT("Work finishing on CPU %d\n", SMP::cpu_id());
  lock(testlock);
  assert(tdata_value == 101);
  assert(tbss_value == 100);
  unlock(testlock);
}
