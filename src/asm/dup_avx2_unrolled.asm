; FASM implementation of distance duplicate test (8 distances)
; Mirrors scalar C logic using BT, fully unrolled (no AVX requirement).
; Signature: int test_any_dup8_avx2_asm(const uint64_t *bs, const int *dist8);
;   rdi = bs pointer, rsi = dist8 pointer
; Returns eax = 1 if any distance already present in the bitset, else 0.

format ELF64
public test_any_dup8_avx2_asm

section '.text' executable

macro test_dist offset {
    mov     edx, [rsi + offset]   ; load dist
    mov     ecx, edx              ; bit index
    and     ecx, 63
    mov     r8d, edx              ; word index
    shr     r8d, 6
    mov     r9, [rdi + r8*8]      ; load 64-bit word
    bt      r9, rcx
    jc      .dup_found
}

test_any_dup8_avx2_asm:
    xor     eax, eax              ; default return 0

    test_dist 0
    test_dist 4
    test_dist 8
    test_dist 12
    test_dist 16
    test_dist 20
    test_dist 24
    test_dist 28

    ret

.dup_found:
    mov     eax, 1
    ret
