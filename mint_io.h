/*
 * This file belongs to FreeMiNT. It's not in the original MiNT 1.12
 * distribution. See the file CHANGES for a detailed log of changes.
 * 
 * 
 * Copyright 2000 Frank Naumann <fnaumann@freemint.de>
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * 
 * Author: Frank Naumann <fnaumann@freemint.de>
 * Started: 200-06-14
 * 
 * Please send suggestions, patches or bug reports to me or
 * the MiNT mailing list.
 * 
 */

# ifdef __MINT__

# ifndef _mint_io_h
# define _mint_io_h

# include <sys/types.h>

# define BLKGETSIZE		(('b'<< 8) | 1)
# define BLOCKSIZE		(('b'<< 8) | 2)

int gettype(int fd);
int gemdos_partition(int fd);

/* 64-bit seek over the XHDI backend. */
loff_t llseek(int fd, loff_t offset, int whence);

/* lseek() keeps mintlib's 32-bit off_t, so a caller with a forced 64-bit off_t
 * seeks through llseek(); mint_io.c undefines __USE_FILE_OFFSET64 first. */
# ifdef __USE_FILE_OFFSET64
# define lseek(fd, offset, whence) llseek((fd), (loff_t)(offset), (whence))
# endif

# endif /* _mint_io_h */

# endif /* __MINT__ */
