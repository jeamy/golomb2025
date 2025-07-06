; Hand-written hot-spot distance duplicate test for Golomb solver
; Build with: fasm dup_avx2.asm dup_avx2.o (Makefile rule will handle)
;
; int test_any_dup8_avx2_asm(const uint64_t *bs, const int *dist8);
;   rdi = bs   (pointer to bitset words)
;   rsi = dist8 (pointer to eight int32 distances)
; Returns EAX = 1 if ANY duplicate distance already set, 0 otherwise.
;
; Ensure linker marks stack as non-executable to silence GNU ld warning

; Simple scalar implementation using BT instruction – fast & valid on all x86-64.
; (Can be replaced later with AVX2 gather optimisation.)

format ELF64
public test_any_dup8_avx2_asm
section '.text' executable

test_any_dup8_avx2_asm:
    ; i = 0 .. 7
    xor     eax, eax        ; default return 0 (no dup)
    mov     rcx, 0          ; loop counter
.next:
    cmp     rcx, 8
    jge     .done           ; processed 8 distances
    ; load distance into edx
    mov     edx, [rsi + rcx*4]   ; int32 value
    ; compute word index in rax = dist >> 6
    mov     eax, edx
    shr     eax, 6
    ; load corresponding 64-bit word from bitset into r8
    mov     r8, [rdi + rax*8]
    ; bit index in ecx = dist & 63 (reuse ecx temporarily)
    mov     ecx, edx
    and     ecx, 63
    ; test bit – BT sets CF
    bt      r8, rcx
    jc      .dup_found      ; CF =1 => bit already set
    ; next iteration
    inc     rcx
    jmp     .next

.dup_found:
    mov     eax, 1
.done:
    ret
