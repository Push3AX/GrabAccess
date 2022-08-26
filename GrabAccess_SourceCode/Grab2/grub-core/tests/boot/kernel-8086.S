
	.text
	.globl _start
_start:
base:	
	.code16
	jmp cont

serialmsg:	
1:	
	movb 0(%si), %bl
	testb %bl, %bl
	jz 1f
	movw $0x3fd, %dx
2:	
	inb %dx, %al
	testb $0x20, %al
	jz   2b

	movw $0x3f8, %dx
	movb %bl, %al
	outb %al, %dx
	incw %si
	jmp 1b
1:
	ret

cont:
#ifdef TARGET_NTLDR
	movw $0x2000, %ax
#elif defined (TARGET_CHAINLOADER)
	xorw %ax, %ax
#else
#error unsupported target
#endif
	movw %ax, %ds
	lea message, %si
	call serialmsg
#include "qemu-shutdown-x86.S"

1:
	hlt
	jmp 1b

message:
	.ascii "Boot Test Passed Successfully\n" SUCCESSFUL_BOOT_STRING "\n"
	.byte 0

	.org 510
	.short 0xaa55
