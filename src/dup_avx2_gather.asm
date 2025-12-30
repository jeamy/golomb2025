; Unrolled distance duplicate test (8 distances) - NASM version
; int test_any_dup8_avx2_nasm(const uint64_t *bs, const int *dist8);
;   rdi = bs    (pointer to bitset words)
;   rsi = dist8 (pointer to eight int32 distances)
; Return EAX = 1 if any duplicate found else 0.
; Fully unrolled SIMD AVX2 gather+variable shift+vptest implementation.

; Build: nasm -f elf64 -o dup_avx2_gather_nasm.o dup_avx2_gather.asm

section .text
global test_any_dup8_avx2_nasm

test_any_dup8_avx2_nasm:
    xor     eax, eax
    ; Load 8x int32 distances
    vmovdqu ymm0, [rsi]

    ; word indices (dist >> 6), 8x int32
    vpsrld  ymm1, ymm0, 6

    ; bit indices (dist & 63), 8x int32
    mov     ecx, 63
    vmovd   xmm7, ecx
    vpbroadcastd ymm7, xmm7
    vpand   ymm4, ymm0, ymm7

    ; Split indices for AVX2 gather (4 lanes at a time)
    ; Keep hi indices out of xmm2, because ymm2 will be used as gather destination.
    vextracti128 xmm0, ymm1, 1

    ; Gather 4x qword words (lo)
    vpcmpeqd ymm6, ymm6, ymm6
    vpgatherdq ymm2, qword [rdi + xmm1*8], ymm6

    ; Gather 4x qword words (hi)
    vpcmpeqd ymm6, ymm6, ymm6
    vpgatherdq ymm3, qword [rdi + xmm0*8], ymm6

    ; ones vector: 1 in each qword lane
    vpcmpeqd ymm5, ymm5, ymm5
    vpsrlq  ymm5, ymm5, 63

    ; lo masks: ymm6 = 1 << (bit0..3)
    vpmovzxdq ymm6, xmm4
    vpsllvq ymm6, ymm5, ymm6
    vpand   ymm6, ymm2, ymm6

    ; hi masks: ymm7 = 1 << (bit4..7)
    vextracti128 xmm0, ymm4, 1
    vpmovzxdq ymm7, xmm0
    vpsllvq ymm7, ymm5, ymm7
    vpand   ymm7, ymm3, ymm7

    ; combine + test
    vpor    ymm6, ymm6, ymm7
    vptest  ymm6, ymm6
    setnz   al
    vzeroupper
    ret
