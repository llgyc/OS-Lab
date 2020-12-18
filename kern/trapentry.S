/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(HANDLER0, T_DIVIDE);
TRAPHANDLER_NOEC(HANDLER1, T_DEBUG);
TRAPHANDLER_NOEC(HANDLER2, T_NMI);
TRAPHANDLER_NOEC(HANDLER3, T_BRKPT);
TRAPHANDLER_NOEC(HANDLER4, T_OFLOW);
TRAPHANDLER_NOEC(HANDLER5, T_BOUND);
TRAPHANDLER_NOEC(HANDLER6, T_ILLOP);
TRAPHANDLER_NOEC(HANDLER7, T_DEVICE);
TRAPHANDLER(HANDLER8, T_DBLFLT);
/* TRAPHANDLER_NOEC(HANDLER9, T_COPROC); */
TRAPHANDLER(HANDLER10, T_TSS);
TRAPHANDLER(HANDLER11, T_SEGNP);
TRAPHANDLER(HANDLER12, T_STACK);
TRAPHANDLER(HANDLER13, T_GPFLT);
TRAPHANDLER(HANDLER14, T_PGFLT);
/* TRAPHANDLER_NOEC(HANDLER15, T_RES); */
TRAPHANDLER_NOEC(HANDLER16, T_FPERR);
TRAPHANDLER(HANDLER17, T_ALIGN);
TRAPHANDLER_NOEC(HANDLER18, T_MCHK);
TRAPHANDLER_NOEC(HANDLER19, T_SIMDERR);
TRAPHANDLER_NOEC(HANDLER32, IRQ_OFFSET+IRQ_TIMER);
TRAPHANDLER_NOEC(HANDLER33, IRQ_OFFSET+IRQ_KBD);
TRAPHANDLER_NOEC(HANDLER36, IRQ_OFFSET+IRQ_SERIAL);
TRAPHANDLER_NOEC(HANDLER39, IRQ_OFFSET+IRQ_SPURIOUS);
TRAPHANDLER_NOEC(HANDLER46, IRQ_OFFSET+IRQ_IDE);
TRAPHANDLER_NOEC(HANDLER48, T_SYSCALL);
TRAPHANDLER_NOEC(HANDLER51, IRQ_OFFSET+IRQ_ERROR);

/*
 * Lab 3: Your code here for _alltraps
 */

	.globl _alltraps
	.type _alltraps, @function
	.align 2
_alltraps:
	pushw $0
	pushw %ds
	pushw $0
	pushw %es
	pushal
	movl $GD_KD, %eax
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	call trap
