; NASM implementation of distance duplicate test (8 distances) using AVX2 gathers.
; Signature: int test_any_dup8_avx2_nasm(const uint64_t *bs, const int *dist8);
;   rdi = bitset base pointer, rsi = pointer to eight int32 distances
; Returns eax = 1 if any duplicate bit is already set, else 0.

section .text
global test_any_dup8_avx2_nasm

test_any_dup8_avx2_nasm:
    xor     eax, eax

    ; Load distances (8 x int32) into ymm0
    vmovdqu ymm0, [rsi]

    ; Compute word indices = dist >> 6 (still int32)
    vpsrld  ymm1, ymm0, 6

    ; Compute bit offsets = dist & 63
    mov     ecx, 63
    vmovd   xmm7, ecx
    vpbroadcastd ymm7, xmm7
    vpand   ymm4, ymm0, ymm7

    ; Split word indices for AVX2 gathers (two 128-bit halves)
    vextracti128 xmm0, ymm1, 1          ; hi 4 indices in xmm0, low remain in xmm1

    ; Gather 4 qwords for low indices
    vpcmpeqd ymm6, ymm6, ymm6           ; set mask to all-ones
    vpgatherdq ymm2, [rdi + xmm1*8], ymm6

    ; Gather 4 qwords for high indices
    vpcmpeqd ymm6, ymm6, ymm6
    vpgatherdq ymm3, [rdi + xmm0*8], ymm6

    ; Build ones vector for mask construction
    vpcmpeqd ymm5, ymm5, ymm5
    vpsrlq  ymm5, ymm5, 63              ; each lane = 1

    ; Low mask lanes (bits 0..3)
    vpmovzxdq ymm6, xmm4                ; zero-extend 4x int32 -> 4x int64
    vpsllvq ymm6, ymm5, ymm6            ; 1ULL << bit
    vpand   ymm6, ymm2, ymm6            ; apply to gathered words

    ; High mask lanes (bits 4..7)
    vextracti128 xmm0, ymm4, 1
    vpmovzxdq ymm7, xmm0
    vpsllvq ymm7, ymm5, ymm7
    vpand   ymm7, ymm3, ymm7

    ; Combine and test
    vpor    ymm6, ymm6, ymm7
    vptest  ymm6, ymm6
    setnz   al
    vzeroupper
    ret
