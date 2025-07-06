#include <immintrin.h>
#include <stdint.h>

/*
 * AVX-512 distance duplicate test for 8 distances.
 * Uses 64-bit gather + per-lane variable shifts (vpsllvq) to build masks
 * entirely in vector registers – no scalar loop.
 * Returns 1 if ANY duplicate distance found, else 0.
 *
 * Compile independently via function target so the object file is always
 * generated, but the function is executed only on CPUs with AVX-512F.
 */
__attribute__((target("avx512f,avx512vl,avx512dq")))
int test_any_dup8_avx512(const uint64_t *bs, const int *dist8)
{
    /* load 8 distances */
    __m256i vdist32 = _mm256_loadu_si256((const __m256i *)dist8);          /* 8 x int32 */

    /* word indices = dist >> 6 (each fits in 32 bits) */
    __m256i vword_idx = _mm256_srli_epi32(vdist32, 6);

    /* gather 8 words (64-bit each) */
    __m512i gathered = _mm512_i32gather_epi64(vword_idx, (const long long *)bs, 8);

    /* bit offsets = dist & 63 */
    __m256i vbit_off32 = _mm256_and_si256(vdist32, _mm256_set1_epi32(63));

    /* extend to 64-bit counts */
    __m512i vbit_off64 = _mm512_cvtepu32_epi64(vbit_off32);

    /* build per-lane mask = 1ULL << bit */
    __m512i vone = _mm512_set1_epi64(1);
    __m512i masks = _mm512_sllv_epi64(vone, vbit_off64); /* VPSLLVQ */

    /* AND and reduce */
    __m512i dup = _mm512_and_epi64(gathered, masks);

    /* test if any lane non-zero */
    return _mm512_test_epi64_mask(dup, dup) != 0;
}
