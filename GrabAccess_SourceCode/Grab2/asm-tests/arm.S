/* on arm clang doesn't support .arch directive */

	.text
	.syntax	unified

#if !defined (__thumb2__)
	.arch	armv7a
	.arm
#else
	.arch	armv7
	.thumb
#endif
	mcr	p15, 0, r11, c7, c14, 2

	/* clang restricts access to dsb/isb despite .arch  */
	dsb
	isb


	
