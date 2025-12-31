; solver_mp_v2.asm - Komplett neue NASM Implementation des Golomb Solvers
; Basierend auf der C-Logik in solver.c, optimiert und korrekt

bits 64
default rel

section .data
    align 8

section .bss
    align 64
    pos_buf:     resd 32        ; int pos[MAX_MARKS]
    dist_buf:    resq 11        ; uint64_t dist_bs[BS_WORDS=11]

section .text

; Konstanten
%define MAX_MARKS 32
%define BS_WORDS 11
%define MAX_LEN_BITSET 600

; Externe C-Funktionen
extern printf
extern memcpy
extern getenv
extern lut_lookup_by_marks
extern test_any_dup8_avx2_gather
extern g_use_simd

; Exportierte Funktionen
global solve_golomb_mt_asm

; ============================================================================
; Bitset-Hilfsfunktionen
; ============================================================================

; void set_bit(uint64_t *bs, int idx)
; rdi = bs, esi = idx
set_bit:
    mov     eax, esi
    shr     eax, 6              ; word_idx = idx >> 6
    and     esi, 63             ; bit_pos = idx & 63
    mov     ecx, esi
    mov     rdx, 1
    shl     rdx, cl             ; mask = 1ULL << bit_pos
    or      [rdi + rax*8], rdx
    ret

; void clr_bit(uint64_t *bs, int idx)
; rdi = bs, esi = idx
clr_bit:
    mov     eax, esi
    shr     eax, 6
    and     esi, 63
    mov     ecx, esi
    mov     rdx, 1
    shl     rdx, cl
    not     rdx
    and     [rdi + rax*8], rdx
    ret

; int test_bit_scalar(const uint64_t *bs, int idx)
; rdi = bs, esi = idx
; return: eax = 0 or 1
test_bit_scalar:
    mov     eax, esi
    shr     eax, 6
    and     esi, 63
    mov     ecx, esi
    mov     rax, [rdi + rax*8]
    shr     rax, cl
    and     eax, 1
    ret

; ============================================================================
; DFS - Rekursive Suche
; ============================================================================
; bool dfs(int depth, int n, int target_len, int *pos, uint64_t *dist_bs, bool verbose)
; Args: edi=depth, esi=n, edx=target_len, rcx=pos*, r8=dist_bs*, r9d=verbose
; Return: eax = 0 (false) or 1 (true)

dfs:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 264            ; locals + 16-byte alignment

    ; Argumente sichern
    mov     r15d, edi           ; depth
    mov     r14d, esi           ; n
    mov     r13d, edx           ; target_len
    mov     r12, rcx            ; pos*
    mov     rbx, r8             ; dist_bs*
    ; r9d = verbose (wird direkt verwendet)

    ; if (depth == n) return pos[n-1] == target_len
    cmp     r15d, r14d
    jne     .not_done
    mov     eax, r14d
    dec     eax
    mov     eax, [r12 + rax*4]
    cmp     eax, r13d
    sete    al
    movzx   eax, al
    jmp     .epilogue

.not_done:
    ; last = pos[depth - 1]
    mov     eax, r15d
    dec     eax
    mov     r10d, [r12 + rax*4] ; r10d = last

    ; Prune: if (last + (n - depth) > target_len) return false
    mov     eax, r14d
    sub     eax, r15d           ; n - depth
    add     eax, r10d           ; last + (n - depth)
    cmp     eax, r13d
    jg      .ret_false

    ; max_next = target_len - (n - depth - 1)
    mov     eax, r14d
    sub     eax, r15d
    dec     eax                 ; n - depth - 1
    mov     ecx, r13d
    sub     ecx, eax            ; ecx = max_next

    ; Symmetry break für depth==1
    cmp     r15d, 1
    jne     .no_sym_break
    mov     eax, r13d
    shr     eax, 1              ; limit = target_len / 2
    mov     edx, r10d
    inc     edx                 ; last + 1
    cmp     eax, edx
    cmovl   eax, edx            ; limit = max(limit, last+1)
    cmp     ecx, eax
    cmovg   ecx, eax            ; max_next = min(max_next, limit)

.no_sym_break:
    ; Loop: for (next = last+1; next <= max_next; next++)
    mov     r11d, r10d
    inc     r11d                ; r11d = next (start at last+1)

.next_loop:
    cmp     r11d, ecx
    jg      .ret_false

    ; gap = next - last
    mov     eax, r11d
    sub     eax, r10d           ; eax = gap

    ; Bounds check für gap
    cmp     eax, MAX_LEN_BITSET
    jge     .skip_candidate

    ; Fast pre-check: test_bit_scalar(dist_bs, gap)
    push    rcx
    push    r9
    mov     rdi, rbx
    mov     esi, eax
    call    test_bit_scalar
    pop     r9
    pop     rcx
    test    eax, eax
    jnz     .skip_candidate

    ; Compute dists[i] = next - pos[i] for i in [0..depth)
    lea     rax, [rbp - 304]    ; dists array
    xor     esi, esi            ; i = 0
.compute_dists:
    cmp     esi, r15d
    jge     .dists_done
    mov     edx, r11d
    sub     edx, [r12 + rsi*4]  ; next - pos[i]
    mov     [rax + rsi*4], edx
    inc     esi
    jmp     .compute_dists

.dists_done:
    ; Check for duplicates (scalar path only for simplicity)
    xor     esi, esi            ; i = 0
.check_loop:
    cmp     esi, r15d
    jge     .all_ok
    
    mov     edx, [rbp - 304 + rsi*4]  ; dists[i]
    
    ; Bounds check
    cmp     edx, MAX_LEN_BITSET
    jge     .skip_candidate
    
    push    rcx
    push    rsi
    push    r9
    push    rdi
    mov     rdi, rbx
    mov     esi, edx
    call    test_bit_scalar
    pop     rdi
    pop     r9
    pop     rsi
    pop     rcx
    test    eax, eax
    jnz     .skip_candidate
    
    inc     esi
    jmp     .check_loop

.all_ok:
    ; Commit: pos[depth] = next
    mov     [r12 + r15*4], r11d

    ; Set bits for all dists[i]
    xor     esi, esi
.set_loop:
    cmp     esi, r15d
    jge     .set_done
    
    mov     edx, [rbp - 304 + rsi*4]
    push    rcx
    push    rsi
    push    r9
    push    rdi
    mov     rdi, rbx
    mov     esi, edx
    call    set_bit
    pop     rdi
    pop     r9
    pop     rsi
    pop     rcx
    
    inc     esi
    jmp     .set_loop

.set_done:
    ; Recursive call: dfs(depth+1, n, target_len, pos, dist_bs, verbose)
    ; Preserve loop invariants across recursion (caller-saved regs)
    ; rcx=max_next, r9=verbose, r10=last, r11=next
    sub     rsp, 32
    mov     [rsp], rcx
    mov     [rsp + 8], r9
    mov     [rsp + 16], r10
    mov     [rsp + 24], r11

    mov     edi, r15d
    inc     edi
    mov     esi, r14d
    mov     edx, r13d
    mov     rcx, r12
    mov     r8, rbx
    ; r9d already has verbose
    call    dfs

    mov     r11, [rsp + 24]
    mov     r10, [rsp + 16]
    mov     r9,  [rsp + 8]
    mov     rcx, [rsp]
    add     rsp, 32
    
    test    eax, eax
    jnz     .found_solution

    ; Rollback: clear bits
    xor     esi, esi
.clr_loop:
    cmp     esi, r15d
    jge     .clr_done
    
    mov     edx, [rbp - 304 + rsi*4]
    push    rcx
    push    rsi
    push    r9
    push    rdi
    mov     rdi, rbx
    mov     esi, edx
    call    clr_bit
    pop     rdi
    pop     r9
    pop     rsi
    pop     rcx
    
    inc     esi
    jmp     .clr_loop

.clr_done:
.skip_candidate:
    inc     r11d
    jmp     .next_loop

.found_solution:
    mov     eax, 1
    jmp     .epilogue

.ret_false:
    xor     eax, eax

.epilogue:
    add     rsp, 264
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; ============================================================================
; solve_golomb_mt_asm - Hauptfunktion
; ============================================================================
; bool solve_golomb_mt_asm(int n, int target_length, ruler_t *out, int verbose)
; Args: edi=n, esi=target_length, rdx=out*, ecx=verbose

solve_golomb_mt_asm:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8              ; Alignment

    ; Args sichern
    mov     r15d, edi           ; n
    mov     r14d, esi           ; target_length
    mov     r13, rdx            ; out*
    mov     r12d, ecx           ; verbose

    ; Bounds check
    cmp     r15d, MAX_MARKS
    jg      .ret0
    cmp     r14d, MAX_LEN_BITSET
    jg      .ret0

    ; Small n: fallback (für n<=3 wäre single-threaded besser, aber wir machen es trotzdem)
    cmp     r15d, 3
    jle     .do_search

.do_search:
    ; Initialisiere pos_buf und dist_buf
    lea     rbx, [pos_buf]
    lea     r12, [dist_buf]

    ; Zero dist_buf
    xor     rax, rax
    mov     rcx, BS_WORDS
.zero_dist:
    mov     [r12 + rcx*8 - 8], rax
    dec     rcx
    jnz     .zero_dist

    ; pos[0] = 0
    mov     dword [rbx], 0

    ; Symmetry break: second_max = target_length / 2
    mov     eax, r14d
    shr     eax, 1              ; half = target_length / 2
    mov     r10d, eax           ; r10d = second_max

    ; T = target_length - (n - 2)
    mov     eax, r15d
    sub     eax, 2
    mov     ecx, r14d
    sub     ecx, eax            ; ecx = T

    ; second_max = min(second_max, T-1)
    mov     eax, ecx
    dec     eax
    cmp     r10d, eax
    cmovg   r10d, eax

    ; second_max = max(second_max, 1)
    cmp     r10d, 1
    jge     .second_max_ok
    mov     r10d, 1
.second_max_ok:

    ; LUT fast-lane (optional, für jetzt überspringen wir das)
    ; Wir gehen direkt zur exhaustive Suche

    ; Loop: for (second = 1; second <= second_max; second++)
    mov     r8d, 1              ; r8d = second

.second_loop:
    cmp     r8d, r10d
    jg      .ret0

    ; Loop: for (third = second+1; third <= T; third++)
    mov     r9d, r8d
    inc     r9d                 ; r9d = third

.third_loop:
    cmp     r9d, ecx
    jg      .next_second

    ; Reset dist_buf
    xor     rax, rax
    push    rcx
    mov     rcx, BS_WORDS
.reset_dist:
    mov     [r12 + rcx*8 - 8], rax
    dec     rcx
    jnz     .reset_dist
    pop     rcx

    ; pos[0]=0, pos[1]=second, pos[2]=third
    mov     dword [rbx], 0
    mov     [rbx + 4], r8d
    mov     [rbx + 8], r9d

    ; Set distances: second, third, (third-second)
    push    rcx
    mov     rdi, r12
    mov     esi, r8d
    call    set_bit

    ; Pre-check duplicates for d13=third and d23=third-second
    mov     edx, r9d
    sub     edx, r8d            ; edx = d23

    mov     rdi, r12
    mov     esi, r9d            ; d13
    call    test_bit_scalar
    test    eax, eax
    jnz     .skip_third

    mov     rdi, r12
    mov     esi, edx            ; d23
    call    test_bit_scalar
    test    eax, eax
    jnz     .skip_third

    ; Commit remaining two distances
    ; NOTE: set_bit clobbers rdx (mask), so commit d23 first (stored in edx)
    mov     rdi, r12
    mov     esi, edx
    call    set_bit

    mov     rdi, r12
    mov     esi, r9d
    call    set_bit
    pop     rcx

    ; Call dfs(3, n, target_length, pos, dist_bs, false)
    ; Preserve loop regs: rcx(T), r8d(second), r9d(third)
    sub     rsp, 32
    mov     [rsp], rcx
    mov     [rsp + 8], r8
    mov     [rsp + 16], r9
    mov     edi, 3
    mov     esi, r15d
    mov     edx, r14d
    mov     rcx, rbx
    mov     r8, r12
    xor     r9d, r9d            ; verbose=false
    call    dfs
    mov     r9, [rsp + 16]
    mov     r8, [rsp + 8]
    mov     rcx, [rsp]
    add     rsp, 32

    test    eax, eax
    jz      .next_third

    ; Success! Fill out result
    mov     eax, r15d
    dec     eax
    mov     eax, [rbx + rax*4]  ; pos[n-1]
    mov     [r13], eax          ; out->length
    mov     [r13 + 4], r15d     ; out->marks

    ; memcpy(out->pos, pos_buf, n*4)
    lea     rdi, [r13 + 8]
    mov     rsi, rbx
    mov     edx, r15d
    shl     edx, 2              ; n*4
    call    memcpy

    mov     eax, 1
    jmp     .epilogue

.next_third:
    inc     r9d
    jmp     .third_loop

.skip_third:
    pop     rcx
    inc     r9d
    jmp     .third_loop

.next_second:
    inc     r8d
    jmp     .second_loop

.ret0:
    xor     eax, eax

.epilogue:
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret
