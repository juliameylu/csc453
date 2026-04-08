/*
 * malloc.c
 * Implements a simple linked-list allocator on top of sbrk(). The allocator
 * supports malloc(), free(), calloc(), and realloc(), including block
 * splitting, free-block coalescing, and optional debug narration.
 */
#include <unistd.h>     /* sbrk, write */
#include <stdlib.h>     /* getenv */
#include <stdio.h>      /* snprintf */
#include <stdint.h>     /* SIZE_MAX */
#include <stdbool.h>
#include <errno.h>
#include <string.h>     /* memset, memcpy */


#define CHUNK_SIZE (64 * 1024) /* 64k */
#define MINIMUM_PAYLOAD_SIZE 16
/* 16-byte alignment, so reasonable is min is 16 */

/* for debugging, knowing which debug msg to print */
#define MALLOC_ID 1
#define FREE_ID 2
#define CALLOC_ID 3
#define REALLOC_ID 4

/* node definition for a linked list */
struct BlockHeader {
        size_t payload_capacity; /* aligned usable payload capacity */
        bool is_free;
        struct BlockHeader *next_block_header;
};

/* static so private to this source file */
static bool suppress_debug = false; /* prevent double print */
static struct BlockHeader *heap_head = NULL;

static void *splitblock(
        struct BlockHeader *curr_block, size_t requested_rounded_size
);
static struct BlockHeader *findblock(void *ptr);
static void merge_free_nodes(void);
static size_t round_up_16(size_t size);
static void print_debug_msg(
        int id, void *ptr, void *new_ptr, size_t nmemb, size_t size
);
static void print_custom_debug_msg(const char *msg);

/*
 * Allocates a 16-byte-aligned payload region from a free block or from
 * newly requested heap space.
 */
void *malloc(size_t size) {
        size_t header_size;
        size_t requested_rounded_size;
        size_t used_block_size;
        struct BlockHeader *prev_block;
        struct BlockHeader *curr_block;
        void *user_payload_ptr;

        if (size == 0) {
                print_debug_msg(MALLOC_ID, NULL, NULL, 0, size);
                print_custom_debug_msg("MALLOC: size is 0, returning\n");
                return NULL;
        }

        /* round header and payload to nearest 16 */
        header_size = round_up_16(sizeof(struct BlockHeader));
        requested_rounded_size = round_up_16(size);

        /* used block size is header + payload */
        used_block_size = header_size + requested_rounded_size;

        /* traverse list to see if any blocks are big enough */
        /* save prev block so can chain link new node to the end */
        prev_block = NULL;
        curr_block = heap_head;
        while (curr_block != NULL) {
                /* if block is big enough, stop */
                if (curr_block->is_free &&
                        curr_block->payload_capacity >=
                        requested_rounded_size) {
                        print_custom_debug_msg(
                                "MALLOC: found block big enough\n"
                        );
                        break;
                }
                prev_block = curr_block;
                curr_block = curr_block->next_block_header;
        }

        /* if no node big enough, sbrk */
        if (curr_block == NULL) {
                size_t new_block_size;
                uintptr_t curr_break;
                uintptr_t aligned_curr_break;
                size_t padding;
                void *new_sbrk;

                print_custom_debug_msg(
                        "MALLOC: no block big enough, calling sbrk\n"
                );
                /* default to CHUNK_SIZE unless bigger */
                new_block_size = (used_block_size < CHUNK_SIZE) ?
                        CHUNK_SIZE : used_block_size;
                /* Align the program break before placing a new header so
                        subsequent payload pointers stay 16-byte aligned. */
                curr_break = (uintptr_t) sbrk(0);
                aligned_curr_break = round_up_16(curr_break);
                padding = aligned_curr_break - curr_break;
                new_sbrk = sbrk(padding);
                /* no available heap space */
                if (new_sbrk == (void *) -1) {
                        errno = ENOMEM;
                        print_debug_msg(MALLOC_ID, NULL, NULL, 0, size);
                        print_custom_debug_msg("MALLOC: padding sbrk failed\n");
                        return NULL;
                }

                curr_block = (struct BlockHeader *) sbrk(new_block_size);

                /* no available heap space */
                if (curr_block == (void *) -1) {
                        errno = ENOMEM;
                        print_debug_msg(MALLOC_ID, NULL, NULL, 0, size);
                        print_custom_debug_msg("MALLOC: block sbrk failed\n");
                        return NULL;
                }

                print_custom_debug_msg("MALLOC: sbrk succeeded\n");

                curr_block->payload_capacity = new_block_size - header_size;
                curr_block->is_free = true;
                curr_block->next_block_header = NULL;

                /* if empty list, make new block the head,
                        otherwise chain it to the prev_block */
                if (prev_block == NULL) {
                        heap_head = curr_block;
                } else {
                        prev_block->next_block_header = curr_block;
                }
        }

        /* use the block */
        curr_block->is_free = false;
        /* optionally split the block */
        user_payload_ptr = splitblock(curr_block, requested_rounded_size);

        print_debug_msg(MALLOC_ID, NULL, user_payload_ptr, 0, size);
        print_custom_debug_msg("MALLOC: malloc succeeded\n");
        return user_payload_ptr;
}

/*
 * Frees the allocation unit containing ptr and coalesces adjacent free
 * blocks. Per the assignment spec, ptr may be an interior payload pointer.
 */
void free(void *ptr) {
        struct BlockHeader *curr_block;

        if (ptr == NULL) {
                print_custom_debug_msg("FREE: ptr is NULL\n");
                print_debug_msg(FREE_ID, ptr, NULL, 0, 0);
                return;
        }

        /* find block, set to free, merge */
        curr_block = findblock(ptr);
        if (curr_block == NULL) {
                print_custom_debug_msg("FREE: block not found\n");
                print_debug_msg(FREE_ID, ptr, NULL, 0, 0);
                return;
        }

        /* guard against double free */
        if (curr_block->is_free) {
                print_custom_debug_msg("FREE: double free\n");
                print_debug_msg(FREE_ID, ptr, NULL, 0, 0);
                return;
        }

        curr_block->is_free = true;
        merge_free_nodes();

        print_custom_debug_msg("FREE: free succeeded\n");
        print_debug_msg(FREE_ID, ptr, NULL, 0, 0);
}

/*
 * Allocates nmemb * size bytes and zero-initializes the returned payload.
 */
void *calloc(size_t nmemb, size_t size) {
        size_t total_req_bytes;
        void *user_payload_ptr;

        /* edge case, size_t overflow, check before multiplying */
        /* nmemb * size <= SIZE_MAX */
        /* nmemb <= SIZE_MAX / size; size != 0 */
        if (size != 0 && nmemb > SIZE_MAX / size) {
                errno = ENOMEM;
                print_custom_debug_msg("CALLOC: overflow\n");
                print_debug_msg(CALLOC_ID, NULL, NULL, nmemb, size);
                return NULL;
        }

        total_req_bytes = nmemb * size;

        suppress_debug = true;
        user_payload_ptr = malloc(total_req_bytes);
        suppress_debug = false;

        if (user_payload_ptr == NULL) {
                print_custom_debug_msg("CALLOC: malloc failed\n");
                print_debug_msg(CALLOC_ID, NULL, user_payload_ptr, nmemb, size);
                return NULL;
        }
        
        memset(user_payload_ptr, 0, total_req_bytes);

        print_custom_debug_msg("CALLOC: calloc succeeded\n");
        print_debug_msg(CALLOC_ID, NULL, user_payload_ptr, nmemb, size);
        return user_payload_ptr;
}

/*
 * Resizes the allocation unit containing ptr, preferring in-place shrink or
 * growth before falling back to allocate-copy-free behavior.
 */
void *realloc(void *ptr, size_t size) {
        struct BlockHeader *curr_block;
        size_t curr_payload_cap;
        size_t requested_rounded_size;
        size_t header_size;
        void *user_payload_ptr;

        if (ptr == NULL) {
                /* equivalent to malloc(size) */
                suppress_debug = true;
                user_payload_ptr = malloc(size);
                suppress_debug = false;
                print_custom_debug_msg(
                        "REALLOC: ptr is NULL, calling malloc\n"
                );
                print_debug_msg(REALLOC_ID, ptr, user_payload_ptr, 0, size);
                return user_payload_ptr;
        } else if (ptr != NULL && size == 0) {
                /* equivalent to free(ptr) */
                suppress_debug = true;
                free(ptr);
                suppress_debug = false;
                print_custom_debug_msg("REALLOC: size is 0, calling free\n");
                print_debug_msg(REALLOC_ID, ptr, NULL, 0, size);
                return NULL;
        }

        /* find block containing ptr */
        curr_block = findblock(ptr);
        if (curr_block == NULL) {
                print_custom_debug_msg("REALLOC: block not found\n");
                print_debug_msg(REALLOC_ID, ptr, (void *) curr_block, 0, size);
                return NULL;
        }
        print_custom_debug_msg("REALLOC: block found\n");

        curr_payload_cap = curr_block->payload_capacity;
        requested_rounded_size = round_up_16(size);
        header_size = round_up_16(sizeof(struct BlockHeader));

        if (curr_payload_cap == requested_rounded_size) {
                /* if don't need to change size, return ptr to start of
                        payload */
                user_payload_ptr = (char *) curr_block + header_size;

                print_custom_debug_msg(
                        "REALLOC: size didn't change, returning payload ptr\n"
                );
                print_debug_msg(REALLOC_ID, ptr, user_payload_ptr, 0, size);
        } else if (curr_payload_cap > requested_rounded_size) {
                /* if shrinking, optionally split leftover free space */
                /* splitblock returns ptr to start of payload */
                print_custom_debug_msg(
                        "REALLOC: user requested smaller size\n"
                );
                user_payload_ptr = splitblock(
                        curr_block, requested_rounded_size
                );
                
                print_debug_msg(REALLOC_ID, ptr, user_payload_ptr, 0, size);
        } else if (curr_block->next_block_header == NULL){
                size_t space_needed;
                size_t size_allocated;
                void *new_sbrk;

                /* if at the end of the heap, avoid copying by sbrking more
                        room */
                print_custom_debug_msg(
                        "REALLOC: at the end of heap, calling sbrk\n"
                );
                space_needed = requested_rounded_size - curr_payload_cap;
                /* default to CHUNK_SIZE unless bigger */
                size_allocated = (space_needed < CHUNK_SIZE) ?
                        CHUNK_SIZE : space_needed;
                new_sbrk = sbrk(size_allocated);
                if (new_sbrk == (void *) -1) {
                        errno = ENOMEM;
                        print_custom_debug_msg("REALLOC: sbrk failed\n");
                        print_debug_msg(REALLOC_ID, ptr, NULL, 0, size);
                        return NULL;
                }
                print_custom_debug_msg("REALLOC: sbrk succeeded\n");

                curr_block->payload_capacity += size_allocated;
                user_payload_ptr = splitblock(
                        curr_block, requested_rounded_size
                );
                
                print_debug_msg(REALLOC_ID, ptr, user_payload_ptr, 0, size);
        } else {
                struct BlockHeader *next_block;
                void *src;

                /* if expanding, try to expand in place first to minimize
                        copying - look at the next block, if free and total
                        space can fit, combine it */
                /* only need to look at the next block because calls to other
                        functions consistently merge free nodes across the
                        entire list */
                print_custom_debug_msg(
                        "REALLOC: attempting to expand in place\n"
                );
                next_block = curr_block->next_block_header;
                if (next_block != NULL && next_block->is_free) {
                        size_t total_next_block_size;
                        size_t total_available_size;

                        total_next_block_size = header_size +
                                next_block->payload_capacity;
                        total_available_size = curr_payload_cap +
                                total_next_block_size;

                        if (total_available_size >= requested_rounded_size) {
                                print_custom_debug_msg(
                                        "REALLOC: expanding in place\n"
                                );
                                curr_block->payload_capacity +=
                                        total_next_block_size;
                                /* skip over consecutive free block */
                                curr_block->next_block_header =
                                        next_block->next_block_header;
                                /* combined might be too big, optionally
                                        split again */
                                user_payload_ptr = splitblock(
                                        curr_block, requested_rounded_size
                                );
                                
                                print_debug_msg(
                                        REALLOC_ID, ptr, user_payload_ptr, 0,
                                        size
                                );
                                return user_payload_ptr;
                        }
                } 
                /* otherwise if combined not big enough, or no next block 
                        - malloc, copy data, free original */
                /* malloc returns beginning of new payload */
                print_custom_debug_msg("REALLOC: copying, calling malloc\n");
                suppress_debug = true;
                user_payload_ptr = malloc(size);
                suppress_debug = false;

                if (user_payload_ptr == NULL) {
                        print_custom_debug_msg("REALLOC: malloc failed\n");
                        print_debug_msg(
                                REALLOC_ID, ptr, user_payload_ptr, 0, size
                        );
                        return NULL;
                }
                
                /* src starts at the old payload; copy only the old payload
                        bytes into the newly allocated destination block. */
                src = (void *) ((char *) curr_block + header_size);

                memcpy(user_payload_ptr, src, curr_payload_cap);
                print_custom_debug_msg("REALLOC: copied bytes\n");

                suppress_debug = true;
                free(ptr);
                suppress_debug = false;

                print_debug_msg(REALLOC_ID, ptr, user_payload_ptr, 0, size);
        }
        return user_payload_ptr;
}

/*
 * Splits a block when enough space remains for a new free block header plus
 * a minimum payload; otherwise leaves the block intact.
 */
static void *splitblock(
        struct BlockHeader *curr_block, size_t requested_rounded_size
) {
        size_t header_size;
        size_t minimum_block_size;
        size_t leftover_size;
        void *user_payload_ptr;

        print_custom_debug_msg("attempting to split block\n");

        /* if leftover is big enough for minimum block size, then make a
                leftover node and put on heap */
        /* round header and payload to nearest 16 */
        header_size = round_up_16(sizeof(struct BlockHeader));
        minimum_block_size = header_size + MINIMUM_PAYLOAD_SIZE;

        leftover_size = curr_block->payload_capacity - requested_rounded_size;
        if (leftover_size >= minimum_block_size) {
                struct BlockHeader *leftover_block;

                /* shrink the used node */
                curr_block->payload_capacity = requested_rounded_size;

                /* The leftover header starts immediately after the resized
                        payload. Use byte-wise arithmetic so offsets are
                        measured in bytes rather than struct-sized units. */
                leftover_block = (struct BlockHeader *) (
                        (char *) curr_block + header_size +
                        requested_rounded_size
                );
                leftover_block->payload_capacity = leftover_size - header_size;
                leftover_block->is_free = true;
                leftover_block->next_block_header =
                        curr_block->next_block_header;
                
                /* chain the leftover block to the used one */
                curr_block->next_block_header = leftover_block;
                print_custom_debug_msg("successfully split block\n");

                /* note: not the most optimal, since worst case is 2n,
                if I have time I will optimize by implementing a doubly linked
                list */
                merge_free_nodes();
        }
        /* otherwise, if used all available mem in node, don't need to change
                next pointers */

        user_payload_ptr = (char *) curr_block + header_size;
        return user_payload_ptr;
}

/*
 * Returns the block whose payload range contains ptr, or NULL if ptr lies
 * outside all tracked payload regions.
 */
static struct BlockHeader *findblock(void *ptr) {
        struct BlockHeader *curr_block;
        size_t header_size;
        char *beg_payload_region;
        char *end_payload_region;
        char *target_ptr;

        print_custom_debug_msg("finding block\n");

        if (ptr == NULL) {
                print_custom_debug_msg(
                        "failed to find block because ptr is null\n"
                );
                return NULL;
        }
        
        curr_block = heap_head;
        header_size = round_up_16(sizeof(struct BlockHeader));
        target_ptr = (char *) ptr;

        while (curr_block != NULL) {
                beg_payload_region = (char *) curr_block + header_size;
                end_payload_region = beg_payload_region +
                        curr_block->payload_capacity;
                if (beg_payload_region <= target_ptr &&
                        target_ptr < end_payload_region) {
                        /* success case: found block */
                        print_custom_debug_msg("successfully found block\n");
                        return curr_block;
                } else if (target_ptr < beg_payload_region) {
                        /* early stop: haven't found if target ptr is less than
                                beginning payload address */
                        print_custom_debug_msg("failed to find block\n");
                        return NULL;
                }
                curr_block = curr_block->next_block_header;
        }
        /* failure case: traversed whole list and haven't found block */
        print_custom_debug_msg("failed to find block\n");
        return NULL;
}

/*
 * Walks the block list and merges consecutive free nodes into the first
 * free block in each run.
 */
static void merge_free_nodes(void){
        struct BlockHeader *curr_block;
        struct BlockHeader *first_free;
        size_t header_size;

        print_custom_debug_msg("attempting to merge free nodes\n");

        curr_block = heap_head;
        first_free = NULL;
        header_size = round_up_16(sizeof(struct BlockHeader));

        while (curr_block != NULL) {
                /* find first free block */
                if (first_free == NULL && curr_block->is_free) {
                        first_free = curr_block;
                } else if (first_free != NULL && curr_block->is_free) {
                        /* if already found first free block */
                        /* increase first_free's capacity by this new
                                consecutive block */
                        first_free->payload_capacity += header_size +
                                curr_block->payload_capacity;
                        /* skip over consecutive free block */
                        first_free->next_block_header =
                                curr_block->next_block_header;
                        curr_block = first_free;
                        print_custom_debug_msg("merged free nodes\n");
                } else if (first_free != NULL && !curr_block->is_free) {
                        /* consecutive free blocks stop */
                        first_free = NULL;
                }
                curr_block = curr_block->next_block_header;
        }
}

/*
 * Rounds size up to the next multiple of 16 bytes.
 */
static size_t round_up_16(size_t size) {
        return ((size + 15) / 16) * 16;
}

/*
 * Prints the structured malloc/free/calloc/realloc debug output required by
 * the assignment when DEBUG_MALLOC is set.
 */
static void print_debug_msg(
        int id, void *ptr, void *new_ptr, size_t nmemb, size_t size
) {
        bool old_suppress;
        char buf[256];
        int len;
        size_t result_size;
        struct BlockHeader *curr_block;

        if (suppress_debug || getenv("DEBUG_MALLOC") == NULL) {
                return;
        }

        old_suppress = suppress_debug;
        suppress_debug = true;
        /* test in two cases: add more debug outputs */
        result_size = 0;

        curr_block = findblock(new_ptr);
        if (curr_block != NULL) {
                result_size = curr_block->payload_capacity;
        }

        switch (id) {
        case MALLOC_ID:
                len = snprintf(
                        buf, sizeof(buf),
                        "MALLOC: malloc(%zu)\n=> (ptr=%p, size=%zu)\n",
                        size, new_ptr, result_size
                );
                break;
        case FREE_ID:
                len = snprintf(buf, sizeof(buf), "MALLOC: free(%p)\n", 
                        ptr);
                break;
        case CALLOC_ID:
                len = snprintf(
                        buf, sizeof(buf),
                        "MALLOC: calloc(%zu,%zu)\n=> (ptr=%p, size=%zu)\n",
                        nmemb, size, new_ptr, result_size
                );
                break;
        case REALLOC_ID:
                len = snprintf(
                        buf, sizeof(buf),
                        "MALLOC: realloc(%p,%zu) => (ptr=%p, size=%zu)\n",
                        ptr, size, new_ptr, result_size
                );
                break;
        default:
                len = snprintf(
                        buf, sizeof(buf),
                        "no available debug message for this mode\n"
                );
        }

        if (len < 0) {
                suppress_debug = old_suppress;
                return;
        }
        if (len > (int)sizeof(buf) - 1) {
                len = sizeof(buf) - 1;
        }
        write(2, buf, len);

        suppress_debug = old_suppress;
}


/*
 * Prints additional free-form debugging text while following the same
 * DEBUG_MALLOC and recursion-suppression rules as print_debug_msg().
 */
static void print_custom_debug_msg(const char *msg) {
        bool old_suppress;
        char buf[256];
        int len;

        if (suppress_debug || getenv("DEBUG_MALLOC") == NULL) {
                return;
        }

        old_suppress = suppress_debug;
        suppress_debug = true;
        
        len = snprintf(buf, sizeof(buf), "%s", msg);

        if (len < 0) {
                suppress_debug = old_suppress;
                return;
        }
        if (len > (int)sizeof(buf) - 1) {
                len = sizeof(buf) - 1;
        }
        write(2, buf, len);

        suppress_debug = old_suppress;
}
