# libIO

Partition access for FreeMiNT, which has no block devices. `mint_io.c`
overrides `open()`, `close()`, `read()`, `write()`, `lseek()`, `llseek()`,
`ioctl()`, `fstat()`, `stat()` and `fsync()`: a drive path such as `C:`, `C:\`
or `C:/` is accessed through XHDI, anything else is passed on to mintlib.
`xhdi.c` provides the XHDI calls.

Meant to be used as a git submodule; the parent project compiles `mint_io.c`
and `xhdi.c` itself. Define `E2FSPROGS_WRAPPER` when building for libext2fs:
`llseek()` then becomes `ext2fs_llseek()` and only RAW, LNX and DOS
partitions are accepted.
