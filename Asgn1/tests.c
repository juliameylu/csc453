#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_failed = 0;

static void check_true(const char *name, int cond) {
        char buf[256];
        int len;

        tests_run++;
        if (cond) {
                len = snprintf(buf, sizeof(buf), "ok: %s\n", name);
        } else {
                len = snprintf(buf, sizeof(buf), "FAIL: %s\n", name);
                tests_failed++;
        }

        if (len > 0) {
                if (len > (int) sizeof(buf) - 1) {
                        len = sizeof(buf) - 1;
                }
                write(1, buf, len);
        }
}

static void test_malloc_alignment(void) {
        void *ptr1 = malloc(1);
        void *ptr2 = malloc(17);

        check_true("malloc(1) returns non-NULL", ptr1 != NULL);
        check_true("malloc(17) returns non-NULL", ptr2 != NULL);
        check_true("malloc(1) is 16-byte aligned",
                ptr1 != NULL && ((uintptr_t) ptr1 % 16) == 0);
        check_true("malloc(17) is 16-byte aligned",
                ptr2 != NULL && ((uintptr_t) ptr2 % 16) == 0);

        free(ptr1);
        free(ptr2);
}

static void test_free_edge_cases(void) {
        int stack_value = 7;
        void *bad_ptr = &stack_value;
        void *ptr = malloc(32);

        check_true("malloc for free edge cases returns non-NULL", ptr != NULL);

        free(NULL);
        check_true("free(NULL) did not crash", 1);

        free(bad_ptr);
        check_true("free(stack pointer) did not crash", 1);

        free(ptr);
        free(ptr);
        check_true("double free did not crash", 1);
}

static void test_internal_pointer_free(void) {
        char *ptr = malloc(64);
        char *next;

        check_true("malloc for internal free returns non-NULL", ptr != NULL);
        if (ptr == NULL) {
                return;
        }

        memset(ptr, 'A', 64);
        free(ptr + 8);
        next = malloc(64);

        check_true("free on interior pointer reclaimed block", next == ptr);
        free(next);
}

static void test_realloc_same_and_shrink(void) {
        char *ptr = malloc(80);
        char *same;
        char *shrunk;
        int i;

        check_true("malloc for realloc tests returns non-NULL", ptr != NULL);
        if (ptr == NULL) {
                return;
        }

        for (i = 0; i < 80; i++) {
                ptr[i] = (char) ('a' + (i % 26));
        }

        same = realloc(ptr, 80);
        check_true("realloc same size keeps pointer", same == ptr);

        shrunk = realloc(same, 24);
        check_true("realloc shrink keeps pointer", shrunk == same);
        check_true("realloc shrink preserves contents",
                shrunk[0] == 'a' && shrunk[10] == 'k');

        free(shrunk);
}

static void test_realloc_grow_at_heap_end(void) {
        char *ptr = malloc(32);
        char *grown;
        int i;
        int preserved = 1;

        check_true("malloc for tail realloc returns non-NULL", ptr != NULL);
        if (ptr == NULL) {
                return;
        }

        for (i = 0; i < 32; i++) {
                ptr[i] = (char) i;
        }

        grown = realloc(ptr, 4096);
        check_true("realloc at heap end returns non-NULL", grown != NULL);
        if (grown == NULL) {
                return;
        }

        for (i = 0; i < 32; i++) {
                if (grown[i] != (char) i) {
                        preserved = 0;
                        break;
                }
        }

        check_true("realloc at heap end preserves contents", preserved);
        free(grown);
}

static void test_realloc_forced_move(void) {
        char *ptr1 = malloc(32);
        char *ptr2 = malloc(32);
        char *moved;
        int i;
        int preserved = 1;

        check_true("malloc first block for forced move returns non-NULL",
                ptr1 != NULL);
        check_true("malloc second block for forced move returns non-NULL",
                ptr2 != NULL);
        if (ptr1 == NULL || ptr2 == NULL) {
                free(ptr1);
                free(ptr2);
                return;
        }

        for (i = 0; i < 32; i++) {
                ptr1[i] = (char) ('A' + i);
        }

        moved = realloc(ptr1, 2048);
        check_true("forced-move realloc returns non-NULL", moved != NULL);
        if (moved == NULL) {
                free(ptr2);
                return;
        }

        for (i = 0; i < 32; i++) {
                if (moved[i] != (char) ('A' + i)) {
                        preserved = 0;
                        break;
                }
        }

        check_true("forced-move realloc preserves contents", preserved);
        free(ptr2);
        free(moved);
}

static void test_calloc_zeroes_requested_bytes(void) {
        unsigned char *ptr = calloc(32, sizeof(unsigned char));
        int i;
        int zeroed = 1;

        check_true("calloc returns non-NULL", ptr != NULL);
        if (ptr == NULL) {
                return;
        }

        for (i = 0; i < 32; i++) {
                if (ptr[i] != 0) {
                        zeroed = 0;
                        break;
                }
        }

        check_true("calloc zeroes requested bytes", zeroed);
        free(ptr);
}

static void test_realloc_special_cases(void) {
        char *ptr = realloc(NULL, 48);

        check_true("realloc(NULL, size) returns non-NULL", ptr != NULL);
        if (ptr != NULL) {
                memset(ptr, 1, 48);
        }

        ptr = realloc(ptr, 0);
        check_true("realloc(ptr, 0) returns NULL", ptr == NULL);
}

int main(void) {
        char buf[64];
        int len;

        test_malloc_alignment();
        test_free_edge_cases();
        test_internal_pointer_free();
        test_realloc_same_and_shrink();
        test_realloc_grow_at_heap_end();
        test_realloc_forced_move();
        test_calloc_zeroes_requested_bytes();
        test_realloc_special_cases();

        len = snprintf(
                buf, sizeof(buf), "\nran %d checks, %d failed\n",
                tests_run, tests_failed
        );
        if (len > 0) {
                if (len > (int) sizeof(buf) - 1) {
                        len = sizeof(buf) - 1;
                }
                write(1, buf, len);
        }
        return tests_failed == 0 ? 0 : 1;
}
