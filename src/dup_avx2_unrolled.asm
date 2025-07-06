; Unrolled version of distance duplicate test (8 distances, no branches)
; int test_any_dup8_avx2_unrolled(const uint64_t *bs, const int *dist8);
;   rdi = bs    (pointer to bitset words)
;   rsi = dist8 (pointer to eight int32 distances)
; Return EAX = 1 if any duplicate found else 0.
; Uses no AVX – pure integer, but eliminates loop & branches.

format ELF64
test_any_dup8_avx2_asm equ test_any_dup8_avx2_unrolled
;public test_any_dup8_avx2_unrolled
public test_any_dup8_avx2_asm
section '.text' executable

test_any_dup8_avx2_unrolled:
    xor     eax, eax        ; default 0

    ; ---- distance 0 ----
    mov     edx, [rsi]      ; dist0
    mov     ecx, edx        ; bit idx
    shr     edx, 6          ; word index
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 1 ----
    mov     edx, [rsi + 4]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 2 ----
    mov     edx, [rsi + 8]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 3 ----
    mov     edx, [rsi + 12]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 4 ----
    mov     edx, [rsi + 16]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 5 ----
    mov     edx, [rsi + 20]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 6 ----
    mov     edx, [rsi + 24]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ; ---- distance 7 ----
    mov     edx, [rsi + 28]
    mov     ecx, edx
    shr     edx, 6
    mov     r8,  [rdi + rdx*8]
    and     ecx, 63
    bt      r8, rcx
    jc      .dup

    ret
.dup:
    mov     eax, 1
    ret
