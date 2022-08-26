/* on mips clang doesn't support privilegied instructions, doubleword store/load
   and crashes with hand-written assembly
 */

	.set mips3
	sync
	ld $t2, 0($t1)

a:
	addiu $t1, $s0, (b - a)
b:	nop
