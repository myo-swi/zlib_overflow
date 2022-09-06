# zlib 1.2.12 heap overflow

## Example to reproduce

Pick an operating system to test. e.g:
```
$ docker run -it ubuntu:bionic
// OR
$ docker run -it ubuntu:jammy
```

```
$ apt update && apt install -y git gcc zlib1g-dev
$ git clone https://github.com/myo-swi/zlib_overflow
$ cd zlib_overflow
$ gcc -g -o inf1 inf1.c -fsanitize=address -lz
$ ./inf1
```

If NOT vulnerable, it will print:
```
$ ./inf1
wtf: 7160 high water mark
```

If vulnerable, it will print:
```
$ ./inf1
=================================================================
==3405==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x604000000080 at pc 0x7f0f6ae53397 bp 0x7ffff4adcbf0 sp 0x7ffff4adc398
READ of size 4294967294 at 0x604000000080 thread T0
[... see asan.log ... ]
```

## Original notes

Test program is a slightly modified version of test/infcover.c

1. unpack zlib
2. build inf1.c

$ gcc   -g -o  inf1 inf1.c -fsanitize=address -I.  trees.c zutil.c inftrees.c deflate.c inflate.c inffast.c crc32.c adler32.c

Found by Evgeny Legerov of @intevydis


Fixed in zlib develop branch https://github.com/madler/zlib/commit/eff308af425b67093bab25f80f1ae950166bece1

This fix introduced NULL pointer dereference.

Correct fix is here https://cgit.freebsd.org/src/commit/sys/contrib/zlib/inflate.c?id=2969066f73fc67a614144ac09b9f3f5291937fed


https://www.cve.org/CVERecord?id=CVE-2022-37434
