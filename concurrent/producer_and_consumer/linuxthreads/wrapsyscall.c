/* wrapsyscall - wrapper arpund system calls to provide cancelation points.
   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>


#ifndef PIC
/* We need a hook to force this file to be linked in when static
   libpthread is used.  */
const int __pthread_provide_wrappers = 0;
#endif


#define CANCELABLE_SYSCALL(res_type, name, param_list, params) \
res_type __libc_##name param_list;					      \
res_type								      \
name param_list								      \
{									      \
  res_type result;							      \
  int oldtype;								      \
  pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);	      \
  result = __libc_##name params;					      \
  pthread_setcanceltype (oldtype, NULL);				      \
  return result;							      \
}

#define CANCELABLE_SYSCALL_VA(res_type, name, param_list, params, last_arg) \
res_type __libc_##name param_list;					      \
res_type								      \
name param_list								      \
{									      \
  res_type result;							      \
  int oldtype;								      \
  va_list ap;								      \
  pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);	      \
  va_start (ap, last_arg);						      \
  result = __libc_##name params;					      \
  va_end (ap);								      \
  pthread_setcanceltype (oldtype, NULL);				      \
  return result;							      \
}


/* close(2).  */
CANCELABLE_SYSCALL (int, close, (int fd), (fd))
strong_alias (close, __close)


/* fcntl(2).  */
CANCELABLE_SYSCALL_VA (int, fcntl, (int fd, int cmd, ...),
		       (fd, cmd, va_arg (ap, long int)), cmd)
strong_alias (fcntl, __fcntl)


/* fsync(2).  */
CANCELABLE_SYSCALL (int, fsync, (int fd), (fd))


/* lseek(2).  */
CANCELABLE_SYSCALL (off_t, lseek, (int fd, off_t offset, int whence),
		    (fd, offset, whence))
strong_alias (lseek, __lseek)


/* msync(2).  */
CANCELABLE_SYSCALL (int, msync, (const void *start, size_t length, int flags),
		    (start, length, flags))


/* nanosleep(2).  */
CANCELABLE_SYSCALL (int, nanosleep, (const struct timespec *requested_time,
				     struct timespec *remaining),
		    (requested_time, remaining))


/* open(2).  */
CANCELABLE_SYSCALL_VA (int, open, (const char *pathname, int flags, ...),
		       (pathname, flags, va_arg (ap, mode_t)), flags)
strong_alias (open, __open)


/* pause(2).  */
CANCELABLE_SYSCALL (int, pause, (void), ())


/* read(2).  */
CANCELABLE_SYSCALL (ssize_t, read, (int fd, void *buf, size_t count),
		    (fd, buf, count))
strong_alias (read, __read)


/* system(3).  */
CANCELABLE_SYSCALL (int, system, (const char *line), (line))


/* tcdrain(2).  */
CANCELABLE_SYSCALL (int, tcdrain, (int fd), (fd))


/* wait(2).  */
CANCELABLE_SYSCALL (__pid_t, wait, (__WAIT_STATUS_DEFN stat_loc), (stat_loc))
strong_alias (wait, __wait)


/* waitpid(2).  */
CANCELABLE_SYSCALL (__pid_t, waitpid, (__pid_t pid, int *stat_loc,
				       int options),
		    (pid, stat_loc, options))


/* write(2).  */
CANCELABLE_SYSCALL (ssize_t, write, (int fd, const void *buf, size_t n),
		    (fd, buf, n))
strong_alias (write, __write)


/* The following system calls are thread cancellation points specified
   in XNS.  */

/* accept(2).  */
CANCELABLE_SYSCALL (int, accept, (int fd, __SOCKADDR_ARG addr,
				  socklen_t *addr_len),
		    (fd, addr, addr_len))

/* connect(2).  */
CANCELABLE_SYSCALL (int, connect, (int fd, __CONST_SOCKADDR_ARG addr,
				   socklen_t len),
		    (fd, addr, len))
strong_alias (connect, __connect)

/* recv(2).  */
CANCELABLE_SYSCALL (int, recv, (int fd, __ptr_t buf, size_t n, int flags),
		    (fd, buf, n, flags))

/* recvfrom(2).  */
CANCELABLE_SYSCALL (int, recvfrom, (int fd, __ptr_t buf, size_t n, int flags,
				    __SOCKADDR_ARG addr, socklen_t *addr_len),
		    (fd, buf, n, flags, addr, addr_len))

/* recvmsg(2).  */
CANCELABLE_SYSCALL (int, recvmsg, (int fd, struct msghdr *message, int flags),
		    (fd, message, flags))

/* send(2).  */
CANCELABLE_SYSCALL (int, send, (int fd, const __ptr_t buf, size_t n,
				int flags),
		    (fd, buf, n, flags))
strong_alias (send, __send)

/* sendmsg(2).  */
CANCELABLE_SYSCALL (int, sendmsg, (int fd, const struct msghdr *message,
				   int flags),
		    (fd, message, flags))

/* sendto(2).  */
CANCELABLE_SYSCALL (int, sendto, (int fd, const __ptr_t buf, size_t n,
				  int flags, __CONST_SOCKADDR_ARG addr,
				  socklen_t addr_len),
		    (fd, buf, n, flags, addr, addr_len))
