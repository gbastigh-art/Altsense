#pragma once

#include "util/types.h"


// list of first 10000 primes, up to 104729
#define NUM_PRIMES 10000

#define MAX_PRIME (PRIMES[NUM_PRIMES - 1])

extern int PRIMES[NUM_PRIMES];

// find random prime > n, good up to MAX_PRIME
int prime_random_above(rand_t *rand, int n);
