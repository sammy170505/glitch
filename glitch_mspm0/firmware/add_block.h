#ifndef ADD_BLOCK_H
#define ADD_BLOCK_H

/*
 * 200 individual volatile increments -- NOT a loop.
 *
 * Per Astra's method: a long straight run of load/add/store keeps the CPU
 * executing an ADD almost 100% of the time, so almost any successful fault
 * (skipped store, corrupted register, PC perturbation) shows up as a final
 * count != 200. A `for` loop would spend cycles on the branch/compare and
 * shrink the vulnerable fraction, so we unroll it fully.
 *
 * Requires `a` to be declared `volatile int` in the enclosing scope so the
 * compiler cannot fold the 200 increments into `a += 200` or cache `a` in a
 * register -- every increment must be a real memory load/add/store.
 */

#define A1   a += 1;
#define A5   A1 A1 A1 A1 A1
#define A25  A5 A5 A5 A5 A5
#define A100 A25 A25 A25 A25
#define A200 A100 A100            /* ADD_COUNT below must match this */

#define ADD_COUNT 200

#endif /* ADD_BLOCK_H */
