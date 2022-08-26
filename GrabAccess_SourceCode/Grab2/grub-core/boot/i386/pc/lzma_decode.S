/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#define FIXED_PROPS

#define LZMA_BASE_SIZE 1846
#define LZMA_LIT_SIZE 768

#define LZMA_PROPERTIES_SIZE 5

#define kNumTopBits 24
#define kTopValue (1 << kNumTopBits)

#define kNumBitModelTotalBits 11
#define kBitModelTotal (1 << kNumBitModelTotalBits)
#define kNumMoveBits 5


#define kNumPosBitsMax 4
#define kNumPosStatesMax (1 << kNumPosBitsMax)

#define kLenNumLowBits 3
#define kLenNumLowSymbols (1 << kLenNumLowBits)
#define kLenNumMidBits 3
#define kLenNumMidSymbols (1 << kLenNumMidBits)
#define kLenNumHighBits 8
#define kLenNumHighSymbols (1 << kLenNumHighBits)

#define LenChoice 0
#define LenChoice2 (LenChoice + 1)
#define LenLow (LenChoice2 + 1)
#define LenMid (LenLow + (kNumPosStatesMax << kLenNumLowBits))
#define LenHigh (LenMid + (kNumPosStatesMax << kLenNumMidBits))
#define kNumLenProbs (LenHigh + kLenNumHighSymbols)


#define kNumStates 12
#define kNumLitStates 7

#define kStartPosModelIndex 4
#define kEndPosModelIndex 14
#define kNumFullDistances (1 << (kEndPosModelIndex >> 1))

#define kNumPosSlotBits 6
#define kNumLenToPosStates 4

#define kNumAlignBits 4
#define kAlignTableSize (1 << kNumAlignBits)

#define kMatchMinLen 2

#define IsMatch 0
#define IsRep (IsMatch + (kNumStates << kNumPosBitsMax))
#define IsRepG0 (IsRep + kNumStates)
#define IsRepG1 (IsRepG0 + kNumStates)
#define IsRepG2 (IsRepG1 + kNumStates)
#define IsRep0Long (IsRepG2 + kNumStates)
#define PosSlot (IsRep0Long + (kNumStates << kNumPosBitsMax))
#define SpecPos (PosSlot + (kNumLenToPosStates << kNumPosSlotBits))
#define Align (SpecPos + kNumFullDistances - kEndPosModelIndex)
#define LenCoder (Align + kAlignTableSize)
#define RepLenCoder (LenCoder + kNumLenProbs)
#define Literal (RepLenCoder + kNumLenProbs)

#define out_size	8(%ebp)

#define now_pos		-4(%ebp)
#define prev_byte	-8(%ebp)
#define range		-12(%ebp)
#define code		-16(%ebp)
#define state		-20(%ebp)
#define rep0		-24(%ebp)
#define rep1		-28(%ebp)
#define rep2		-32(%ebp)
#define rep3		-36(%ebp)

#ifdef FIXED_PROPS

#define FIXED_LC	3
#define FIXED_LP	0
#define FIXED_PB	2

#define POS_STATE_MASK	((1 << (FIXED_PB)) - 1)
#define LIT_POS_MASK	((1 << (FIXED_LP)) - 1)

#define LOCAL_SIZE	36

#else

#define lc		(%ebx)
#define lp		4(%ebx)
#define pb		8(%ebx)
#define probs		12(%ebx)

#define pos_state_mask	-40(%ebp)
#define lit_pos_mask	-44(%ebp)

#define LOCAL_SIZE	44

#endif

RangeDecoderBitDecode:
#ifdef FIXED_PROPS
	leal	(%ebx, %eax, 4), %eax
#else
	shll	$2, %eax
	addl	probs, %eax
#endif

	movl	%eax, %ecx
	movl	(%ecx), %eax

	movl	range, %edx
	shrl	$kNumBitModelTotalBits, %edx
	mull	%edx

	cmpl	code, %eax
	jbe	1f

	movl	%eax, range
	movl	$kBitModelTotal, %edx
	subl	(%ecx), %edx
	shrl	$kNumMoveBits, %edx
	addl	%edx, (%ecx)
	clc
3:
	pushf
	cmpl	$kTopValue, range
	jnc	2f
	shll	$8, code
	lodsb
	movb	%al, code
	shll	$8, range
2:
	popf
	ret
1:
	subl	%eax, range
	subl	%eax, code
	movl	(%ecx), %edx
	shrl	$kNumMoveBits, %edx
	subl	%edx, (%ecx)
	stc
	jmp	3b

RangeDecoderBitTreeDecode:
RangeDecoderReverseBitTreeDecode:
	movzbl	%cl, %ecx
	xorl	%edx, %edx
	pushl	%edx
	incl	%edx
	pushl	%edx

1:
	pushl	%eax
	pushl	%ecx
	pushl	%edx

	addl	%edx, %eax
	call	RangeDecoderBitDecode

	popl	%edx
	popl	%ecx

	jnc	2f
	movl	4(%esp), %eax
	orl	%eax, 8(%esp)
	stc

2:
	adcl	%edx, %edx
	popl	%eax

	shll	$1, (%esp)
	loop	1b

	popl	%ecx
	subl	%ecx, %edx		/* RangeDecoderBitTreeDecode */
	popl	%ecx			/* RangeDecoderReverseBitTreeDecode */
	ret

LzmaLenDecode:
	pushl	%eax
	addl	$LenChoice, %eax
	call	RangeDecoderBitDecode
	popl	%eax
	jc	1f
	pushl	$0
	movb	$kLenNumLowBits, %cl
	addl	$LenLow, %eax
2:
	movl	12(%esp), %edx
	shll	%cl, %edx
	addl	%edx, %eax
3:

	call	RangeDecoderBitTreeDecode
	popl	%eax
	addl	%eax, %edx
	ret

1:
	pushl	%eax
	addl	$LenChoice2, %eax
	call	RangeDecoderBitDecode
	popl	%eax
	jc	1f
	pushl	$kLenNumLowSymbols
	movb	$kLenNumMidBits, %cl
	addl	$LenMid, %eax
	jmp	2b

1:
	pushl	$(kLenNumLowSymbols + kLenNumMidSymbols)
	addl	$LenHigh, %eax
	movb	$kLenNumHighBits, %cl
	jmp	3b

WriteByte:
	movb	%al, prev_byte
	stosb
	incl	now_pos
	ret

/*
 * int LzmaDecode(CLzmaDecoderState *vs,
 *                const unsigned char *inStream,
 *                unsigned char *outStream,
 *                SizeT outSize);
 */

_LzmaDecodeA:

	pushl	%ebp
	movl	%esp, %ebp
	subl	$LOCAL_SIZE, %esp

#ifndef ASM_FILE
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	%eax, %ebx
	movl	%edx, %esi
	pushl	%ecx
#else
	pushl	%edi
#endif

	cld

#ifdef FIXED_PROPS
	movl	%ebx, %edi
	movl	$(Literal + (LZMA_LIT_SIZE << (FIXED_LC + FIXED_LP))), %ecx
#else
	movl	$LZMA_LIT_SIZE, %eax
	movb	lc, %cl
	addb	lp, %cl
	shll	%cl, %eax
	addl	$Literal, %eax
	movl	%eax, %ecx
	movl	probs, %edi
#endif

	movl	$(kBitModelTotal >> 1), %eax

	rep
	stosl

	popl	%edi

	xorl	%eax, %eax
	movl	%eax, now_pos
	movl	%eax, prev_byte
	movl	%eax, state

	incl	%eax
	movl	%eax, rep0
	movl	%eax, rep1
	movl	%eax, rep2
	movl	%eax, rep3

#ifndef FIXED_PROPS
	movl	%eax, %edx
	movb	pb, %cl
	shll	%cl, %edx
	decl	%edx
	movl	%edx, pos_state_mask

	movl	%eax, %edx
	movb	lp, %cl
	shll	%cl, %edx
	decl	%edx
	movl	%edx, lit_pos_mask;
#endif

	/* RangeDecoderInit */
	negl	%eax
	movl	%eax, range

	incl	%eax
	movb	$5, %cl

1:
	shll	$8, %eax
	lodsb
	loop	1b

	movl	%eax, code

lzma_decode_loop:
	movl	now_pos, %eax
	cmpl	out_size, %eax

	jb	1f

#ifndef ASM_FILE
	xorl	%eax, %eax

	popl	%ebx
	popl	%edi
	popl	%esi
#endif

	movl	%ebp, %esp
	popl	%ebp
	ret

1:
#ifdef FIXED_PROPS
	andl	$POS_STATE_MASK, %eax
#else
	andl	pos_state_mask, %eax
#endif
	pushl	%eax				/* posState */
	movl	state, %edx
	shll	$kNumPosBitsMax, %edx
	addl	%edx, %eax
	pushl	%eax				/* (state << kNumPosBitsMax) + posState */

	call	RangeDecoderBitDecode
	jc	1f

	movl	now_pos, %eax

#ifdef FIXED_PROPS
	andl	$LIT_POS_MASK, %eax
	shll	$FIXED_LC, %eax
	movl	prev_byte, %edx
	shrl	$(8 - FIXED_LC), %edx
#else
	andl	lit_pos_mask, %eax
	movb	lc, %cl
	shll	%cl, %eax
	negb	%cl
	addb	$8, %cl
	movl	prev_byte, %edx
	shrl	%cl, %edx
#endif

	addl	%edx, %eax
	movl	$LZMA_LIT_SIZE, %edx
	mull	%edx
	addl	$Literal, %eax
	pushl	%eax

	incl	%edx			/* edx = 1 */

	movl	rep0, %eax
	negl	%eax
	pushl	(%edi, %eax)		/* matchByte */

	cmpb	$kNumLitStates, state
	jb	5f

	/* LzmaLiteralDecodeMatch */

3:
	cmpl	$0x100, %edx
	jae	4f

	xorl	%eax, %eax
	shlb	$1, (%esp)
	adcl	%eax, %eax

	pushl	%eax
	pushl	%edx

	shll	$8, %eax
	leal	0x100(%edx, %eax), %eax
	addl	12(%esp), %eax
	call	RangeDecoderBitDecode

	setc	%al
	popl	%edx
	adcl	%edx, %edx

	popl	%ecx
	cmpb	%cl, %al
	jz	3b

5:

	/* LzmaLiteralDecode */

	cmpl	$0x100, %edx
	jae	4f

	pushl	%edx
	movl	%edx, %eax
	addl	8(%esp), %eax
	call	RangeDecoderBitDecode
	popl	%edx
	adcl	%edx, %edx
	jmp	5b

4:
	addl	$16, %esp

	movb	%dl, %al
	call	WriteByte

	movb	state, %al
	cmpb	$4, %al
	jae	2f
	xorb	%al, %al
	jmp	3f
2:
	subb	$3, %al
	cmpb	$7, %al
	jb	3f
	subb	$3, %al
3:
	movb	%al, state
	jmp	lzma_decode_loop

1:
	movl	state, %eax
	addl	$IsRep, %eax
	call	RangeDecoderBitDecode
	jnc	1f

	movl	state, %eax
	addl	$IsRepG0, %eax
	call	RangeDecoderBitDecode
	jc	10f

	movl	(%esp), %eax
	addl	$IsRep0Long, %eax
	call	RangeDecoderBitDecode
	jc	20f

	cmpb	$7, state
	movb	$9, state
	jb	100f
	addb	$2, state
100:

	movl	$1, %ecx

3:
	movl	rep0, %edx
	negl	%edx

4:
	movb	(%edi, %edx), %al
	call	WriteByte
	loop	4b

	popl	%eax
	popl	%eax
	jmp	lzma_decode_loop

10:
	movl	state, %eax
	addl	$IsRepG1, %eax
	call	RangeDecoderBitDecode
	movl	rep1, %edx
	jnc	100f

	movl	state, %eax
	addl	$IsRepG2, %eax
	call	RangeDecoderBitDecode
	movl	rep2, %edx
	jnc	1000f
	movl	rep2, %edx
	xchgl	rep3, %edx
1000:
	pushl	rep1
	popl	rep2
100:
	xchg	rep0, %edx
	movl	%edx, rep1
20:

	movl	$RepLenCoder, %eax
	call	LzmaLenDecode

	cmpb	$7, state
	movb	$8, state
	jb	100f
	addb	$3, state
100:
	jmp	2f

1:
	movl	rep0, %eax
	xchgl	rep1, %eax
	xchgl	rep2, %eax
	movl	%eax, rep3

	cmpb	$7, state
	movb	$7, state
	jb	10f
	addb	$3, state
10:

	movl	$LenCoder, %eax
	call	LzmaLenDecode
	pushl	%edx

	movl	$(kNumLenToPosStates - 1), %eax
	cmpl	%eax, %edx
	jbe	100f
	movl	%eax, %edx
100:
	movb	$kNumPosSlotBits, %cl
	shll	%cl, %edx
	leal	PosSlot(%edx), %eax
	call	RangeDecoderBitTreeDecode

	movl	%edx, rep0
	cmpl	$kStartPosModelIndex, %edx
	jb	100f

	movl	%edx, %ecx
	shrl	$1, %ecx
	decl	%ecx

	movzbl	%dl, %eax
	andb	$1, %al
	orb	$2, %al
	shll	%cl, %eax
	movl	%eax, rep0

	cmpl	$kEndPosModelIndex, %edx
	jae	200f
	movl	rep0, %eax
	addl	$(SpecPos - 1), %eax
	subl	%edx, %eax
	jmp	300f
200:

	subb	$kNumAlignBits, %cl

	/* RangeDecoderDecodeDirectBits */
	xorl	%edx, %edx

1000:
	shrl	$1, range
	shll	$1, %edx

	movl	range, %eax
	cmpl	%eax, code
	jb	2000f
	subl	%eax, code
	orb	$1, %dl
2000:

	cmpl	$kTopValue, %eax
	jae	3000f
	shll	$8, range
	shll	$8, code
	lodsb
	movb	%al, code

3000:
	loop	1000b

	movb	$kNumAlignBits, %cl
	shll	%cl, %edx
	addl	%edx, rep0

	movl	$Align, %eax

300:
	call	RangeDecoderReverseBitTreeDecode
	addl	%ecx, rep0

100:
	incl	rep0
	popl	%edx

2:

	addl	$kMatchMinLen, %edx
	movl	%edx, %ecx

	jmp	3b
