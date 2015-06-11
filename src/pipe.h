/* Copyright (C) 2012 Akiri Solutions, Inc.
   http://www.akirisolutions.com

   wq - A general purpose work-queue library for C/C++.

   The logr package is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The logr package is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the logr source code; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */
#ifndef __PIPE_H__
#define __PIPE_H__

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#ifdef __WIN32
static inline int
pipe(PIPE *pfds)
{
    BOOL b;

    b = CreatePipe(&pfds[WORKQUEUE_READ_PIPE],
                   &pfds[WORKQUEUE_WRITE_PIPE],
                   NULL, 0);
    return (b) ? 0 : -1;
}
#endif


static inline ssize_t
read_pipe(PIPE p, void *buf, size_t count)
{
#ifdef __WIN32
    BOOL b;
    DWORD n;

    b = ReadFile(p, buf, (DWORD)count, &n, NULL);
    if (b) {
        if (n == 0) {
            errno = EWOULDBLOCK;
            return -1;
        }
        return (ssize_t)n;
    }

    errno = GetLastError();
    if (errno == ERROR_NO_DATA) {
        return 0;
    }
    return -1;
#else
    return read(p, buf, count);
#endif
}

static inline ssize_t
write_pipe(PIPE p, const void *buf, size_t count)
{
#ifdef __WIN32
    BOOL b;
    DWORD n;

    b = WriteFile(p, buf, (DWORD)count, &n, NULL);
    if (b) {
        if (n == 0) {
            errno = EWOULDBLOCK;
            return -1;
        }
        return (ssize_t)n;
    }
    errno = GetLastError();
    return -1;
#else
    return write(p, buf, count);
#endif
}

static inline void
close_pipe(PIPE p)
{
#ifdef __WIN32
    CloseHandle(p);
#else
    close(p);
#endif
}

#ifdef __WIN32
static inline int
pipe_set_nonblocking(PIPE p)
{
    DWORD state;
    BOOL b;

    b = GetNamedPipeHandleState(p, &state, NULL, NULL, NULL, NULL, 0);
    if (!b) return -1;

    state |= PIPE_NOWAIT;

    b = SetNamedPipeHandleState(p, &state, NULL, NULL);
    if (!b) return -1;

    return 0;
}

#else /* __WIN32 */

static int
pipe_set_nonblocking(PIPE p)
{
    int flags, rc;

    flags = fcntl(p, F_GETFL);
    if (flags < 0) return -1;

    flags |= O_NONBLOCK;
    rc = fcntl(p, F_SETFL, flags);
    if (rc < 0) return -1;

    return 0;
}

#endif

#endif /* __PIPE_H__ */
