/* Copyright (C) 2002-2019 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <not-cancel.h>
#include "pthreadP.h"
#include <atomic.h>
#include <lowlevellock.h>
#include <stap-probe.h>

/* TODO: this probably comes from a kernel header when upstream? */
struct futex_wait_block {
	int *uaddr;
	int val;
	int bitset;
} __attribute__((packed));

int
__pthread_mutex_timedlock_any (pthread_mutex_t *mutexlist, int mutexcount,
			       const struct timespec *abstime, int *outlocked)
{
  /* This requires futex support */
#ifndef __NR_futex
  return ENOTSUP;
#endif

  if (mutexlist == NULL)
  {
    /* User is asking us if kernel supports the feature. */

    /* TODO: how does one check if supported?
     * I was thinking of trying the ioctl once and then returning the static
     * cached value, is that OK?
     */
    return 0;
  }

  if (mutexlist != NULL && mutexcount <= 0)
    return EINVAL;

  if (outlocked == NULL)
    return EINVAL;

  int type = PTHREAD_MUTEX_TYPE (mutexlist);

  for (int i = 1; i < mutexcount; i++)
  {
    /* Types have to match, since the PRIVATE flag is OP-global. */
    if (PTHREAD_MUTEX_TYPE (&mutexlist[i]) != type)
      return EINVAL;
  }

  int kind = type & PTHREAD_MUTEX_KIND_MASK_NP;

  /* TODO: implement recursive, errorcheck and adaptive. */
  if (kind != PTHREAD_MUTEX_NORMAL)
    return EINVAL;

  /* TODO: implement robust. */
  if (type & PTHREAD_MUTEX_ROBUST_NORMAL_NP)
    return EINVAL;

  /* TODO: implement PI. */
  if (type & PTHREAD_MUTEX_PRIO_INHERIT_NP)
    return EINVAL;

  /* TODO: implement PP. */
  if (type & PTHREAD_MUTEX_PRIO_PROTECT_NP)
    return EINVAL;

  pid_t id = THREAD_GETMEM (THREAD_SELF, tid);
  int result;

  result = -1;

  for (int i = 0; i < mutexcount; i++)
  {
    if (__lll_trylock (&mutexlist[i].__data.__lock) == 0)
    {
      result = i;
      break;
    }
  }

  while (result == -1)
  {
    for (int i = 0; i < mutexcount; i++)
    {
      int oldval = atomic_exchange_acq (&mutexlist[i].__data.__lock, 2);

      if (oldval == 0)
      {
	result = i;
	break;
      }
    }

    if (result == -1)
    {
      /* Couldn't get one of the locks immediately, we have to sleep now. */
      struct timespec *timeout = NULL;
      struct timespec rt;

      if (abstime != NULL)
      {
	/* Reject invalid timeouts. */
	if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
	  return EINVAL;

	struct timeval tv;

	/* Get the current time.  */
	(void) __gettimeofday (&tv, NULL);

	/* Compute relative timeout.  */
	rt.tv_sec = abstime->tv_sec - tv.tv_sec;
	rt.tv_nsec = abstime->tv_nsec - tv.tv_usec * 1000;
	if (rt.tv_nsec < 0)
	{
	  rt.tv_nsec += 1000000000;
	  --rt.tv_sec;
	}

	if (rt.tv_sec < 0)
	  return ETIMEDOUT;

	timeout = &rt;
      }

      struct futex_wait_block waitblock[mutexcount];

      for (int i = 0; i < mutexcount; i++)
      {
	waitblock[i].uaddr = &mutexlist[i].__data.__lock;
	waitblock[i].val = 2;
	waitblock[i].bitset = ~0;
      }

      long int __ret;

      /* Safe to use the flag for the first one, since all their types match. */
      int private_flag = PTHREAD_MUTEX_PSHARED (&mutexlist[0]);

      __ret = lll_futex_timed_wait_multiple (waitblock, mutexcount, timeout,
					    private_flag);

      if (__ret < 0)
	return -__ret; /* TODO is this correct? */

      /* Have slept, try grabbing the one that woke us up? */
      if (atomic_exchange_acq (&mutexlist[__ret].__data.__lock, 2) == 0)
      {
	/* We got it, done, loop will end below. */
	result = __ret;
      }
    }
  }

  if (result != -1)
  {
    /* Record the ownership. */
    mutexlist[result].__data.__owner = id;
    ++mutexlist[result].__data.__nusers;

    /* Let the user know which mutex is now locked. */
    *outlocked = result;

    result = 0;
  }

  return result;
}

weak_alias (__pthread_mutex_timedlock_any, pthread_mutex_timedlock_any)
