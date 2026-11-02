/* Trampoline: run the first instructions of the stock dial handler (0x6D45C) that the hook
 * overwrote, then continue in the original code at 0x6D468. */
	.text
	.global	_orig_tempo_dial
	.global	orig_tempo_dial
	.align	2
_orig_tempo_dial:
orig_tempo_dial:
	mov.l	r14,@-r15
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	mov	r4,r13
	mov.l	1f,r3		/* value of the literal at 0x6D514 */
	sts.l	pr,@-r15
	mov.l	2f,r0
	jmp	@r0
	nop
	.align	2
1:	.long	0x00064418
2:	.long	0x0006d468
