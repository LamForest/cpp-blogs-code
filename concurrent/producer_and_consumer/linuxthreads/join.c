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

/* Thread termination and joining */

#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include "pthread.h"
#include "internals.h"
#include "spinlock.h"
#include "restart.h"

void pthread_exit(void * retval)
{
  pthread_descr self = thread_self();
  pthread_descr joining;
  struct pthread_request request;

  /* Reset the cancellation flag to avoid looping if the cleanup handlers
     contain cancellation points */
  self->p_canceled = 0;
  /* Call cleanup functions and destroy the thread-specific data */
  __pthread_perform_cleanup();
  __pthread_destroy_specifics();
  /* Store return value */
  acquire(self->p_spinlock);
  self->p_retval = retval;
  /* Say that we've terminated */
  self->p_terminated = 1;
  /* See if someone is joining on us */
  joining = self->p_joining;
  release(self->p_spinlock);
  /* Restart joining thread if any */
  if (joining != NULL) restart(joining);
  /* If this is the initial thread, block until all threads have terminated.
     If another thread calls exit, we'll be terminated from our signal
     handler. */
  if (self == __pthread_main_thread && __pthread_manager_request >= 0) {
    request.req_thread = self;
    request.req_kind = REQ_MAIN_THREAD_EXIT;
    __libc_write(__pthread_manager_request, (char *)&request, sizeof(request));
    suspend(self);
  }
  /* Exit the process (but don't flush stdio streams, and don't run
     atexit functions). */
  _exit(0);
}

int pthread_join(pthread_t thread_id, void ** thread_return)
{
  volatile pthread_descr self = thread_self();
  struct pthread_request request;
  pthread_handle handle = thread_handle(thread_id);
  pthread_descr th;

  acquire(&handle->h_spinlock);
  if (invalid_handle(handle, thread_id)) {
    release(&handle->h_spinlock);
    return ESRCH;
  }
  th = handle->h_descr;
  if (th == self) {
    release(&handle->h_spinlock);
    return EDEADLK;
  }
  /* If detached or already joined, error */
  if (th->p_detached || th->p_joining != NULL) {
    release(&handle->h_spinlock);
    return EINVAL;
  }
  /* If not terminated yet, suspend ourselves. */
  if (! th->p_terminated) {
    th->p_joining = self;
    release(&handle->h_spinlock);
    suspend_with_cancellation(self);
    /* This is a cancellation point */
    if (self->p_canceled && self->p_cancelstate == PTHREAD_CANCEL_ENABLE) {
      th->p_joining = NULL;
      pthread_exit(PTHREAD_CANCELED);
    }
    acquire(&handle->h_spinlock);
  }
  /* Get return value */
  if (thread_return != NULL) *thread_return = th->p_retval;
  release(&handle->h_spinlock);
  /* Send notification to thread manager */
  if (__pthread_manager_request >= 0) {
    request.req_thread = self;
    request.req_kind = REQ_FREE;
    request.req_args.free.thread = th;
    __libc_write(__pthread_manager_request,
		 (char *) &request, sizeof(request));
  }
  return 0;
}

int pthread_detach(pthread_t thread_id)
{
  int terminated;
  struct pthread_request request;
  pthread_handle handle = thread_handle(thread_id);
  pthread_descr th;

  acquire(&handle->h_spinlock);
  if (invalid_handle(handle, thread_id)) {
    release(&handle->h_spinlock);
    return ESRCH;
  }
  th = handle->h_descr;
  /* If already detached, error */
  if (th->p_detached) {
    release(&handle->h_spinlock);
    return EINVAL;
  }
  /* If already joining, don't do anything. */
  if (th->p_joining != NULL) {
    release(&handle->h_spinlock);
    return 0;
  }
  /* Mark as detached */
  th->p_detached = 1;
  terminated = th->p_terminated;
  release(&handle->h_spinlock);
  /* If already terminated, notify thread manager to reclaim resources */
  if (terminated && __pthread_manager_request >= 0) {
    request.req_thread = thread_self();
    request.req_kind = REQ_FREE;
    request.req_args.free.thread = th;
    __libc_write(__pthread_manager_request,
		 (char *) &request, sizeof(request));
  }
  return 0;
}
