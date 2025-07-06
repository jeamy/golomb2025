#include <immintrin.h>
#include <stdint.h>

/*
 * int test_any_dup8_avx2_gather(const uint64_t *bs, const int *dist8);
 * Returns 1 if ANY of the 8 distances already exists in bitset.
 * Requires AVX2; marked with function attribute so it is placed in AVX2
 * sub-target and only executed after a CPUID check.
 */
__attribute__((target("avx2")))
int test_any_dup8_avx2_gather(const uint64_t *bs, const int *dist8)
{
    /* Step 1: load 8 distances */
    __m256i vdist = _mm256_loadu_si256((const __m256i *)dist8);

    /* Step 2: compute 64-bit word indices = dist >> 6 */
    __m256i vword_idx = _mm256_srli_epi32(vdist, 6);

    /* AVX2 gather works on 4 indices (128-bit). Split vector. */
    __m128i idx_lo = _mm256_castsi256_si128(vword_idx);         /* first 4 */
    __m128i idx_hi = _mm256_extracti128_si256(vword_idx, 1);    /* last 4  */

    /* Step 3: gather corresponding bitset words (scale = 8 bytes) */
    __m256i words_lo = _mm256_i32gather_epi64((const long long *)bs, idx_lo, 8);
    __m256i words_hi = _mm256_i32gather_epi64((const long long *)bs, idx_hi, 8);

    /* Step 4: combine into array for scalar bit test (AVX2 lacks per-lane shift) */
    uint64_t words[8];
    _mm256_storeu_si256((__m256i *)words, words_lo);
    _mm256_storeu_si256((__m256i *)(words + 4), words_hi);

    /* Step 5: scalar bit test (branchless). Early-out on first duplicate. */
    for (int i = 0; i < 8; ++i) {
        uint32_t dist = (uint32_t)dist8[i];
        uint32_t bit = dist & 63;
        uint64_t mask = 1ULL << bit;
        if (words[i] & mask)
            return 1;
    }
    return 0;
}
