/*
 * Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2012, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Copyright 2002, Michael Noisternig. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <asm_defs.h>

#include <arch/x86/arch_altcodepatch.h>

#include "asm_offsets.h"
#include "syscall_numbers.h"


.text


/* addr_t x86_get_stack_frame(); */
FUNCTION(x86_get_stack_frame):
	mov		%rbp, %rax
	ret
FUNCTION_END(x86_get_stack_frame)


/* void x86_64_thread_entry(); */
FUNCTION(x86_64_thread_entry):
	// Get thread pointer.
	pop		%r12

	// Reset FPU state.
	movl	(gXsaveMask), %eax
	movl	(gXsaveMask+4), %edx
	leaq	THREAD_user_fpu_state(%r12), %rdi
	CODEPATCH_START
	fxrstorq	(%rdi)
	CODEPATCH_END(ALTCODEPATCH_TAG_XRSTOR)

	xorq	%rbp, %rbp

	movq	%rsp, %rax
	addq	$16, %rsp
	andq	$0xfffffffffffffff0, %rsp
	subq	$8, %rsp

	movq	8(%rax), %rdi
	jmp		*(%rax)
FUNCTION_END(x86_64_thread_entry)


/* thread exit stub */
.align 8
FUNCTION(x86_userspace_thread_exit):
	movq	%rax, %rdi
	movq	$SYSCALL_EXIT_THREAD, %rax
	syscall
.align 8
FUNCTION_END(x86_userspace_thread_exit)
SYMBOL(x86_end_userspace_thread_exit):


null_idt_descr:
	.word	0
	.quad	0

FUNCTION(x86_reboot):
	lidt	null_idt_descr
	int		$0
done:
	jmp		done
FUNCTION_END(x86_reboot)


/*!	\fn void arch_debug_call_with_fault_handler(cpu_ent* cpu,
		jmp_buf jumpBuffer, void (*function)(void*), void* parameter)

	Called by debug_call_with_fault_handler() to do the dirty work of setting
	the fault handler and calling the function. If the function causes a page
	fault, the arch_debug_call_with_fault_handler() calls longjmp() with the
	given \a jumpBuffer. Otherwise it returns normally.

	debug_call_with_fault_handler() has already saved the CPU's fault_handler
	and fault_handler_stack_pointer and will reset them later, so
	arch_debug_call_with_fault_handler() doesn't need to care about it.

	\param cpu The \c cpu_ent for the current CPU.
	\param jumpBuffer Buffer to be used for longjmp().
	\param function The function to be called.
	\param parameter The parameter to be passed to the function to be called.
*/
FUNCTION(arch_debug_call_with_fault_handler):
	push	%rbp
	movq	%rsp, %rbp

	// Preserve the jump buffer address for the fault return.
	push	%rsi
	// Align the stack before calling the function.
	subq	$8, %rsp

	// Set fault handler address, and fault handler stack pointer address. We
	// don't need to save the previous values, since that's done by the caller.
	movq	$.L_debug_call_fault_handler, CPU_ENT_fault_handler(%rdi)
	movq	%rbp, CPU_ENT_fault_handler_stack_pointer(%rdi)

	// Call the function.
	movq	%rcx, %rdi
	call	*%rdx

	// Regular return.
	movq	%rbp, %rsp
	pop		%rbp
	ret

.L_debug_call_fault_handler:
	// Fault -- return via longjmp(jumpBuffer, 1)
	movq	%rbp, %rsp
	movq	-8(%rsp), %rdi
	movq	$1, %rsi
	call	longjmp
FUNCTION_END(arch_debug_call_with_fault_handler)


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

/* status_t _arch_cpu_user_memcpy(void* to, const void* from, size_t size,
	void (**faultHandler)(void)) */
FUNCTION(_arch_cpu_user_memcpy):
	movq	(%rcx), %r8
	leaq	.L_user_memcpy_error(%rip), %rax
	movq	%rax, (%rcx)
	movq	%rcx, %r9

	ASM_STAC
	movl	gX86UserMemcpyRepFrom(%rip), %eax
	cmpq	%rax, %rdx
	jb		.L_user_memcpy_gpr
	/* Avoid Zen 3 4K-aliasing penalty on rep movsb when
	 * ((dst - src) & 0xfff) is in [1..31] or [4080..4095]. */
	movq	%rdi, %rax
	subq	%rsi, %rax
	andl	$0xfff, %eax
	jz		.L_user_memcpy_rep
	cmpl	$32, %eax
	jb		.L_user_memcpy_gpr
	cmpl	$4080, %eax
	jae		.L_user_memcpy_gpr
	jmp		.L_user_memcpy_rep

.L_user_memcpy_gpr:
	cmpq	$32, %rdx
	jb		.L_user_memcpy_qword_setup
.L_user_memcpy_gpr32:
	movq	0(%rsi), %rax
	movq	8(%rsi), %rcx
	movq	16(%rsi), %r10
	movq	24(%rsi), %r11
	movq	%rax, 0(%rdi)
	movq	%rcx, 8(%rdi)
	movq	%r10, 16(%rdi)
	movq	%r11, 24(%rdi)
	addq	$32, %rsi
	addq	$32, %rdi
	subq	$32, %rdx
	cmpq	$32, %rdx
	jae		.L_user_memcpy_gpr32
.L_user_memcpy_qword_setup:
	movq	%rdx, %rcx
	shrq	$3, %rcx
	jz		.L_user_memcpy_tail
.L_user_memcpy_qword:
	movq	(%rsi), %rax
	movq	%rax, (%rdi)
	addq	$8, %rsi
	addq	$8, %rdi
	decq	%rcx
	jnz		.L_user_memcpy_qword
.L_user_memcpy_tail:
	andl	$7, %edx
	jz		.L_user_memcpy_done
.L_user_memcpy_byte:
	movb	(%rsi), %al
	movb	%al, (%rdi)
	incq	%rsi
	incq	%rdi
	decl	%edx
	jnz		.L_user_memcpy_byte
	jmp		.L_user_memcpy_done
.L_user_memcpy_rep:
	cld
	movq	%rdx, %rcx
	rep	movsb
.L_user_memcpy_done:
	ASM_CLAC

	movq	%r8, (%r9)
	xorl	%eax, %eax
	ret

.L_user_memcpy_error:
	ASM_CLAC
	movq	%r8, (%r9)
	movq	$-1, %rax
	ret
FUNCTION_END(_arch_cpu_user_memcpy)


/* status_t _arch_cpu_user_memset(void* to, char c, size_t count,
	void (**faultHandler)(void)) */
FUNCTION(_arch_cpu_user_memset):
	movq	(%rcx), %r8
	leaq	.L_user_memset_error(%rip), %rax
	movq	%rax, (%rcx)
	movq	%rcx, %r9

	ASM_STAC
	movzbq	%sil, %rax
	cmpq	$64, %rdx
	jae		.L_user_memset_rep
	movq	%rdx, %rcx
	shrq	$3, %rcx
	jz		.L_user_memset_tail
	movabsq	$0x0101010101010101, %r10
	imulq	%r10, %rax
.L_user_memset_qword:
	movq	%rax, (%rdi)
	addq	$8, %rdi
	decq	%rcx
	jnz		.L_user_memset_qword
.L_user_memset_tail:
	andl	$7, %edx
	jz		.L_user_memset_done
.L_user_memset_byte:
	movb	%al, (%rdi)
	incq	%rdi
	decl	%edx
	jnz		.L_user_memset_byte
	jmp		.L_user_memset_done
.L_user_memset_rep:
	cld
	movq	%rdx, %rcx
	rep	stosb
.L_user_memset_done:
	ASM_CLAC

	movq	%r8, (%r9)
	xorl	%eax, %eax
	ret

.L_user_memset_error:
	ASM_CLAC
	movq	%r8, (%r9)
	movq	$-1, %rax
	ret
FUNCTION_END(_arch_cpu_user_memset)


/* ssize_t _arch_cpu_user_strlcpy(char* to, const char* from, size_t size,
	void (**faultHandler)(void)) */
FUNCTION(_arch_cpu_user_strlcpy):
	movq	%rsi, %r10
	movq	(%rcx), %r8
	leaq	.L_user_strlcpy_error(%rip), %rax
	movq	%rax, (%rcx)
	movq	%rcx, %r9

	ASM_STAC
	cld

	testq	%rdx, %rdx
	jz		.L_user_strlcpy_count
	decq	%rdx
	jz		.L_user_strlcpy_terminate

	/* SWAR 8-byte copy when >= 8 bytes remain and src does not cross a 4 KiB
	 * page boundary (so an 8-byte load never faults on an unmapped next page). */
	movabsq	$0x0101010101010101, %r11
.L_user_strlcpy_word:
	cmpq	$8, %rdx
	jb		.L_user_strlcpy_loop
	movl	%esi, %eax
	andl	$0xfff, %eax
	cmpl	$4088, %eax
	ja		.L_user_strlcpy_loop
	movq	(%rsi), %rax
	movq	%rax, %rcx
	subq	%r11, %rcx
	notq	%rax
	andq	%rax, %rcx
	notq	%rax
	shlq	$7, %r11
	testq	%r11, %rcx
	jnz		.L_user_strlcpy_loop
	shrq	$7, %r11
	movq	%rax, (%rdi)
	addq	$8, %rsi
	addq	$8, %rdi
	subq	$8, %rdx
	jnz		.L_user_strlcpy_word
	jmp		.L_user_strlcpy_terminate

.L_user_strlcpy_loop:
	lodsb
	stosb
	testb	%al, %al
	jz		.L_user_strlcpy_done
	decq	%rdx
	jnz		.L_user_strlcpy_loop

.L_user_strlcpy_terminate:
	movb	$0, (%rdi)

.L_user_strlcpy_count:
	movq	%rsi, %rdi
	xorl	%eax, %eax
	movq	$-1, %rcx
	repnz	scasb
	movq	%rdi, %rsi

.L_user_strlcpy_done:
	ASM_CLAC
	movq	%r8, (%r9)
	movq	%rsi, %rax
	subq	%r10, %rax
	decq	%rax
	ret

.L_user_strlcpy_error:
	ASM_CLAC
	movq	%r8, (%r9)
	movq	$-2147478783, %rax	/* B_BAD_ADDRESS */
	ret
FUNCTION_END(_arch_cpu_user_strlcpy)

// SPDX-SnippetEnd
