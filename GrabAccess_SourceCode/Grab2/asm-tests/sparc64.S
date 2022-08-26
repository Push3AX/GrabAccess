        .text
1:	
	/* A small list of examples of what clang doesn't support.  */
	clr	%o0
	lduw	[%o4 + 4], %o4
	and     %o6, ~0xff, %o6
	stw	%o5, [%o3]
	bne,pt	%icc, 1b
	 nop
