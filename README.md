zlib heap overflow

asan log attached.

Test program is a slightly modified version of test/infcover.c

1. unpack zlib
2. build inf1.c
$ gcc   -g -o  inf1 inf1.c -fsanitize=address -I.  trees.c zutil.c inftrees.c deflate.c inflate.c inffast.c crc32.c adler32.c

Found by EL of @intevydis