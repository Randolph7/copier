/* memcpy with AVX
   Copyright (C) 2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <linux/linkage.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/export.h>
#include <asm/trapnr.h>

	.section .text.avx,"ax",@progbits

SYM_FUNC_START (__memcpy_avx_unaligned)
	mov	%rdi, %rax
	add	%rdx, %rax
	cmp	$256, %rdx
	jae	.L_256bytesormore
	cmp	$16, %dl
	jb	.L_less_16bytes
	cmp	$128, %dl
	jb	.L_less_128bytes
	vmovdqu (%rsi), %xmm0
	lea	(%rsi, %rdx), %rcx
	vmovdqu 0x10(%rsi), %xmm1
	vmovdqu 0x20(%rsi), %xmm2
	vmovdqu 0x30(%rsi), %xmm3
	vmovdqu 0x40(%rsi), %xmm4
	vmovdqu 0x50(%rsi), %xmm5
	vmovdqu 0x60(%rsi), %xmm6
	vmovdqu 0x70(%rsi), %xmm7
	vmovdqu -0x80(%rcx), %xmm8
	vmovdqu -0x70(%rcx), %xmm9
	vmovdqu -0x60(%rcx), %xmm10
	vmovdqu -0x50(%rcx), %xmm11
	vmovdqu -0x40(%rcx), %xmm12
	vmovdqu -0x30(%rcx), %xmm13
	vmovdqu -0x20(%rcx), %xmm14
	vmovdqu -0x10(%rcx), %xmm15
	lea	(%rdi, %rdx), %rdx
	vmovdqu %xmm0, (%rdi)
	vmovdqu %xmm1, 0x10(%rdi)
	vmovdqu %xmm2, 0x20(%rdi)
	vmovdqu %xmm3, 0x30(%rdi)
	vmovdqu %xmm4, 0x40(%rdi)
	vmovdqu %xmm5, 0x50(%rdi)
	vmovdqu %xmm6, 0x60(%rdi)
	vmovdqu %xmm7, 0x70(%rdi)
	vmovdqu %xmm8, -0x80(%rdx)
	vmovdqu %xmm9, -0x70(%rdx)
	vmovdqu %xmm10, -0x60(%rdx)
	vmovdqu %xmm11, -0x50(%rdx)
	vmovdqu %xmm12, -0x40(%rdx)
	vmovdqu %xmm13, -0x30(%rdx)
	vmovdqu %xmm14, -0x20(%rdx)
	vmovdqu %xmm15, -0x10(%rdx)
	ret
	.p2align 4
.L_less_128bytes:
	cmp	$64, %dl
	jb	.L_less_64bytes
	vmovdqu (%rsi), %xmm0
	lea	(%rsi, %rdx), %rcx
	vmovdqu 0x10(%rsi), %xmm1
	vmovdqu 0x20(%rsi), %xmm2
	lea	(%rdi, %rdx), %rdx
	vmovdqu 0x30(%rsi), %xmm3
	vmovdqu -0x40(%rcx), %xmm4
	vmovdqu -0x30(%rcx), %xmm5
	vmovdqu -0x20(%rcx), %xmm6
	vmovdqu -0x10(%rcx), %xmm7
	vmovdqu %xmm0, (%rdi)
	vmovdqu %xmm1, 0x10(%rdi)
	vmovdqu %xmm2, 0x20(%rdi)
	vmovdqu %xmm3, 0x30(%rdi)
	vmovdqu %xmm4, -0x40(%rdx)
	vmovdqu %xmm5, -0x30(%rdx)
	vmovdqu %xmm6, -0x20(%rdx)
	vmovdqu %xmm7, -0x10(%rdx)
	ret

	.p2align 4
.L_less_64bytes:
	cmp	$32, %dl
	jb	.L_less_32bytes
	vmovdqu (%rsi), %xmm0
	vmovdqu 0x10(%rsi), %xmm1
	vmovdqu -0x20(%rsi, %rdx), %xmm6
	vmovdqu -0x10(%rsi, %rdx), %xmm7
	vmovdqu %xmm0, (%rdi)
	vmovdqu %xmm1, 0x10(%rdi)
	vmovdqu %xmm6, -0x20(%rdi, %rdx)
	vmovdqu %xmm7, -0x10(%rdi, %rdx)
	ret

	.p2align 4
.L_less_32bytes:
	vmovdqu (%rsi), %xmm0
	vmovdqu -0x10(%rsi, %rdx), %xmm7
	vmovdqu %xmm0, (%rdi)
	vmovdqu %xmm7, -0x10(%rdi, %rdx)
	ret

	.p2align 4
.L_less_16bytes:
	cmp	$8, %dl
	jb	.L_less_8bytes
	movq -0x08(%rsi, %rdx),	%rcx
	movq (%rsi),	%rsi
	movq %rsi, (%rdi)
	movq %rcx, -0x08(%rdi, %rdx)
	ret

	.p2align 4
.L_less_8bytes:
	cmp	$4, %dl
	jb	.L_less_4bytes
	mov -0x04(%rsi, %rdx), %ecx
	mov (%rsi),	%esi
	mov %esi, (%rdi)
	mov %ecx, -0x04(%rdi, %rdx)
	ret

.L_less_4bytes:
	cmp	$1, %dl
	jbe	.L_less_2bytes
	mov -0x02(%rsi, %rdx),	%cx
	mov (%rsi),	%si
	mov %si, (%rdi)
	mov %cx, -0x02(%rdi, %rdx)
	ret

.L_less_2bytes:
	jb	.L_less_0bytes
	mov	(%rsi), %cl
	mov	%cl,	(%rdi)
.L_less_0bytes:
	ret

	.p2align 4
.L_256bytesormore:
	cmp	$2048, %rdx
	jae	.L_gobble_data_movsb
	mov	%rax, %r8
	lea	(%rsi, %rdx), %rcx
	mov	%rdi, %r10
	vmovdqu -0x80(%rcx), %xmm5
	vmovdqu -0x70(%rcx), %xmm6
	mov	$0x80, %rax
	and	$-32, %rdi
	add	$32, %rdi
	vmovdqu -0x60(%rcx), %xmm7
	vmovdqu -0x50(%rcx), %xmm8
	mov	%rdi, %r11
	sub	%r10, %r11
	vmovdqu -0x40(%rcx), %xmm9
	vmovdqu -0x30(%rcx), %xmm10
	sub	%r11, %rdx
	vmovdqu -0x20(%rcx), %xmm11
	vmovdqu -0x10(%rcx), %xmm12
	vmovdqu	(%rsi), %ymm4
	add	%r11, %rsi
	sub	%eax, %edx
.L_goble_128_loop:
	vmovdqu (%rsi), %ymm0
	vmovdqu 0x20(%rsi), %ymm1
	vmovdqu 0x40(%rsi), %ymm2
	vmovdqu 0x60(%rsi), %ymm3
	add	%rax, %rsi
	vmovdqa %ymm0, (%rdi)
	vmovdqa %ymm1, 0x20(%rdi)
	vmovdqa %ymm2, 0x40(%rdi)
	vmovdqa %ymm3, 0x60(%rdi)
	add	%rax, %rdi
	sub	%eax, %edx
	jae	.L_goble_128_loop
	add	%eax, %edx
	add	%rdi, %rdx
	vmovdqu	%ymm4, (%r10)
	vzeroupper
	vmovdqu %xmm5, -0x80(%rdx)
	vmovdqu %xmm6, -0x70(%rdx)
	vmovdqu %xmm7, -0x60(%rdx)
	vmovdqu %xmm8, -0x50(%rdx)
	vmovdqu %xmm9, -0x40(%rdx)
	vmovdqu %xmm10, -0x30(%rdx)
	vmovdqu %xmm11, -0x20(%rdx)
	vmovdqu %xmm12, -0x10(%rdx)
	mov	%r8, %rax
	ret

	.p2align 4
.L_gobble_data_movsb:
	lea	(%rsi, %rdx), %rcx
	vmovdqu	(%rsi), %ymm4
	vmovdqu -0x80(%rsi,%rdx), %xmm5
	vmovdqu -0x70(%rcx), %xmm6
	vmovdqu -0x60(%rcx), %xmm7
	vmovdqu -0x50(%rcx), %xmm8
	vmovdqu -0x40(%rcx), %xmm9
	vmovdqu -0x30(%rcx), %xmm10
	vmovdqu -0x20(%rcx), %xmm11
	vmovdqu -0x10(%rcx), %xmm12
	mov	%rdi, %r8
	and	$-32, %rdi
	add	$32, %rdi
	mov	%rdi, %r10
	sub	%r8, %r10
	sub	%r10, %rdx
	add	%r10, %rsi
	lea	(%rdi, %rdx), %rcx
	add	$-0x80, %rdx
.L_gobble_mem_fwd_loop:
	prefetchnta 0x1c0(%rsi)
	prefetchnta 0x280(%rsi)
	vmovdqu	(%rsi), %ymm0
	vmovdqu	0x20(%rsi), %ymm1
	vmovdqu	0x40(%rsi), %ymm2
	vmovdqu	0x60(%rsi), %ymm3
	sub	$-0x80, %rsi
	vmovntdq	%ymm0, (%rdi)
	vmovntdq	%ymm1, 0x20(%rdi)
	vmovntdq	%ymm2, 0x40(%rdi)
	vmovntdq	%ymm3, 0x60(%rdi)
	sub	$-0x80, %rdi
	add	$-0x80, %rdx
	jb	.L_gobble_mem_fwd_loop
	sfence
	vmovdqu	%ymm4, (%r8)
	vzeroupper
	vmovdqu %xmm5, -0x80(%rcx)
	vmovdqu %xmm6, -0x70(%rcx)
	vmovdqu %xmm7, -0x60(%rcx)
	vmovdqu %xmm8, -0x50(%rcx)
	vmovdqu %xmm9, -0x40(%rcx)
	vmovdqu %xmm10, -0x30(%rcx)
	vmovdqu %xmm11, -0x20(%rcx)
	vmovdqu %xmm12, -0x10(%rcx)
	ret

SYM_FUNC_END (__memcpy_avx_unaligned)
