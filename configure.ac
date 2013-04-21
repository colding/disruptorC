#  
#  Copyright (C) 2012 by Jules Colding <jcolding@gmail.com>.
#
#  All Rights Reserved.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without any warranty.

AC_PREREQ([2.68])
AC_INIT([disruptorC],[0.5.0],[jcolding@gmail.com])
AC_COPYRIGHT([Copyright (C) 2012 Jules Colding. All Rights Reserved.])

# For GCC extensions
AC_USE_SYSTEM_EXTENSIONS

AC_CONFIG_SRCDIR([bootstrap])
AC_CONFIG_HEADERS([ac_config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

dnl Standard checks
AC_LANG([C])
AC_PROG_CXX

dnl Initialize libtool
LT_INIT([disable-shared])
AC_SUBST([LIBTOOL_DEPS])

# unset -g -O2 in CFLAGS unless the user specified otherwise
AS_IF([test "x${ac_cv_env_CFLAGS_set}" = "x"], [CFLAGS=""])

# Checks for programs.

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([unistd.h stdio.h stdlib.h string.h inttypes.h sys/time.h pthread.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_CHECK_FUNCS([malloc memset])

#
# Check implementation details of build host
#

# Check for __atomic* builtins
AC_MSG_CHECKING([for __atomic builtins])
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM(
		[[

  		]],
  		[[
			int n = 1;
			int m = 0;
			m = __atomic_fetch_add(&n, 1, __ATOMIC_SEQ_CST);
  		]]
  	)],
  	[
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_ATOMIC_BUILTINS], [1], [the compiler supports __atomic builtins])
	],
  	[
		AC_MSG_RESULT([no])
		AC_MSG_FAILURE([compiler support for __atomic builtins are required])
	]
)

AC_MSG_CHECKING([for cache line size])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM(
		[[
			#include <stddef.h>

			#if defined __APPLE__
			        #include <sys/sysctl.h>
			#elif defined __linux__
			        #include <stdio.h>
			#else
				#error unrecognized platform
			#endif
		]],
		[[
			#if defined __APPLE__
			        size_t line_size = 0;
			        size_t sizeof_line_size = sizeof(line_size);
			        sysctlbyname("hw.cachelinesize", &line_size, &sizeof_line_size, 0, 0);
			        return line_size;
			#elif defined __linux__
				FILE *p = 0;
				p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
				unsigned int i = 0;
				if (p) {
				        fscanf(p, "%d", &i);
				        fclose(p);
				}
				return i;
			#else
				return 0;
			#endif
		]]
	)],
	[
		cache_line_size=0
	],
	[
		cache_line_size=$?
	],
	[
		AC_MSG_FAILURE([could not deduce cache line size])
	]
)
AC_DEFINE_UNQUOTED([CACHE_LINE_SIZE], [($cache_line_size)], [Define to value of cache line size])
AC_MSG_RESULT($cache_line_size)

target=`uname`
case "x$target" in
	xDarwin)
	AM_CONDITIONAL(OS_DARWIN, true)
	AM_CONDITIONAL(OS_LINUX, false)
	;;
	xLinux)
	AM_CONDITIONAL(OS_DARWIN, false)
	AM_CONDITIONAL(OS_LINUX, true)
	;;
	*)
	AC_MSG_ERROR([disruptorC is not supported on this operating system])
	;;
esac

dnl
dnl CPPFLAGS, CFLAGS, LIBS and stuff
dnl

dnl Check for pthread support
AX_PTHREAD(
	[
		AX_NOOP()
	],
	[
		AC_MSG_ERROR([pthreads are required])
	]
)

dnl
dnl LIBS
dnl
LIBS="$PTHREAD_LIBS $LIBS"
AC_SUBST(LIBS)

dnl
dnl Check if the compiler supports -iquote DIR (from ltrace)
dnl
ac_cv_have_iquote=no
AC_MSG_CHECKING(whether compiler accepts -iquote dir)
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -iquote ."
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[ac_cv_have_iquote=yes],[])
CPPFLAGS="$save_CPPFLAGS"
AC_MSG_RESULT($ac_cv_have_iquote)
if test $ac_cv_have_iquote = yes; then
  iquote="-iquote "
else
  iquote="-I"
fi

dnl Should only be searchable in "" include statements
DISRUPTORC_INCLUDES="$iquote$DISRUPTORC_top_dir"

dnl
dnl CFLAGS
dnl

dnl Now set the preprocessor arguments
DISRUPTORC_CFLAGS="$DISRUPTORC_INCLUDES"

dnl Pthread stuff as needed
DISRUPTORC_CFLAGS="$DISRUPTORC_CFLAGS $PTHREAD_CFLAGS"

dnl Warnings and other compiler options
AX_CFLAGS_GCC_OPTION(-Wall, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Winline, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Werror, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Wundef, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Wpointer-arith, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Werror-implicit-function-declaration, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Wbad-function-cast, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Wcast-align, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-Wmissing-declarations, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-pipe, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-fprefetch-loop-arrays, DISRUPTORC_CFLAGS)
dnl AX_CFLAGS_GCC_OPTION(-ggdb, DISRUPTORC_CFLAGS)
AX_CFLAGS_GCC_OPTION(-O2, DISRUPTORC_CFLAGS)

dnl Needed to get size of int64 on linux
if test "x$target" = "xLinux"; then
	DISRUPTORC_CFLAGS="$DISRUPTORC_CFLAGS -D__STDC_LIMIT_MACROS"
fi
AC_SUBST(DISRUPTORC_CFLAGS)

dnl
dnl LDFLAGS
dnl
if test "x$target" = "xDarwin"; then
	LDFLAGS="-flat_namespace $LDFLAGS"
fi
LDFLAGS="-L$DISRUPTORC_top_dir/ $LDFLAGS"
AC_SUBST(LDFLAGS)


dnl ******************************
dnl Makefiles
dnl ******************************

AC_CONFIG_FILES([Makefile
		 test/Makefile	
	 ])
AC_OUTPUT

echo "\

	Target Platform:           $target
	Cache line size:	   $cache_line_size
	DISRUPTORC_CFLAGS:         $DISRUPTORC_CFLAGS
"
