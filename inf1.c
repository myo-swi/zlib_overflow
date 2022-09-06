#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "zlib.h"

#define ZLIB_INTERNAL
//#include "inftrees.h"
//#include "inflate.h"

#define local static

/* these items are strung together in a linked list, one for each allocation */
struct mem_item {
    void *ptr;                  /* pointer to allocated memory */
    size_t size;                /* requested size of allocation */
    struct mem_item *next;      /* pointer to next item in list, or NULL */
};

/* this structure is at the root of the linked list, and tracks statistics */
struct mem_zone {
    struct mem_item *first;     /* pointer to first item in list, or NULL */
    size_t total, highwater;    /* total allocations, and largest total */
    size_t limit;               /* memory allocation limit, or 0 if no limit */
    int notlifo, rogue;         /* counts of non-LIFO frees and rogue frees */
};

/* memory allocation routine to pass to zlib */
local void *mem_alloc(void *mem, unsigned count, unsigned size)
{
    void *ptr;
    struct mem_item *item;
    struct mem_zone *zone = mem;
    size_t len = count * (size_t)size;

    /* induced allocation failure */
    if (zone == NULL || (zone->limit && zone->total + len > zone->limit))
        return NULL;

    /* perform allocation using the standard library, fill memory with a
       non-zero value to make sure that the code isn't depending on zeros */
    ptr = malloc(len);
    if (ptr == NULL)
        return NULL;
    memset(ptr, 0xa5, len);

    /* create a new item for the list */
    item = malloc(sizeof(struct mem_item));
    if (item == NULL) {
        free(ptr);
        return NULL;
    }
    item->ptr = ptr;
    item->size = len;

    /* insert item at the beginning of the list */
    item->next = zone->first;
    zone->first = item;

    /* update the statistics */
    zone->total += item->size;
    if (zone->total > zone->highwater)
        zone->highwater = zone->total;

    /* return the allocated memory */
    return ptr;
}

/* memory free routine to pass to zlib */
local void mem_free(void *mem, void *ptr)
{
    struct mem_item *item, *next;
    struct mem_zone *zone = mem;

    /* if no zone, just do a free */
    if (zone == NULL) {
        free(ptr);
        return;
    }

    /* point next to the item that matches ptr, or NULL if not found -- remove
       the item from the linked list if found */
    next = zone->first;
    if (next) {
        if (next->ptr == ptr)
            zone->first = next->next;   /* first one is it, remove from list */
        else {
            do {                        /* search the linked list */
                item = next;
                next = item->next;
            } while (next != NULL && next->ptr != ptr);
            if (next) {                 /* if found, remove from linked list */
                item->next = next->next;
                zone->notlifo++;        /* not a LIFO free */
            }

        }
    }

    /* if found, update the statistics and free the item */
    if (next) {
        zone->total -= next->size;
        free(next);
    }

    /* if not found, update the rogue count */
    else
        zone->rogue++;

    /* in any case, do the requested free with the standard library function */
    free(ptr);
}

/* set up a controlled memory allocation space for monitoring, set the stream
   parameters to the controlled routines, with opaque pointing to the space */
local void mem_setup(z_stream *strm)
{
    struct mem_zone *zone;

    zone = malloc(sizeof(struct mem_zone));
    assert(zone != NULL);
    zone->first = NULL;
    zone->total = 0;
    zone->highwater = 0;
    zone->limit = 0;
    zone->notlifo = 0;
    zone->rogue = 0;
    strm->opaque = zone;
    strm->zalloc = mem_alloc;
    strm->zfree = mem_free;
}

/* set a limit on the total memory allocation, or 0 to remove the limit */
local void mem_limit(z_stream *strm, size_t limit)
{
    struct mem_zone *zone = strm->opaque;

    zone->limit = limit;
}

/* show the current total requested allocations in bytes */
local void mem_used(z_stream *strm, char *prefix)
{
    struct mem_zone *zone = strm->opaque;

    fprintf(stderr, "%s: %lu allocated\n", prefix, zone->total);
}

/* show the high water allocation in bytes */
local void mem_high(z_stream *strm, char *prefix)
{
    struct mem_zone *zone = strm->opaque;

    fprintf(stderr, "%s: %lu high water mark\n", prefix, zone->highwater);
}

/* release the memory allocation zone -- if there are any surprises, notify */
local void mem_done(z_stream *strm, char *prefix)
{
    int count = 0;
    struct mem_item *item, *next;
    struct mem_zone *zone = strm->opaque;

    /* show high water mark */
    mem_high(strm, prefix);

    /* free leftover allocations and item structures, if any */
    item = zone->first;
    while (item != NULL) {
        free(item->ptr);
        next = item->next;
        free(item);
        item = next;
        count++;
    }

    /* issue alerts about anything unexpected */
    if (count || zone->total)
        fprintf(stderr, "** %s: %lu bytes in %d blocks not freed\n",
                prefix, zone->total, count);
    if (zone->notlifo)
        fprintf(stderr, "** %s: %d frees not LIFO\n", prefix, zone->notlifo);
    if (zone->rogue)
        fprintf(stderr, "** %s: %d frees not recognized\n",
                prefix, zone->rogue);

    /* free the zone and delete from the stream */
    free(zone);
    strm->opaque = Z_NULL;
    strm->zalloc = Z_NULL;
    strm->zfree = Z_NULL;
}

/* -- inflate test routines -- */

/* Decode a hexadecimal string, set *len to length, in[] to the bytes.  This
   decodes liberally, in that hex digits can be adjacent, in which case two in
   a row writes a byte.  Or they can be delimited by any non-hex character,
   where the delimiters are ignored except when a single hex digit is followed
   by a delimiter, where that single digit writes a byte.  The returned data is
   allocated and must eventually be freed.  NULL is returned if out of memory.
   If the length is not needed, then len can be NULL. */
local unsigned char *h2b(const char *hex, unsigned *len)
{
    unsigned char *in, *re;
    unsigned next, val;

    in = malloc((strlen(hex) + 1) >> 1);
    if (in == NULL)
        return NULL;
    next = 0;
    val = 1;
    do {
        if (*hex >= '0' && *hex <= '9')
            val = (val << 4) + *hex - '0';
        else if (*hex >= 'A' && *hex <= 'F')
            val = (val << 4) + *hex - 'A' + 10;
        else if (*hex >= 'a' && *hex <= 'f')
            val = (val << 4) + *hex - 'a' + 10;
        else if (val != 1 && val < 32)  /* one digit followed by delimiter */
            val += 240;                 /* make it look like two digits */
        if (val > 255) {                /* have two digits */
            in[next++] = val & 0xff;    /* save the decoded byte */
            val = 1;                    /* start over */
        }
    } while (*hex++);       /* go through the loop with the terminating null */
    if (len != NULL)
        *len = next;
    re = realloc(in, next);
    return re == NULL ? in : re;
}

local void inf(char *hex, char *what, unsigned step, int win, unsigned len,
               int err)
{
    int ret;
    unsigned have;
    unsigned char *in, *out;
    z_stream strm, copy;
    gz_header head;

    mem_setup(&strm);
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, win);
    if (ret != Z_OK) {
        mem_done(&strm, what);
        return;
    }
    out = malloc(len);                          assert(out != NULL);
    if (win == 47) {
        head.extra = out;
        head.extra_max = len;
        head.name = out;
        head.name_max = len;
        head.comment = out;
        head.comm_max = len;
        ret = inflateGetHeader(&strm, &head);   assert(ret == Z_OK);
    }
    in = h2b(hex, &have);                       assert(in != NULL);
    if (step == 0 || step > have)
        step = have;
    strm.avail_in = step;
    have -= step;
    strm.next_in = in;
    do {
        strm.avail_out = len;
        strm.next_out = out;
        ret = inflate(&strm, Z_NO_FLUSH);       
        if (ret != Z_OK && ret != Z_BUF_ERROR && ret != Z_NEED_DICT)
            break;
        have += strm.avail_in;
        strm.avail_in = step > have ? have : step;
        have -= strm.avail_in;
    } while (strm.avail_in);
    free(in);
    free(out);
    ret = inflateReset2(&strm, -8);            
    ret = inflateEnd(&strm);                   
    mem_done(&strm, what);
}
void main () {
 inf("1f 8b 08 04 61 62 63 64 61 62 52 51 1f 8b 08 04 61 62 63 64 61 62 52 51 1f 8b 08 04 61 62 63 64 61 62 52 51 1f 8b 08 04 61 62 63 64 61 62 52 51", "wtf", 13, 47, 12, Z_OK);
}
