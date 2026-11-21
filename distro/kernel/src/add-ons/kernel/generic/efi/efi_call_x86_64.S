/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*	Calls an EFI runtime service in the Microsoft x64 convention on a stack
	of its own (docs/EFI-RUNTIME.md). Interrupts are off and the module lock
	is held, so one saved stack pointer is enough.

	uint64 efi_call5(uint64 function, uint64 arg1, uint64 arg2, uint64 arg3,
		uint64 arg4, uint64 arg5, uint64 stackTop)

	System V in:	rdi, rsi, rdx, rcx, r8, r9, 16(%rbp)
	Microsoft out:	rcx, rdx, r8, r9, 32(%rsp), with 32 bytes of shadow space
		and %rsp 16-byte aligned at the call.
*/


#include <asm_defs.h>


	.text

FUNCTION(efi_call5):
	push	%rbp
	mov		%rsp, %rbp
	// The firmware keeps these in its convention, but on a fault it does
	// not return; restore them ourselves.
	push	%rbx
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	mov		%rsp, efi_saved_stack(%rip)

	mov		16(%rbp), %rax
	and		$-16, %rax
	mov		%rax, %rsp
	sub		$48, %rsp
	mov		%r9, 32(%rsp)
	mov		%r8, %r9
	mov		%rcx, %r8
	mov		%rsi, %rcx
	// %rdx is already the second argument
	mov		%rdi, %rax
	cld
	call	*%rax

.Lrestore:
	mov		efi_saved_stack(%rip), %rsp
	pop		%r15
	pop		%r14
	pop		%r13
	pop		%r12
	pop		%rbx
	pop		%rbp
	ret
FUNCTION_END(efi_call5)


/*	The kernel fault handlers jump here (r5_firmware_fault() in arch_int.cpp,
	through gR5FirmwareFaultHandler) when the firmware faults; the firmware's
	stack is dropped. */
FUNCTION(efi_call_fault):
	mov		$-1, %rax
	jmp		.Lrestore
FUNCTION_END(efi_call_fault)


	.bss
	.align	8
efi_saved_stack:
	.skip	8
