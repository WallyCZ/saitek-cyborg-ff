;this is only pseudocode for documenting all changes made

SECTION .text

overwrite_0x10001303:
                call    patch_1303

overwrite_0x100013A0:
                call    patch_13A0

overwrite_0x10001402:
                nop
                nop

overwrite_0x1000140C:
                jmp     patch_1402

overwrite_0x10001426:
                call    patch_1426
                nop
                nop

overwrite_0x10003156:
                call    patch_3156
                nop

overwrite_0x10003244:
                call    patch_3244
                nop

SECTION .patch       ; patch section

patch_1303:
                pushfq
                mov     rsi, rcx
                shr     rsi, 20h
                shl     rsi, 20h
                or      rsi, rdx
                mov     rbx, rcx
                popfq
                retn

patch_13A0:
                mov     esi, r9d
                mov     rbx, rcx
                pushfq
                shr     rbx, 20h
                shl     rbx, 20h
                or      rbx, rdx
                popfq
                retn

patch_1402:
                pushfq
                shr     rcx, 20h
                shl     rcx, 20h
                or      rcx, rdx
                popfq
                jmp     sub_10006B90

patch_3156:
                pushfq
                shr     rbx, 20h
                shl     rbx, 20h
                or      rbx, r8
                mov     rsi, rcx
                popfq
                retn

patch_3244:
                mov     r8, r9
                pushfq
                mov     rdx, rcx
                shr     rdx, 20h
                shl     rdx, 20h
                or      rdx, r10
                popfq
                retn

patch_1426:
                test    rdx, rdx
                mov     rbx, r8
                mov     rcx, rdx
                retn
