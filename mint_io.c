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

/* The overrides below must match mintlib's 32-bit off_t even when the caller
 * forces a 64-bit one; llseek() uses loff_t and stays 64-bit. */
# undef __USE_FILE_OFFSET64

# include <assert.h>
# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <limits.h>
# include <stdarg.h>
# include <stdlib.h>
# include <stdio.h>
# include <string.h>
# include <time.h>
# include <unistd.h>

# include <sys/ioctl.h>
# include <sys/stat.h>

# ifdef E2FSPROGS_WRAPPER
# include "et/com_err.h"
# include "ext2fs/ext2_io.h"
# define loff_t ext2_loff_t
# define llseek ext2fs_llseek
# endif

# include <mintbind.h>
# include "mint_io.h"
# include "xhdi.h"


# if 0
# define DEBUG(x)	fprintf x
# else
# define DEBUG(x)
# endif

# define DriveToLetter(d) ((d) < 26 ? 'A' + (d) : (d) - 26 + '1')
# define DriveFromLetter(d) \
	(((d) >= 'A' && (d) <= 'Z') ? ((d) - 'A') : \
	 ((d) >= 'a' && (d) <= 'z') ? ((d) - 'a') : \
	 ((d) >= '1' && (d) <= '6') ? ((d) - '1' + 26) : \
	 -1)


/* prototypes */

int __open_v(const char *_filename, int iomode, va_list argp);

int open(__const char *__file, int __oflag, ...) __THROW;
int __open(__const char *__file, int __oflag, ...) __THROW;

int ioctl(int fd, int cmd, void *arg);
int __ioctl(int fd, int cmd, void *arg);

int fsync(int __fd) __THROW;
int __fsync(int __fd) __THROW;

__off_t lseek(int __fd, __off_t __offset, int __whence) __THROW;
__off_t __lseek(int __fd, __off_t __offset, int __whence) __THROW;

loff_t lseek64(int fd, loff_t offset, int origin);

int close(int __fd) __THROW;
int __close(int __fd) __THROW;

ssize_t read(int __fd, void *__buf, size_t __nbytes) __THROW;
ssize_t __read(int __fd, void *__buf, size_t __nbytes) __THROW;

ssize_t write(int __fd, __const void *__buf, size_t __n) __THROW;
ssize_t __write(int __fd, __const void *__buf, size_t __n) __THROW;

int fstat(int __fd, struct stat *__buf) __THROW;
int __fstat(int __fd, struct stat *__buf) __THROW;

int stat(const char *filename, struct stat *st) __THROW;
int __stat(const char *filename, struct stat *st) __THROW;


struct device
{
	int used;
	
	int drv;
	int open_flags;
	
	ushort xhdi_maj;
	ushort xhdi_min;
	ulong xhdi_start;
	ulong xhdi_blocks;
	ulong xhdi_blocksize;
	char xhdi_id[4];
	
	loff_t pos;
};

# define DEVS 16
static struct device devs[DEVS];

static void
init_device(struct device *dev)
{
	dev->used = 0;
	
	dev->drv = -1;
	dev->open_flags = 0;
	dev->xhdi_maj = 0;
	dev->xhdi_min = 0;
	dev->xhdi_start = 0;
	dev->xhdi_blocks = 0;
	dev->xhdi_blocksize = 0;
	
	dev->pos = 0;
}

static inline void
init(void)
{
	static int done = 0;
	int i;
	
	if (done)
		return;
	
	assert(sizeof(loff_t) == sizeof(long long));
	
	for (i = 0; i < DEVS; i++)
		init_device(&devs[i]);
	
	init_XHDI();
	
	/* we are now initialized */
	done = 1;
}

static struct device *
get_device(int fd)
{
	struct device *dev;
	
	if ((fd < 1024) || (fd >= (1024 + DEVS)))
		return NULL;
	
	fd -= 1024;
	dev = &devs[fd];
	
	assert(dev->used);
	
	return dev;
}

/* Compare an XHDI partition ID with 'id'. A DOS ID is "\0D" followed by the
 * partition type byte, so it is matched by its first two bytes only. */
static int
xhdi_id_is(const char *xhdi_id, const char *id)
{
	return memcmp(xhdi_id, id, id[0] ? 3 : 2) == 0;
}

static int
alloc_device(void)
{
	int i;
	
	for (i = 0; i < DEVS; i++)
	{
		struct device *dev = &devs[i];
		
		if (!dev->used)
		{
			dev->used = 1;
			return (i + 1024);
		}
	}
	
	__set_errno(ENOMEM);
	return -1;
}

static void
free_device(struct device *dev)
{
	assert(dev->used);
	
	init_device(dev);
}


int
open(const char *filename, int iomode, ...)
{
	const char *f = filename;
	struct device *mydev = NULL;
	int dev = -1;
	long ret;
	
	init();
	
	if (!filename)
	{
		__set_errno(EINVAL);
		return -1;
	}
	
	if ((f[1] == ':') && ((f[2] == '\0') ||
		(((f[2] == '\\') || (f[2] == '/')) && (f[3] == '\0'))))
	{
		int c = DriveFromLetter(f[0]);
		
		if ((c >= 0) && (c < 32))
		{
			dev = alloc_device();
			if (dev != -1)
			{
				mydev = get_device(dev);
				assert(mydev);
				
				mydev->drv = c;
				mydev->open_flags = iomode;
			}
		}
	}
	
	if (dev == -1)
	{
		/* fall through */
		
		va_list args;
		int retval;
		
		va_start(args, iomode);
		retval = __open_v(filename, iomode, args);
		va_end(args);
		
		DEBUG((stderr, "open: %s managed by posix: %d\n", filename, retval));
		return retval;
	}
	
	__set_errno(EERROR);
	
	ret = XHGetVersion();
	DEBUG((stderr, "XHDI version: %lx\n", ret));
	
	ret = XHInqDev2(mydev->drv,
			&mydev->xhdi_maj, &mydev->xhdi_min,
			&mydev->xhdi_start, NULL,
			&mydev->xhdi_blocks, mydev->xhdi_id);
	if (ret)
	{
		fprintf(stderr, "XHInqDev2 [%c] fail (ret = %li, errno = %i)\n",
			DriveToLetter(mydev->drv), ret, errno);
		ret = -1;
	}
	else
	{
		ret = XHInqTarget(mydev->xhdi_maj, mydev->xhdi_min,
				  &mydev->xhdi_blocksize, NULL, NULL);
		if (ret)
		{
			fprintf(stderr, "XHInqTarget [%i:%i] fail (ret = %li, errno = %i)\n",
				mydev->xhdi_maj, mydev->xhdi_min, ret, errno);
			ret = -1;
		}
		else
		{
			char *xhdi_id = mydev->xhdi_id;
			
			if (       0
#ifndef E2FSPROGS_WRAPPER
				|| xhdi_id_is(xhdi_id, "GEM") /* GEM */
				|| xhdi_id_is(xhdi_id, "BGM") /* BGM */
				|| xhdi_id_is(xhdi_id, "F32") /* F32 */
				|| xhdi_id_is(xhdi_id, "MIX") /* MIX */
#endif
				|| xhdi_id_is(xhdi_id, "RAW") /* RAW */
				|| xhdi_id_is(xhdi_id, "LNX") /* LNX */
				|| xhdi_id_is(xhdi_id, "\0D")) /* any DOS */
			{
				DEBUG((stderr, "Partition ok and accepted!\n"));
				DEBUG((stderr, "start = %lu, blocks = %lu, blocksize = %lu\n",
					mydev->xhdi_start, mydev->xhdi_blocks,
					mydev->xhdi_blocksize));
				DEBUG((stderr, "open: %s managed by xhdi: %d\n", filename, dev));
			}
			else
			{
				xhdi_id[3] = '\0';
				fprintf(stderr, "Wrong partition ID [%s]!\n", xhdi_id);
#ifdef E2FSPROGS_WRAPPER
				fprintf(stderr, "Only RAW, LNX and DOS partitions are supported.\n");
#else
				fprintf(stderr, "Only GEM, BGM, F32, MIX, RAW, LNX and DOS partitions are supported.\n");
#endif
				
				__set_errno(EPERM);
				ret = -1;
			}
		}
	}
	
	if (ret)
	{
		if (mydev)
			free_device(mydev);
		
		return -1;
	}
	
	/* only lock a drive known to be an XHDI partition: locking unmounts it
	 * and moves every process off it */
	if (mydev->open_flags == O_RDONLY)
	{
		DEBUG((stderr, "readonly mode!\n"));
		sync();
	}
	else if ((ret = Dlock(1, mydev->drv)) && ret != -ENOSYS)
	{
		fprintf(stderr, "Can't lock partition %c:!\n", DriveToLetter(mydev->drv));
		
		if (mydev)
			free_device(mydev);
		
		__set_errno(EACCES);
		return -1;
	}
	
	return dev;
}

int
close(int fd)
{
	struct device *mydev = get_device(fd);
	int ret = 0;
	
	if (!mydev)
	{
		/* fall through */
		DEBUG((stderr, "close: posix %d\n", fd));
		return __close(fd);
	}
	DEBUG((stderr, "close: xhdi %d\n", fd));
	
	if (mydev->open_flags == O_RDONLY)
	{
		;
	}
	else if ((ret = Dlock(0, mydev->drv)) && ret != -ENOSYS)
	{
		fprintf(stderr, "Can't unlock partition %c:!\n", DriveToLetter(mydev->drv));
		
		__set_errno(EACCES);
		ret = -1;
	}
	else
		ret = 0;
	
	free_device(mydev);
	return ret;
}

/* largest transfer handed to rwabs_xhdi(), also the size of its cache */
# define XHDI_CHUNK	(1024L * 128)

static long
rwabs_xhdi(struct device *mydev, ushort rw, void *buf, ulong size, ulong recno)
{
	/* simple buffer */
	static char buffer[XHDI_CHUNK];
	static ulong buf_recno = 0;
	static long buf_n = 0;
	
	ulong n = size / mydev->xhdi_blocksize;
	long r;
	
	assert((size % mydev->xhdi_blocksize) == 0);
	
	if (!n || (recno + n) > mydev->xhdi_blocks)
	{
		fprintf(stderr, "rwabs_xhdi: access outside partition (drv = %c:)\n", DriveToLetter(mydev->drv));
		exit(2);
	}
	
	if (n > 65535UL)
	{
		fprintf(stderr, "rwabs_xhdi: n to large (drv = %c)\n", DriveToLetter(mydev->drv));
		exit(2);
	}
	
	if (!rw && (buf_recno == recno) && (buf_n == n))
	{
		bcopy(buffer, buf, buf_n * mydev->xhdi_blocksize);
		return 0;
	}
	
	r = XHReadWrite(mydev->xhdi_maj, mydev->xhdi_min, rw, mydev->xhdi_start + recno, n, buf);
	
	if (!r && (n * mydev->xhdi_blocksize) <= sizeof(buffer))
	{
		bcopy(buf, buffer, n * mydev->xhdi_blocksize);
		
		buf_recno = recno;
		buf_n = n;
	}
	else
		buf_n = 0;
	
	return r;
}

# define max(a,b)	(a > b ? a : b)
# define min(a,b)	(a > b ? b : a)

ssize_t
read(int fd, void *_buf, size_t size)
{
	struct device *mydev = get_device(fd);
	char *buf = _buf;
	long todo;		/* characters remaining */
	long done;		/* characters processed */
	
	if (!mydev)
	{
		/* fall through */
		ssize_t nread = __read(fd, _buf, size);
		DEBUG((stderr, "read: posix %d %ld = %ld\n", fd, (long)size, (long)nread));
		return nread;
	}
	
	todo = size;
	done = 0;
	
	DEBUG((stderr, "read: xhdi %d %ld\n", fd, (long)size));
	if (todo == 0)
		return 0;
	
	/* EOF check */
	if (mydev->pos >= (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
		return 0;
	
	/* a read running past the end of the partition is cut short there */
	if (mydev->pos + todo > (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
		size = todo = (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize - mydev->pos;
	
	/* partial block copy
	 */
	if (mydev->pos % mydev->xhdi_blocksize)
	{
		char tmp[mydev->xhdi_blocksize];
		
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		ulong offset = mydev->pos % mydev->xhdi_blocksize;
		ulong data;
		long ret;
		
		ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "read: partial part: read failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		data = mydev->xhdi_blocksize - offset;
		data = min(todo, data);
		
		memcpy(buf, tmp + offset, data);
		
		buf += data;
		todo -= data;
		done += data;
		mydev->pos += data;
	}
	
	if (todo)
	{
		assert((todo > 0));
		assert((mydev->pos % mydev->xhdi_blocksize) == 0);
	}
	
	
	/* full blocks, in chunks: XHDI takes at most 65535 sectors at once
	 */
	while (todo / mydev->xhdi_blocksize)
	{
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		ulong data = min(todo, max(XHDI_CHUNK, (long) mydev->xhdi_blocksize));
		long ret;
		
		data = (data / mydev->xhdi_blocksize) * mydev->xhdi_blocksize;
		ret = rwabs_xhdi(mydev, 0, buf, data, recno);
		if (ret)
		{
			DEBUG((stderr, "read: full blocks: read failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		buf += data;
		todo -= data;
		done += data;
		mydev->pos += data;
	}
	
	if (todo)
	{
		assert((todo > 0) && (todo < mydev->xhdi_blocksize));
		assert((mydev->pos % mydev->xhdi_blocksize) == 0);
	}
	
	/* anything left?
	 */
	if (todo)
	{
		char tmp[mydev->xhdi_blocksize];
		
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		long ret;
		
		ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "read: left part: read failure (r = %li, errno = %i)]\n", ret, errno));
			return done;
		}
		
		memcpy(buf, tmp, todo);
		
		done += todo;
		mydev->pos += todo;
	}
	
	assert(done == size);
	
	return done;
}

#pragma GCC diagnostic ignored "-Wcast-qual"

ssize_t
write(int fd, const void *_buf, size_t size)
{
	struct device *mydev = get_device(fd);
	const char *buf = _buf;
	long todo;		/* characters remaining */
	long done;		/* characters processed */
	
	if (!mydev)
	{
		/* fall through */
		ssize_t nwrite = __write(fd, _buf, size);
		DEBUG((stderr, "write: posix %d %ld = %ld\n", fd, (long)size, (long)nwrite));
		return nwrite;
	}
	
	DEBUG((stderr, "write: xhdi %d %ld\n", fd, (long)size));
	if (mydev->open_flags == O_RDONLY)
	{
		__set_errno(EPERM);
		return -1;
	}
	
	todo = size;
	done = 0;
	
	if (todo == 0)
		return 0;
	
	/* EOF check */
	if (mydev->pos >= (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
	{
		__set_errno(ENOSPC);
		return -1;
	}
	
	/* a write running past the end of the partition is cut short there */
	if (mydev->pos + todo > (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
		size = todo = (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize - mydev->pos;
	
	/* partial block copy
	 */
	if (mydev->pos % mydev->xhdi_blocksize)
	{
		char tmp[mydev->xhdi_blocksize];
		
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		ulong offset = mydev->pos % mydev->xhdi_blocksize;
		ulong data;
		long ret;
		
		ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "write: partial part: read failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		data = mydev->xhdi_blocksize - offset;
		data = min(todo, data);
		
		memcpy(tmp + offset, buf, data);
		
		ret = rwabs_xhdi(mydev, 1, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "write: partial part: write failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		buf += data;
		todo -= data;
		done += data;
		mydev->pos += data;
	}
	
	if (todo)
	{
		assert((todo > 0));
		assert((mydev->pos % mydev->xhdi_blocksize) == 0);
	}
	
	/* full blocks, in chunks: XHDI takes at most 65535 sectors at once
	 */
	while (todo / mydev->xhdi_blocksize)
	{
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		ulong data = min(todo, max(XHDI_CHUNK, (long) mydev->xhdi_blocksize));
		long ret;
		
		data = (data / mydev->xhdi_blocksize) * mydev->xhdi_blocksize;
		ret = rwabs_xhdi(mydev, 1, (void *)buf, data, recno);
		if (ret)
		{
			DEBUG((stderr, "write: full blocks: write failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		buf += data;
		todo -= data;
		done += data;
		mydev->pos += data;
	}
	
	if (todo)
	{
		assert((todo > 0) && (todo < mydev->xhdi_blocksize));
		assert((mydev->pos % mydev->xhdi_blocksize) == 0);
	}
	
	/* anything left?
	 */
	if (todo)
	{
		char tmp[mydev->xhdi_blocksize];
		
		ulong recno = mydev->pos / mydev->xhdi_blocksize;
		long ret;
		
		ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "write: left part: read failure (r = %li, errno = %i)]\n", ret, errno));
			return done;
		}
		
		memcpy(tmp, buf, todo);
		
		ret = rwabs_xhdi(mydev, 1, tmp, mydev->xhdi_blocksize, recno);
		if (ret)
		{
			DEBUG((stderr, "write: partial part: write failure (r = %li, errno = %i)\n", ret, errno));
			return done;
		}
		
		done += todo;
		mydev->pos += todo;
	}
	
	assert(done == size);
	
	return done;
}

int
ioctl(int fd, int cmd, void *arg)
{
	struct device *mydev = get_device(fd);
	
	if (!mydev)
	{
		/* fall through */
		DEBUG((stderr, "ioctl: posix: %d %04x\n", fd, cmd));
		return __ioctl(fd, cmd, arg);
	}
	DEBUG((stderr, "ioctl: xhdi: %d %04x\n", fd, cmd));
	
	switch (cmd)
	{
		case BLKGETSIZE:
		{
			ulong *size = arg;
			*size = mydev->xhdi_blocks * (mydev->xhdi_blocksize / 512);
			break;
		}
		case BLOCKSIZE:
		{
			ulong *block_size = arg;
			*block_size = mydev->xhdi_blocksize;
			break;
		}
		default:
			__set_errno(EINVAL);
			return -1;
	}
	
	return 0;
}

int
fstat(int fd, struct stat *st)
{
	struct device *mydev = get_device(fd);
	
	if (!mydev)
	{
		/* fall through */
		int s = __fstat(fd, st);
		DEBUG((stderr, "fstat: posix: %d: %ld\n", fd, s == 0 ? (long)st->st_size : 0));
		return s;
	}
	
	bzero(st, sizeof(*st));
	
	st->st_dev	= mydev->xhdi_maj;
	st->st_ino	= mydev->drv;
	st->st_mode	= S_IFBLK | S_IRUSR | S_IWUSR;
	st->st_nlink	= 1;
	st->st_uid	= 0;
	st->st_gid	= 0;
	st->st_rdev	= mydev->xhdi_min;
	st->st_atime	= time(NULL);
	st->st_mtime	= time(NULL);
	st->st_ctime	= time(NULL);
	st->st_size	= (int64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
	st->st_blocks	= (int64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize / 512;
	st->st_blksize	= mydev->xhdi_blocksize;
	st->st_flags	= 0;
	st->st_gen	= 0;
	
	DEBUG((stderr, "fstat: xhdi: %d: %ld\n", fd, (long)st->st_size));
	return 0;
}

int
stat(const char *filename, struct stat *st)
{
	struct device *mydev;
	int fd, res;
	
	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return -1;

	mydev = get_device(fd);
	if (!mydev)
	{
		close(fd);
		
		/* fall through */
		return __stat(filename, st);
	}
	
	res = fstat(fd, st);
	close(fd);
	
	return res;
}

int
fsync(int fd)
{
	struct device *mydev = get_device(fd);
	
	if (!mydev)
		/* fall through */
		return __fsync(fd);
	
	/* nothing todo */
	return 0;
}

loff_t
llseek(int fd, loff_t offset, int origin)
{
	struct device *mydev = get_device(fd);
	loff_t _offset;
	
	if (!mydev)
	{
		/* fall through */
		loff_t pos = __lseek(fd, (off_t) offset, origin);
		DEBUG((stderr, "llseek: posix: %d: %ld -> %ld\n", fd, (long)offset, (long)pos));
		return pos;
	}
	
	_offset = offset;
	switch (origin)
	{
		case SEEK_SET:
			break;
		case SEEK_CUR:
			_offset += mydev->pos;
			break;
		case SEEK_END:
			_offset += (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
			break;
		default:
			return -1;
	}
	
	if (_offset > (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
	{
		__set_errno(EINVAL);
		return -1;
	}
	
	DEBUG((stderr, "llseek: xhdi: %d: %ld -> %ld\n", fd, (long)offset, (long)_offset));
	mydev->pos = _offset;
	return mydev->pos;
}

loff_t
lseek64(int fd, loff_t offset, int origin)
{
	return llseek(fd, offset, origin);
}

__off_t
lseek(int fd, __off_t offset, int mode)
{
	struct device *mydev = get_device(fd);
	loff_t _offset;
	
	if (!mydev)
	{
		/* fall through */
		off_t pos = __lseek(fd, offset, mode);
		DEBUG((stderr, "lseek: posix: %d: %ld -> %ld\n", fd, (long)offset, (long)pos));
		return pos;
	}
	
	_offset = offset;
	switch (mode)
	{
		case SEEK_SET:
			break;
		case SEEK_CUR:
			_offset += mydev->pos;
			break;
		case SEEK_END:
			_offset += (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
			break;
		default:
			return -1;
	}
	
	if (_offset > LONG_MAX)
	{
		__set_errno(EINVAL);
		return -1;
	}
	
	if (_offset > (loff_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
	{
		__set_errno(EINVAL);
		return -1;
	}
	
	DEBUG((stderr, "lseek: xhdi: %d: %ld -> %ld\n", fd, (long)offset, (long)_offset));
	mydev->pos = _offset;
	return (off_t) mydev->pos;
}

int
gettype(int fd)
{
	struct device *mydev = get_device(fd);
	char *xhdi_id;
	
	if (!mydev)
		return -1;

	/* Get filesystem type by XHDI ID */
	xhdi_id = mydev->xhdi_id;
	if (xhdi_id_is(xhdi_id, "\0D"))
		return 0;   /* DOS (\0D*) */
	else
		return 1;   /* Atari (GEM/GBM) */
}

int
gemdos_partition(int fd)
{
	struct device *mydev = get_device(fd);
	char *xhdi_id;
	
	if (!mydev)
		return 0;
	
	xhdi_id = mydev->xhdi_id;
	return xhdi_id_is(xhdi_id, "GEM") || xhdi_id_is(xhdi_id, "BGM");
}

# endif /* __MINT__ */
