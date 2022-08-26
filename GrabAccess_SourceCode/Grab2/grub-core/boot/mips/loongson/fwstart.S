/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2004,2005,2007,2008,2009,2010  Free Software Foundation, Inc.
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

#include <grub/mips/loongson/serial.h>
#include <grub/mips/loongson/pci.h>
#include <grub/mips/loongson.h>
#include <grub/pci.h>
#include <grub/machine/serial.h>
#include <grub/machine/kernel.h>
#include <grub/ns8250.h>
#include <grub/cs5536.h>
#include <grub/smbus.h>

#ifndef FULOONG2F
#include <grub/vgaregs.h>
#define GRUB_SM712_REG_BASE 0x700000
#define GRUB_SM712_PCIID 0x0712126f
#endif

#ifdef FULOONG2F
#define GRUB_MACHINE_SERIAL_PORT GRUB_MACHINE_SERIAL_PORT2
#define GRUB_MACHINE_SERIAL_DIVISOR_115200 GRUB_MACHINE_SERIAL_PORT2_DIVISOR_115200
#else
#define GRUB_MACHINE_SERIAL_PORT GRUB_MACHINE_SERIAL_PORT0
#define GRUB_MACHINE_SERIAL_DIVISOR_115200 GRUB_MACHINE_SERIAL_PORT0_DIVISOR_115200
#endif

	.set noreorder
	.set noat
	.set nomacro
	.set mips3

	.global start,_start,__start
start:
_start:
__start:	
	/* Put serial init as soon as possible.  But on Fuloong2f serial is past
	   Geode, so on Fuloong2f we need Geode first.
	*/
#ifndef FULOONG2F
	bal serial_hw_init
	 nop
#endif

	/* Find CS5536 controller.  */
	/* $t4 chooses device in priority encoding.  */
	/* Resulting value is kept in GRUB_MACHINE_PCI_CONF_CTRL_REG.
	   This way we don't need to sacrifice a register for it.  */
retry_cs5536:	
	/* We have only one bus (0). Function is 0.  */
	lui $t0, %hi(GRUB_MACHINE_PCI_CONF_CTRL_REG_ADDR_2F)
	lui $t1, %hi(GRUB_MACHINE_PCI_CONFSPACE_2F)
	lui $t3, %hi(GRUB_CS5536_PCIID)
	addiu $t3, $t3, %lo(GRUB_CS5536_PCIID)
	ori $t4, $zero, 1
1:
	andi $t4, $t4, ((1 << GRUB_PCI_NUM_DEVICES_2F) - 1)
	/* In case of failure try again. CS5536 may be slow to come up.  */
	beql  $t4, $zero, retry_cs5536
	 nop
	sw   $t4, %lo(GRUB_MACHINE_PCI_CONF_CTRL_REG_ADDR_2F) ($t0)
	lw   $t2, (%lo(GRUB_MACHINE_PCI_CONFSPACE_2F) + GRUB_PCI_REG_PCI_ID) ($t1)
	bnel  $t2, $t3, 1b
	 sll $t4, $t4, 1

#ifndef FULOONG2F
	lui $a0, %hi(cs5536_found)
	bal message
	 addiu $a0, $a0, %lo(cs5536_found)
	bal printhex
	 move $a0, $t4
#endif

	lui $t0, %hi(GRUB_MACHINE_PCI_CONFSPACE_2F)
	li  $t1, GRUB_CS5536_MSR_MAILBOX_CONFIG_ENABLED
	sw  $t1, (%lo(GRUB_MACHINE_PCI_CONFSPACE_2F) + GRUB_CS5536_MSR_MAILBOX_CONFIG) ($t0)
	
	/* Set GPIO LBAR.  */
	lui $a0, %hi(GRUB_CS5536_MSR_GPIO_BAR)
	addiu $a0, $a0, %lo(GRUB_CS5536_MSR_GPIO_BAR)
	ori $a1, $zero, GRUB_CS5536_LBAR_GPIO
	/* Set mask to 0xf and enabled bit to 1.  */
	bal wrmsr
	 ori $a2, $zero, ((GRUB_CS5536_LBAR_MASK_MASK \
	                  | GRUB_CS5536_LBAR_ENABLE) >> 32)

	bal gpio_init
	 nop

#ifdef FULOONG2F
	bal serial_hw_init
	 nop
#endif

	/* Initialise SMBus controller.  */
	/* Set SMBUS LBAR.  */
	lui $a0, %hi(GRUB_CS5536_MSR_SMB_BAR)
	addiu $a0, $a0, %lo(GRUB_CS5536_MSR_SMB_BAR)
	ori $a1, $zero, GRUB_CS5536_LBAR_SMBUS
	/* Set mask to 0xf and enabled bit to 1.  */
	bal wrmsr
	 ori $a2, $zero, ((GRUB_CS5536_LBAR_MASK_MASK \
	                   | GRUB_CS5536_LBAR_ENABLE) >> 32)

	lui $a0, %hi(smbus_enabled)
	bal message
	  addiu $a0, $a0, %lo(smbus_enabled)

	lui $t0, %hi(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS)

	/* Disable SMB.  */
	sb $zero, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL2) ($t0)

	/* Disable interrupts.  */
	sb $zero, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1) ($t0)

	/* Set as master.  */
	sb $zero, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_ADDR) ($t0)

	/* Launch SMBus controller at slowest speed possible.  */
	ori $t1, $zero, 0xff
	sb $t1, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL3) ($t0)
	sb $t1, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL2) ($t0)

	/* Yeeloong and Fuloong2f have only one memory slot.  */
	/* Output first byte on serial for debugging.  */
	ori $a1, $zero, GRUB_SMB_RAM_START_ADDR
	bal read_spd
	 move $a0, $zero
	bal printhex
	 move $a0, $v0

	bal read_spd
	 ori $a0, $zero, GRUB_SMBUS_SPD_MEMORY_TYPE_ADDR
	ori $t0, $zero, GRUB_SMBUS_SPD_MEMORY_TYPE_DDR2
	lui $a0, %hi(unimplemented_memory_type)
	bne $t0, $v0, fatal
	 addiu $a0, $a0, %lo(unimplemented_memory_type)

	/* And here is our goal: DDR2 controller initialisation.  */
        lui	$t0, %hi(GRUB_CPU_LOONGSON_CORECFG)
        ld	$t1, %lo(GRUB_CPU_LOONGSON_CORECFG) ($t0)
	/* Use addiu for sign-extension.  */
	addiu	$t2, $zero, ~(GRUB_CPU_LOONGSON_CORECFG_DISABLE_DDR2_SPACE|GRUB_CPU_LOONGSON_CORECFG_BUFFER_CPU)
	and	$t1, $t1, $t2
	sd	$t1, %lo (GRUB_CPU_LOONGSON_CORECFG) ($t0)

	b continue

	.org GRUB_CPU_LOONGSON_FLASH_TLB_REFILL - GRUB_CPU_LOONGSON_FLASH_START
tlb_refill:	
	mfc0 $s1, GRUB_CPU_LOONGSON_COP0_EPC
	mfc0 $s2, GRUB_CPU_LOONGSON_COP0_BADVADDR
	move $s3, $ra
	lui $a0, %hi(epc)
	bal message
	 addiu $a0, $a0, %lo(epc)

	bal printhex
	 move $a0, $s1

	lui $a0, %hi(badvaddr)
	bal message
	 addiu $a0, $a0, %lo(badvaddr)

	bal printhex
	 move $a0, $s2

	lui $a0, %hi(return_msg)
	bal message
	 addiu $a0, $a0, %lo(return_msg)

	bal printhex
	 move $a0, $s3
	
	lui $a0, %hi(newline)
	bal message
	 addiu $a0, $a0, %lo(newline)

	lui $a0, %hi(unhandled_tlb_refill)
	b fatal
	 addiu $a0, $a0, %lo(unhandled_tlb_refill)

	.org GRUB_CPU_LOONGSON_FLASH_CACHE_ERROR - GRUB_CPU_LOONGSON_FLASH_START
cache_error:
	lui $a0, %hi(unhandled_cache_error)
	b fatal
	 addiu $a0, $a0, %lo(unhandled_cache_error)

	.org GRUB_CPU_LOONGSON_FLASH_OTHER_EXCEPTION - GRUB_CPU_LOONGSON_FLASH_START
other_exception:
	mfc0 $s0, GRUB_CPU_LOONGSON_COP0_CAUSE
	mfc0 $s1, GRUB_CPU_LOONGSON_COP0_EPC
	mfc0 $s2, GRUB_CPU_LOONGSON_COP0_BADVADDR
	lui $a0, %hi(cause)
	bal message
	 addiu $a0, $a0, %lo(cause)

	bal printhex
	 move $a0, $s0

	lui $a0, %hi(epc)
	bal message
	 addiu $a0, $a0, %lo(epc)

	bal printhex
	 move $a0, $s1

	lui $a0, %hi(badvaddr)
	bal message
	 addiu $a0, $a0, %lo(badvaddr)

	bal printhex
	 move $a0, $s2
	
	lui $a0, %hi(newline)
	bal message
	 addiu $a0, $a0, %lo(newline)
	
	lui $a0, %hi(unhandled_exception)
	b fatal
	 addiu $a0, $a0, %lo(unhandled_exception)

gpio_init:
	lui $t0, %hi(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_GPIO)
	addiu  $t0, $t0, %lo(GRUB_MACHINE_PCI_IO_BASE_2F + GRUB_CS5536_LBAR_GPIO)
	lui $t1, %hi (gpio_dump)
	addiu $t1, $t1, %lo (gpio_dump)

1:	
	lw $t2, 0($t1)
	sw $t2, 0($t0)
	addiu $t0, $t0, 4
	addiu $t1, $t1, 4
	lui $t2, %hi (gpio_dump_end)
	addiu $t2, $t2, %lo (gpio_dump_end)
	bne $t1, $t2, 1b
	 nop
	jr $ra
	 nop

	/* Same as similarly named C function but in asm since
	   we need it early.  */
	/* In: none. Out: none. Clobbered: $t0, $t1, $t2, $a0, $a1, $a2.  */
serial_hw_init:
	move $t2, $ra
#ifdef FULOONG2F
	lui $a0, %hi(GRUB_CS5536_MSR_DIVIL_LEG_IO)
	addiu $a0, $a0, %lo(GRUB_CS5536_MSR_DIVIL_LEG_IO)
	lui $a1, %hi (GRUB_CS5536_MSR_DIVIL_LEG_IO_UART2_COM3    \
		     | GRUB_CS5536_MSR_DIVIL_LEG_IO_F_REMAP	    \
		     | GRUB_CS5536_MSR_DIVIL_LEG_IO_MODE_X86    \
	             | GRUB_CS5536_MSR_DIVIL_LEG_IO_UART1_COM1)
	ori $a1, $a1, (GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE0 \
		       | GRUB_CS5536_MSR_DIVIL_LEG_IO_RTC_ENABLE1)
	bal wrmsr
	 move $a2, $zero

	lui $a0, %hi(GRUB_CS5536_MSR_DIVIL_UART1_CONF)
	addiu $a0, $a0, %lo(GRUB_CS5536_MSR_DIVIL_UART1_CONF)
	li $a1, 2
	bal wrmsr
	 move $a2, $zero

	lui $a0, %hi(GRUB_CS5536_MSR_DIVIL_UART2_CONF)
	addiu $a0, $a0, %lo(GRUB_CS5536_MSR_DIVIL_UART2_CONF)
	li $a1, 2
	bal wrmsr
	 move $a2, $zero
#endif
	
	lui $t0, %hi (GRUB_MACHINE_SERIAL_PORT)

	/* Turn off the interrupt.  */
	sb $zero, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_IER)($t0)

	/* Set DLAB.  */
	ori $t1, $zero, UART_DLAB
	sb  $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_LCR)($t0)

	/* Set the baud rate 115200.  */
	ori $t1, $zero, GRUB_MACHINE_SERIAL_DIVISOR_115200
	sb $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_DLL)($t0) 
	sb $zero, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_DLH)($t0) 

	/* Set the line status.  */
	ori $t1, $zero, (UART_NO_PARITY | UART_8BITS_WORD | UART_1_STOP_BIT)
        sb  $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_LCR)($t0)

	/* Enable the FIFO.  */
	ori $t1, $zero, UART_ENABLE_FIFO_TRIGGER1
	sb $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_FCR)($t0)

	/* Turn on DTR and RTS.  */
	ori $t1, $zero, UART_ENABLE_DTRRTS
	sb  $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_MCR)($t0)

	/* Let message return to original caller.  */
	lui  $a0, %hi(notification_string)
	addiu $a0, $a0, %lo(notification_string)
	move $ra, $t2

	/* Print message on serial console.  */
	/* In: $a0 = asciiz message. Out: none. Clobbered: $t0, $t1, $a0.  */
message:
	lui $t0, %hi (GRUB_MACHINE_SERIAL_PORT)
1:
	lb $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_LSR)($t0)
	andi $t1, $t1, UART_EMPTY_TRANSMITTER
	beq $t1, $zero, 1b
	 nop
	lb  $t1, 0($a0)
	sb  $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_TX)($t0)
	bne $t1, $zero, 1b
	 addiu $a0, $a0, 1
	jr  $ra
	 nop
	
	/* Print 32-bit hexadecimal on serial.
           In:	$a0. Out: None. Clobbered: $a0, $t0, $t1, $t2
	*/
printhex:
	lui $t0, %hi  (GRUB_MACHINE_SERIAL_PORT)
	ori $t2, $zero, 8
1:
	lb $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_LSR)($t0)
	andi $t1, $t1, UART_EMPTY_TRANSMITTER
	beq $t1, $zero, 1b
	 nop
	srl  $t1, $a0, 28
	addiu $t1, $t1, -10
	bltz  $t1, 2f
	 sll  $a0, $a0, 4
	addiu $t1, $t1, 'A'-10-'0' 
2:	addiu $t1, $t1, '0'+10
	sb  $t1, (%lo (GRUB_MACHINE_SERIAL_PORT) + UART_TX)($t0)
	addiu $t2, $t2, -1
	bne $t2, $zero, 1b
	 nop
	jr  $ra
	 nop

fatal:
	bal message
	 nop
self:
	b self
	 nop
	
	/* Write CS5536 MSR.
           In:   $a0 address, $a1 lower word, $a2 upper word.
           Out:	 None
           Clobbered:	 $t0
	*/
wrmsr:
	lui $t0, %hi(GRUB_MACHINE_PCI_CONFSPACE_2F)
	sw  $a0, (%lo(GRUB_MACHINE_PCI_CONFSPACE_2F) + GRUB_CS5536_MSR_MAILBOX_ADDR) ($t0)
	sw  $a1, (%lo(GRUB_MACHINE_PCI_CONFSPACE_2F) + GRUB_CS5536_MSR_MAILBOX_DATA0) ($t0)
	jr $ra
	 sw  $a2, (%lo(GRUB_MACHINE_PCI_CONFSPACE_2F) + GRUB_CS5536_MSR_MAILBOX_DATA1) ($t0)

	/* Wait for SMBus data or empty transmitter.  */
	/* In: $a0 = exception handler. Out: none. Clobbered: $t0, $t1  */
smbus_wait:
1:	
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_STATUS + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $t0, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_STATUS + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	andi $t1, $t0, GRUB_CS5536_SMB_REG_STATUS_SDAST
	bne $t1, $zero, return
	 nop
	andi $t1, $t0, (GRUB_CS5536_SMB_REG_STATUS_BER | GRUB_CS5536_SMB_REG_STATUS_NACK)
	beq $t1, $zero, 1b
	 nop
	jr $a0
	 nop
return:
	jr $ra
	 nop
	
	/* Read SPD byte. In: $a0 byte, $a1 device. Out: $v0 read byte (0x100 on failure).
           Clobbered: $t0, $t1, $t2, $t3, $a0. */
read_spd:
	move $t2, $a0
	move $t3, $ra
	lui $a0, %hi(read_spd_fail)
	addiu $a0, $a0, %lo(read_spd_fail)

	/* Send START.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	ori $t1, $t1, GRUB_CS5536_SMB_REG_CTRL1_START
	bal smbus_wait
	 sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	
	/* Send device address.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F)
	sll $t1, $a1, 1
	bal smbus_wait
	 sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	/* Send ACK.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	ori $t1, $t1, GRUB_CS5536_SMB_REG_CTRL1_ACK
	sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	/* Send byte address.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F)
	bal smbus_wait
	 sb $t2, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	/* Send START.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	ori $t1, $t1, GRUB_CS5536_SMB_REG_CTRL1_START
	bal smbus_wait
	 sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	/* Send device address.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F)
	sll $t1, $a1, 1
	ori $t1, $t1, 1
	bal smbus_wait
	 sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	/* Send STOP.  */
	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	ori $t1, $t1, GRUB_CS5536_SMB_REG_CTRL1_STOP
	bal smbus_wait
	 sb $t1, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_CTRL1 + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)

	lui $t0, %hi(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F)
	lb $v0, %lo(GRUB_CS5536_LBAR_SMBUS + GRUB_CS5536_SMB_REG_DATA + GRUB_MACHINE_PCI_IO_BASE_2F) ($t0)
	jr $t3
	 andi $v0, $v0, 0xff
read_spd_fail:
	jr $t3
	 ori $v0, $v0, 0x100

notification_string:	.asciz "GRUB "
cs5536_found:	.asciz "CS5536 at "
sm_failed: .asciz "SM transaction failed.\n\r"
unhandled_tlb_refill:	.asciz "Unhandled TLB refill.\n\r"
unhandled_cache_error:	.asciz "Unhandled cache error.\n\r"
unhandled_exception:	.asciz "Unhandled exception.\n\r"
smbus_enabled:	.asciz "SMBus controller enabled.\n\r"
unimplemented_memory_type:	.asciz "non-DDR2 memory isn't supported.\n\r"
no_cas_latency:		.asciz "Couldn't determine CAS latency.\n\r"
cause:	 .asciz "Cause: "
epc:	.asciz "\n\rEPC: "
badvaddr:	.asciz "\n\rBadVaddr: "
newline:	.asciz "\n\r"
return_msg:	  .asciz "\n\rReturn address: "
caches_enabled:	.asciz "Caches enabled\n\r"

	.p2align 3

regdump:
	.quad 0x0100010000000101 /* 0 */
	.quad 0x0100010100000000 /* 2 */
	.quad 0x0101000001000000 /* 3 */
	.quad 0x0100020200010101 /* 4 */
	.quad 0x0a04030603050203 /* 6 */
	.quad 0x0f0e040000010a0b /* 7 */
#ifdef FULOONG2F
	.quad 0x0000000100000001 /* 8 */
#else
	.quad 0x0000010200000102 /* 8 */
#endif
	.quad 0x0000060c00000000 /* 9 */
	.quad 0x2323233f3f1f0200 /* a */
	.quad 0x5f7f232323232323 /* b */
	.quad 0x002a3c0615000000 /* c */
	.quad 0x002a002a002a002a /* d */
	.quad 0x002a002a002a002a /* e */
#ifdef FULOONG2F
	.quad 0x00b40020005b0004 /* f */
#else
	.quad 0x00b40020006d0004 /* f */
#endif
	.quad 0x070007ff00000087 /* 10 */
	.quad 0x000000000016101f /* 11 */
	.quad 0x001c000000000000 /* 12 */
	.quad 0x28e1000200c8006b /* 13 */
	.quad 0x0000204200c8002f /* 14 */
	.quad 0x0000000000030d40 /* 15 */
	.quad 0 /* 16 */
	.quad 0 /* 17 */
	.quad 0 /* 18 */
	.quad 0 /* 19 */
	.quad 0 /* 1a */
	.quad 0 /* 1b */
	.quad 0 /* 1c */

/* Dump of GPIO connections. FIXME: Remove useless and macroify.  */
gpio_dump:
#ifdef FULOONG2F
	.long 0xffff0000, 0x2eefd110, 0xffff0000, 0xffff0000
	.long 0x2eefd110, 0xffff0000, 0x1000efff, 0xefff1000
	.long 0x3df3c20c, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0x7df3820c, 0x3df3c20c, 0xffff0000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0x3de3c21c, 0x3d83c27c
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0xffff0000, 0xffff0000, 0x0000ffff, 0xffff0000
	.long 0xefff1000, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0xefff1000, 0xefff1000, 0xffff0000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0xefff1000, 0xefff1000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
#else
	.long 0xffff0000, 0x2ffdd002, 0xffff0000, 0xffff0000
	.long 0x2fffd000, 0xffff0000, 0x1000efff, 0xefff1000
	.long 0x3ffbc004, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0x3ffbc004, 0x3ffbc004, 0xffff0000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0x3ffbc004, 0x3f9bc064
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0xffff0000, 0xffff0000, 0x0000ffff, 0xffff0000
	.long 0xefff1000, 0xffff0000, 0xffff0000, 0xffff0000
	.long 0xefff1000, 0xefff1000, 0xffff0000, 0x00000000
	.long 0xffff0000, 0xffff0000, 0xefff1000, 0xffff0000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x00000000, 0x00000000, 0x00000000
	.long 0x00000000, 0x50000000, 0x00000000, 0x00000000
#endif
gpio_dump_end:	

	.p2align 3

write_dumpreg:	
	ld $t2, 0($t6)
	sd $t2, 0($t4)
	addiu $t4, $t4, GRUB_CPU_LOONGSON_DDR2_REG_STEP
	jr $ra
	 addiu $t6, $t6, GRUB_CPU_LOONGSON_DDR2_REG_SIZE

continue:
	lui $t4, %hi(GRUB_CPU_LOONGSON_DDR2_BASE)
	addiu $t4, $t4, %lo(GRUB_CPU_LOONGSON_DDR2_BASE)
	lui $t6, %hi(regdump)

	/* 0 */
	bal write_dumpreg
	 addiu $t6, $t6, %lo(regdump)

	/* 1 */
	ori $a1, $a1, GRUB_SMB_RAM_START_ADDR
	move $t8, $zero
	lui  $t5, 0x0001
	bal read_spd
	 ori $a0, $zero, GRUB_SMBUS_SPD_MEMORY_NUM_BANKS_ADDR
	ori $t7, $zero, 8
	bne $v0, $t7, 1f
	 ori $t5, $t5, 0x0001
	ori $t8, $t8, GRUB_CPU_LOONGSON_DDR2_REG1_HI_8BANKS
1:
	dsll $t8, $t8, 32
	or $t5, $t5, $t8
	sd  $t5, 0 ($t4)
	addiu $t4, $t4, GRUB_CPU_LOONGSON_DDR2_REG_STEP

	/* 2 */
	bal write_dumpreg
	 nop

	/* 3 */
	bal write_dumpreg
	 nop

	/* 4 */
	bal write_dumpreg
	 nop

	/* 5 */
	/* FIXME: figure termination resistance.  */
	ori $t5, $zero, 0x2
	bal read_spd
	 ori $a0, $zero, GRUB_SMBUS_SPD_MEMORY_NUM_ROWS_ADDR
	/* $v0 = 15 - $v0.  */
	xori $v0, $v0, 0xf
	andi $v0, $v0, 0x7
	sll $v0, $v0, 8
	or $t5, $t5, $v0

	/* Find the fastest supported CAS latency.  */
	bal read_spd
	 ori $a0, $zero, GRUB_SMBUS_SPD_MEMORY_CAS_LATENCY_ADDR
	ori $t0, $zero, GRUB_SMBUS_SPD_MEMORY_CAS_LATENCY_MIN_VALUE
	ori $t1, $zero, (1 << GRUB_SMBUS_SPD_MEMORY_CAS_LATENCY_MIN_VALUE)
2:	
	and $t2, $t1, $v0
	bne $t2, $zero, 1f
	 ori $t3, $zero, 8
	lui $a0, %hi(no_cas_latency)
	beq $t0, $t3, fatal
	 addiu $a0, $a0, %lo(no_cas_latency)
	addiu $t0, $t0, 1
	b 2b
	 sll $t1, $t1, 1
1:
	sll $t0, $t0, 16
	or $t5, $t5, $t0
	
	bal read_spd
	 ori $a0, $zero, GRUB_SMBUS_SPD_MEMORY_NUM_COLUMNS_ADDR
	/* $v0 = 15 - ($v0 + 1) = 14 - $v0.  */
	addiu $v0, $v0, 1
	xori $v0, $v0, 0xf
	andi $v0, $v0, 0x7
	sll $v0, 24
	or $t5, $t5, $v0
	sd  $t5, 0 ($t4)

	addiu $t4, $t4, GRUB_CPU_LOONGSON_DDR2_REG_STEP
	
	ori $t7, $zero, 0x16

1:	
	ld $t2, 0($t6)
	sd $t2, 0($t4)
	addiu $t4, $t4, GRUB_CPU_LOONGSON_DDR2_REG_STEP
	addiu $t7, $t7, -1
	bne $t7, $zero, 1b
	 addiu $t6, $t6, GRUB_CPU_LOONGSON_DDR2_REG_SIZE
	
	lui $t4, %hi(GRUB_CPU_LOONGSON_DDR2_BASE)
	ld  $t5, (%lo(GRUB_CPU_LOONGSON_DDR2_BASE) + 0x30) ($t4)
	ori $t0, $zero, 1
	dsll $t0, $t0, 40
	or $t5, $t5, $t0
	sd  $t5, (%lo(GRUB_CPU_LOONGSON_DDR2_BASE) + 0x30) ($t4)

	/* Desactivate DDR2 registers.  */
        lui	$t0, %hi (GRUB_CPU_LOONGSON_CORECFG)
        ld	$t1, %lo (GRUB_CPU_LOONGSON_CORECFG) ($t0)
	ori	$t1, $t1, GRUB_CPU_LOONGSON_CORECFG_DISABLE_DDR2_SPACE
        sd	$t1, %lo (GRUB_CPU_LOONGSON_CORECFG) ($t0)

	/* Enable cache.  */
	mfc0	$t0, GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG
	addiu 	$t1, $zero, ~GRUB_CPU_LOONGSON_CACHE_TYPE_MASK
	and     $t0, $t1, $t1
	/* Set line size to 32 bytes and disabled cache.  */
	ori   	$t0, $t0, (GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG_ILINESIZE \
	                   | GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG_DLINESIZE \
	                   | GRUB_CPU_LOONGSON_CACHE_ACCELERATED)
	mtc0	$t0, GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG

	/* Invalidate all I-cache entries.  */
	srl $t1, $t0, GRUB_CPU_LOONGSON_COP0_CACHE_ISIZE_SHIFT
	andi $t1, $t1, GRUB_CPU_LOONGSON_COP0_CACHE_SIZE_MASK
	ori $t2, $zero, (1 << (GRUB_CPU_LOONGSON_COP0_CACHE_SIZE_OFFSET \
	                       - GRUB_CPU_LOONGSON_CACHE_LINE_SIZE_LOG_BIG \
	                       - GRUB_CPU_LOONGSON_I_CACHE_LOG_WAYS))
	sll $t1, $t2, $t1
	lui $t2, 0x8000

1:	
	cache GRUB_CPU_LOONGSON_COP0_I_INDEX_INVALIDATE, 0($t2)
	addiu $t1, $t1, -1
	bne $t1, $zero, 1b
	 addiu $t2, $t2, (1 << GRUB_CPU_LOONGSON_COP0_I_INDEX_BIT_OFFSET)

	/* Invalidate all D-cache entries.  */
	srl $t1, $t0, GRUB_CPU_LOONGSON_COP0_CACHE_DSIZE_SHIFT
	andi $t1, $t1, GRUB_CPU_LOONGSON_COP0_CACHE_SIZE_MASK
	ori $t2, $zero, (1 << (GRUB_CPU_LOONGSON_COP0_CACHE_SIZE_OFFSET \
	                       - GRUB_CPU_LOONGSON_CACHE_LINE_SIZE_LOG_BIG \
	                       - GRUB_CPU_LOONGSON_D_CACHE_LOG_WAYS))
	sll $t1, $t2, $t1
	lui $t2, 0x8000
	mtc0 $zero, GRUB_CPU_LOONGSON_COP0_CACHE_TAGLO
	mtc0 $zero, GRUB_CPU_LOONGSON_COP0_CACHE_TAGHI
1:
	/* All four ways.  */
	cache GRUB_CPU_LOONGSON_COP0_D_INDEX_TAG_STORE, 0($t2)
	cache GRUB_CPU_LOONGSON_COP0_D_INDEX_TAG_STORE, 1($t2)
	cache GRUB_CPU_LOONGSON_COP0_D_INDEX_TAG_STORE, 2($t2)
	cache GRUB_CPU_LOONGSON_COP0_D_INDEX_TAG_STORE, 3($t2)
	addiu $t1, $t1, -1
	bne $t1, $zero, 1b
	 addiu $t2, $t2, (1 << GRUB_CPU_LOONGSON_COP0_D_INDEX_BIT_OFFSET)

	/* Invalidate all S-cache entries.  */
	ori $t1, $zero, (1 << (GRUB_CPU_LOONGSON_SECONDARY_CACHE_LOG_SIZE \
	                       - GRUB_CPU_LOONGSON_CACHE_LINE_SIZE_LOG_BIG \
	                       - GRUB_CPU_LOONGSON_S_CACHE_LOG_WAYS))
	lui $t2, 0x8000
	mtc0 $zero, GRUB_CPU_LOONGSON_COP0_CACHE_TAGLO
	mtc0 $zero, GRUB_CPU_LOONGSON_COP0_CACHE_TAGHI
1:
	/* All four ways.  */
	cache GRUB_CPU_LOONGSON_COP0_S_INDEX_TAG_STORE, 0($t2)
	cache GRUB_CPU_LOONGSON_COP0_S_INDEX_TAG_STORE, 1($t2)
	cache GRUB_CPU_LOONGSON_COP0_S_INDEX_TAG_STORE, 2($t2)
	cache GRUB_CPU_LOONGSON_COP0_S_INDEX_TAG_STORE, 3($t2)
	addiu $t1, $t1, -1
	bne $t1, $zero, 1b
	 addiu $t2, $t2, (1 << GRUB_CPU_LOONGSON_COP0_D_INDEX_BIT_OFFSET)
	
	/* Finally enable cache.  */
	mfc0	$t0, GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG
	addiu 	$t1, $zero, ~GRUB_CPU_LOONGSON_CACHE_TYPE_MASK
	and     $t0, $t1, $t1
	ori   	$t0, $t0, GRUB_CPU_LOONGSON_CACHE_CACHED
	mtc0	$t0, GRUB_CPU_LOONGSON_COP0_CACHE_CONFIG

	lui $a0, %hi(caches_enabled)
	bal message
	  addiu $a0, $a0, %lo(caches_enabled)

	/* Set ROM delay cycles to 1.  */
	lui $t0, %hi(GRUB_CPU_LOONGSON_LIOCFG)
	lw  $t1, %lo(GRUB_CPU_LOONGSON_LIOCFG) ($t0)
	addiu $t2, $zero, ~(GRUB_CPU_LOONGSON_ROM_DELAY_MASK \
	                    << GRUB_CPU_LOONGSON_ROM_DELAY_OFFSET)
	and $t1, $t1, $t2
	ori $t1, $t1, (1 << GRUB_CPU_LOONGSON_ROM_DELAY_OFFSET)
	sw  $t1, %lo(GRUB_CPU_LOONGSON_LIOCFG) ($t0)
	
	addiu $a0, $zero, -1
	addiu $a1, $zero, -1

	/* Take advantage of cache.  */
	lui $t0, %hi(cached_continue - 0x20000000)
	addiu $t0, $t0, %lo(cached_continue - 0x20000000)
	jr $t0
#ifdef FULOONG2F
	 addiu $a2, $zero, -(1 + GRUB_ARCH_MACHINE_FULOONG2F)
#else
	 addiu $a2, $zero, -(1 + GRUB_ARCH_MACHINE_YEELOONG)
#endif

cached_continue:
