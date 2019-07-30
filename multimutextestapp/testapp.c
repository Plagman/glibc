#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


static pthread_mutex_t m[3];


static void *
tf (void *arg)
{
  int out;
  int ret = pthread_mutex_lock_any( m, 3, &out );

  printf("new thread ret %i out %i!\n", ret, out );

  sleep(5);
  printf("new thread done sleeping!\n");

  pthread_mutex_unlock( &m[out] );

  return NULL;
}


int main(int ac, char**av)
{
  pthread_mutex_init (&m[0], NULL);
  pthread_mutex_init (&m[1], NULL);
  pthread_mutex_init (&m[2], NULL);

  int out;

  int ret = pthread_mutex_lock_any( m, 3, &out );

  printf("main thread ret %i out %i!\n", ret, out );


  pthread_t th;
  if (pthread_create (&th, NULL, tf, NULL) != 0)
  {
    puts ("create failed");
    return 1;
  }

  sleep(1);

  ret = pthread_mutex_lock_any( m, 3, &out );

  printf("main thread ret %i out %i!\n", ret, out );

  sleep(1);

  struct timespec timeoutTime;
  clock_gettime(CLOCK_REALTIME, &timeoutTime);
  timeoutTime.tv_sec += 1;

  ret = pthread_mutex_timedlock_any( m, 3, &timeoutTime, &out );

  printf("main thread ret %i out %i!\n", ret, out );

  clock_gettime(CLOCK_REALTIME, &timeoutTime);
  timeoutTime.tv_sec += 3;

  ret = pthread_mutex_timedlock_any( m, 3, &timeoutTime, &out );

  printf("main thread ret %i out %i!\n", ret, out );

  pthread_mutex_unlock( &m[out] );
  pthread_mutex_unlock( &m[0] );
  pthread_mutex_unlock( &m[2] );

  printf("DONE unlock!\n");

  return 0;
}
