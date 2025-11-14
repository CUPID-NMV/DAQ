#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.


# Tested with autoconf 2.69, released in 2012
AC_PREREQ([2.69])
#	Copyright (C) 2023 CAEN SpA
#
#	This file is part of the CAEN PLU Scaler demo.
#
#	The CAEN WaveDump MB is free software; you can redistribute it and/or
#	modify it under the terms of the GNU General Public
#	License as published by the Free Software Foundation; either
#	version 3 of the License, or (at your option) any later version.
#
#	The CAEN PLU Scaler demo is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#	General Public License for more details.
#
#	You should have received a copy of the GNU General Public
#	License along with the CAEN PLU Scaler demo; if not, see
#	https://www.gnu.org/licenses/.
#
#	SPDX-License-Identifier: GPL-3.0-or-later

AC_INIT(CAEN_PLUScalerDAQ, [m4_esyscmd_s([echo $VERSION])], support.computing@caen.it)

AM_INIT_AUTOMAKE([-Wall foreign subdir-objects tar-ustar])

AC_PROG_CC
AC_PROG_CC_C99

# Checks for other programs
AC_PROG_INSTALL

# Checks for standard libraries
AC_SEARCH_LIBS([sqrt], [m])
AX_PTHREAD([], [AC_MSG_ERROR([pthread required.])])

# Checks for CAEN libraries
plu_not_found_msg="CAEN_PLULib must be installed on your system."
AC_CHECK_HEADER([CAEN_PLULib.h], [], [AC_MSG_ERROR($plu_not_found_msg)])
AC_SEARCH_LIBS([CAEN_PLU_OpenDevice2], [CAEN_PLU], [], [AC_MSG_ERROR($plu_not_found_msg)])

# Defines
AC_DEFINE([LINUX], [])

# Checks for programs
AC_CHECK_PROG(GNUPLOT, gnuplot, "yes", "no")
if test $GNUPLOT == "no"; then
  AC_MSG_ERROR(
    *****************************************************************
    GnuPlot not found. Please install it and retry.
    *****************************************************************
  )
fi

# Checks for library functions.
AC_CONFIG_FILES([
	Makefile
	src/Makefile
])

AC_OUTPUT
