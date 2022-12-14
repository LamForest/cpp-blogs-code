/* Linuxthreads - a simple clone()-based implementation of Posix        */
/* threads for Linux.                                                   */
/* Copyright (C) 1996 Xavier Leroy (Xavier.Leroy@inria.fr)              */
/*                                                                      */
/* This program is free software; you can redistribute it and/or        */
/* modify it under the terms of the GNU Library General Public License  */
/* as published by the Free Software Foundation; either version 2       */
/* of the License, or (at your option) any later version.               */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU Library General Public License for more details.                 */

/* Semaphores a la POSIX 1003.1b */

#include "pthread.h"
#include "semaphore.h"
#include "internals.h"
#include "restart.h"


#if !defined HAS_COMPARE_AND_SWAP && !defined TEST_FOR_COMPARE_AND_SWAP
/* If we have no atomic compare and swap, fake it using an extra spinlock.  */

#include "spinlock.h"
static inline int sem_compare_and_swap(sem_t *sem, long oldval, long newval)
{
  int ret;
  acquire(&sem->sem_spinlock);
  ret = (sem->sem_status == oldval);
  if (ret) sem->sem_status = newval;
  release(&sem->sem_spinlock);
  return ret;
}

#elif defined TEST_FOR_COMPARE_AND_SWAP

#include "spinlock.h"
static int has_compare_and_swap = -1; /* to be determined at run-time */

static inline int sem_compare_and_swap(sem_t *sem, long oldval, long newval)
{
  int ret;

  if (has_compare_and_swap == 1)
    return __compare_and_swap(&sem->sem_status, oldval, newval);

  acquire(&sem->sem_spinlock);
  ret = (sem->sem_status == oldval);
  if (ret) sem->sem_status = newval;
  release(&sem->sem_spinlock);
  return ret;
}

#else
/* But if we do have an atomic compare and swap, use it!  */

static inline int sem_compare_and_swap(sem_t *sem, long oldval, long newval)
{
  return __compare_and_swap(&sem->sem_status, oldval, newval);
}

#endif


/* The state of a semaphore is represented by a long int encoding
   either the semaphore count if >= 0 and no thread is waiting on it,
   or the head of the list of threads waiting for the semaphore.
   To distinguish the two cases, we encode the semaphore count N
   as 2N+1, so that it has the lowest bit set.

   A sequence of sem_wait operations on a semaphore initialized to N
   result in the following successive states:
     2N+1, 2N-1, ..., 3, 1, &first_waiting_thread, &second_waiting_thread, ...
*/

static void sem_restart_list(pthread_descr waiting);

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
  if ((long)value > SEM_VALUE_MAX) {
    errno = EINVAL;
    return -1;
  }
  if (pshared) {
    errno = ENOSYS;
    return -1;
  }
#ifdef TEST_FOR_COMPARE_AND_SWAP
  if (has_compare_and_swap == -1) {
    has_compare_and_swap = compare_and_swap_is_available();
  }
#endif
#if !defined HAS_COMPARE_AND_SWAP || defined TEST_FOR_COMPARE_AND_SWAP
  sem->sem_spinlock = 0;
#endif
  sem->sem_status = ((long)value << 1) + 1;
  return 0;
}

int sem_wait(sem_t * sem)
{
  long oldstatus, newstatus;
  volatile pthread_descr self = thread_self();
  pthread_descr * th;

  while (1) {
    do {
      oldstatus = sem->sem_status;
      if ((oldstatus & 1) && (oldstatus != 1))
        newstatus = oldstatus - 2;
      else {
        newstatus = (long) self;
        self->p_nextwaiting = (pthread_descr) oldstatus;
      }
    }
    while (! sem_compare_and_swap(sem, oldstatus, newstatus));
    if (newstatus & 1)
      /* We got the semaphore. */
      return 0;
    /* Wait for sem_post or cancellation */
    suspend_with_cancellation(self);
    /* This is a cancellation point */
    if (self->p_canceled && self->p_cancelstate == PTHREAD_CANCEL_ENABLE) {
      /* Remove ourselves from the waiting list if we're still on it */
      /* First check if we're at the head of the list. */
      do {
        oldstatus = sem->sem_status;
        if (oldstatus != (long) self) break;
        newstatus = (long) self->p_nextwaiting;
      }
      while (! sem_compare_and_swap(sem, oldstatus, newstatus));
      /* Now, check if we're somewhere in the list.
         There's a race condition with sem_post here, but it does not matter:
         the net result is that at the time pthread_exit is called,
         self is no longer reachable from sem->sem_status. */
      if (oldstatus != (long) self && (oldstatus & 1) == 0) {
	for (th = &(((pthread_descr) oldstatus)->p_nextwaiting);
	     *th != NULL && *th != (pthread_descr) 1;
	     th = &((*th)->p_nextwaiting)) {
          if (*th == self) {
            *th = self->p_nextwaiting;
            break;
          }
        }
      }
      pthread_exit(PTHREAD_CANCELED);
    }
  }
}

int sem_trywait(sem_t * sem)
{
  long oldstatus, newstatus;

  do {
    oldstatus = sem->sem_status;
    if ((oldstatus & 1) == 0 || (oldstatus == 1)) {
      errno = EAGAIN;
      return -1;
    }
    newstatus = oldstatus - 2;
  }
  while (! sem_compare_and_swap(sem, oldstatus, newstatus));
  return 0;
}

int sem_post(sem_t * sem)
{
  long oldstatus, newstatus;

  do {
    oldstatus = sem->sem_status;
    if ((oldstatus & 1) == 0)
      newstatus = 3;
    else {
      if (oldstatus >= SEM_VALUE_MAX) {
        /* Overflow */
        errno = ERANGE;
        return -1;
      }
      newstatus = oldstatus + 2;
    }
  }
  while (! sem_compare_and_swap(sem, oldstatus, newstatus));
  if ((oldstatus & 1) == 0)
    sem_restart_list((pthread_descr) oldstatus);
  return 0;
}

int sem_getvalue(sem_t * sem, int * sval)
{
  long status = sem->sem_status;
  if (status & 1)
    *sval = (int)((unsigned long) status >> 1);
  else
    *sval = 0;
  return 0;
}

int sem_destroy(sem_t * sem)
{
  if ((sem->sem_status & 1) == 0) {
    errno = EBUSY;
    return -1;
  }
  return 0;
}

/* Auxiliary function for restarting all threads on a waiting list,
   in priority order. */

static void sem_restart_list(pthread_descr waiting)
{
  pthread_descr th, towake, *p;

  /* Sort list of waiting threads by decreasing priority (insertion sort) */
  towake = NULL;
  while (waiting != (pthread_descr) 1) {
    th = waiting;
    waiting = waiting->p_nextwaiting;
    p = &towake;
    while (*p != NULL && th->p_priority < (*p)->p_priority)
      p = &((*p)->p_nextwaiting);
    th->p_nextwaiting = *p;
    *p = th;
  }
  /* Wake up threads in priority order */
  while (towake != NULL) {
    th = towake;
    towake = towake->p_nextwaiting;
    th->p_nextwaiting = NULL;
    restart(th);
  }
}
