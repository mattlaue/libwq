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

/*
  See also:
    ftp://ftp.nodc.noaa.gov/nodc/archive/arc0019/0050970/1.1/data/0-data/AT015L06/adcp/www/programs/logging/serial_c/cooper_serial_c/serial_c/clocks.c
*/
#ifndef __WIN32
#include <time.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_error.h>
#include <stdio.h>

static clock_serv_t cclock = 0;

int
wq_gettime(struct timespec *tp)
{
   kern_return_t ret;
   mach_timespec_t mach_t;

      if (cclock == 0)
      {
          ret = host_get_clock_service(mach_host_self(), CALENDAR_CLOCK,
                                       &cclock);
          if (ret != KERN_SUCCESS) {
              return -1;
          }
      }

      ret = clock_get_time(cclock, &mach_t);
      if (ret != KERN_SUCCESS) {
          return -1;
      }

      tp->tv_sec = mach_t.tv_sec;
      tp->tv_nsec = mach_t.tv_nsec;
      return 0;
}

#else

int
wq_gettime(struct timespec *tp)
{
    return clock_gettime(CLOCK_REALTIME, tp);
}

#endif
#endif
