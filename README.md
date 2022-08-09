zlib 1.2.12 heap overflow

asan log attached.

Test program is a slightly modified version of test/infcover.c

1. unpack zlib
2. build inf1.c

$ gcc   -g -o  inf1 inf1.c -fsanitize=address -I.  trees.c zutil.c inftrees.c deflate.c inflate.c inffast.c crc32.c adler32.c

Found by Evgeny Legerov of @intevydis


Fixed in zlib develop branch https://github.com/madler/zlib/commit/eff308af425b67093bab25f80f1ae950166bece1

This fix introduced NULL pointer dereference.

Correct fix is here https://cgit.freebsd.org/src/commit/sys/contrib/zlib/inflate.c?id=2969066f73fc67a614144ac09b9f3f5291937fed


https://www.cve.org/CVERecord?id=CVE-2022-37434
