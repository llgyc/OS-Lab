# Report for lab3, Yuchen Gu

完成的challenge如下：

```
Challenge! Modify the JOS kernel monitor so that you can 'continue' execution from the current location (e.g., after the int3, if the kernel monitor was invoked via the breakpoint exception), and so that you can single-step one instruction at a time. You will need to understand certain bits of the EFLAGS register in order to implement single-stepping.
```


## Environment Configuration

```
Hardware Environment:
Memory:         2GB
Processor:      Intel(R) Core(TM) i7-8750H CPU @ 2.20GHz
OS Type:        32 bit
Disk:           20GB

Software Environment:
OS:             Ubuntu 18.04.5 LTS
Gcc:            gcc (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0
Make:           GNU Make 4.1
Gdb:            GNU gdb (Ubuntu 8.1-0ubuntu3.2) 8.1.0.20180409-git

```

## Part A: User Environments and Exception Handling

### Allocating the Environments Array

#### Exercise 1

仿照lab2完成即可，由于在`kern/env.c`中定义了envs变量，所以只需extern声明即可，映射按照`inc/memlayout.h`中的进行即可

```c
	extern struct Env *envs;
	envs = (struct Env *)boot_alloc(NENV * sizeof(struct Env));
	boot_map_region(kern_pgdir, UENVS, PTSIZE, PADDR(envs), PTE_U);
```

### Creating and Running Environments

#### Exercise 2

`env_init()`因为要保证链表第一个是0，所以倒着循环

	for (int i = NENV - 1; i >= 0; i--) {
		envs[i].env_status = ENV_FREE;
		envs[i].env_id = 0;
		envs[i].env_link = env_free_list;
		env_free_list = &envs[i];
	}

`env_setup_vm()` 直接使用kernel页目录作为模板，可以避免大量手动修改

	p->pp_ref++;
	e->env_pgdir = page2kva(p);
	memcpy(e->env_pgdir, kern_pgdir, PGSIZE);
`region_alloc()` 特殊情况包括：长度为0的时候，有可能多分出来一页；需要的空间太多（新建页表等操作）

	if (e == NULL)
		panic("region_alloc: Env pointer is null\n");
	if (len == 0)
		return;
	char *start_va = ROUNDDOWN((char *)va, PGSIZE);
	char *end_va = ROUNDUP((char *)va + len, PGSIZE);
	if (start_va > end_va)
		panic("region_alloc: Out of memory.\n");
	for (; start_va != end_va; start_va += PGSIZE) {
		struct PageInfo *pp = page_alloc(0);
		if (pp == NULL)
			panic("region_alloc: Out of memory.\n");
		if (page_insert(e->env_pgdir, pp, start_va, PTE_U | PTE_W) < 0)
			panic("region_alloc: page_insert failed.\n");
	}
`load_icode()`仿照`boot/main.c`写即可，枚举每个需要load的section，然后先整体置0，再把需要的部分复制进去，这样可以减少特判，这里注意要切换成新环境的页表，否则由于分配的物理页不连续不方便在内核页表下进行memcpy，而进入env_pgdir后，起始位置和目标位置在虚拟地址中都是连续的一段

```
	struct Elf *elf = (struct Elf *)binary;
	if (elf->e_magic != ELF_MAGIC)
		panic("load_icode: Not a valid ELF binary.\n");
	
	lcr3(PADDR(e->env_pgdir));
	struct Proghdr *ph, *eph;
	ph = (struct Proghdr *) (binary + elf->e_phoff);
	eph = ph + elf->e_phnum;
	for (; ph < eph; ph++) {
		if (ph->p_type != ELF_PROG_LOAD) continue;
		region_alloc(e, (void *)ph->p_va, ph->p_memsz);
		memset((void *)ph->p_va, 0, ph->p_memsz);
		memcpy((void *)ph->p_va, (void *)(binary + ph->p_offset), ph->p_filesz);
	}
	lcr3(PADDR(kern_pgdir));
	e->env_tf.tf_eip = elf->e_entry;

	region_alloc(e, (void *)(USTACKTOP - PGSIZE), PGSIZE);
```

`env_create()`按照注释完成即可，这里可以用handout中提到的`%e`输出错误信息

```
	struct Env *e;
	int ret = env_alloc(&e, 0);
	if (ret < 0)
		panic("env_alloc: %e", -ret);
	load_icode(e, binary);	
	e->env_type = type;
```

`env_run()`同样按照注释一步步写即可，这个函数是不会返回的

```
	if (e->status != ENV_RUNNABLE)
		panic("env_run: A not runnable environment is scheduled.\n");
	if (curenv && curenv->env_status == ENV_RUNNING) 
		curenv->env_status = ENV_RUNNABLE;
	curenv = e;
	e->env_status = ENV_RUNNING;
	e->env_runs++;
	lcr3(PADDR(e->env_pgdir));
	
	env_pop_tf(&e->env_tf);
```

实现以后，正如handout所述，出现了triple fault：

```
***
*** Use Ctrl-a x to exit qemu
***
qemu-system-i386 -nographic -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
[00000000] new env 00001000
EAX=00000000 EBX=00000000 ECX=0000000d EDX=eebfde88
ESI=00000000 EDI=00000000 EBP=eebfde60 ESP=eebfde54
EIP=00800d03 EFL=00000092 [--S-A--] CPL=3 II=0 A20=1 SMM=0 HLT=0
ES =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
CS =001b 00000000 ffffffff 00cffa00 DPL=3 CS32 [-R-]
SS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
DS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
FS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
GS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
LDT=0000 00000000 00000000 00008200 DPL=0 LDT
TR =0028 f018fba0 00000067 00408900 DPL=0 TSS32-avl
GDT=     f011c300 0000002f
IDT=     f018f380 000007ff
CR0=80050033 CR2=00000000 CR3=003bc000 CR4=00000000
DR0=00000000 DR1=00000000 DR2=00000000 DR3=00000000 
DR6=ffff0ff0 DR7=00000400
EFER=0000000000000000
Triple fault.  Halting for inspection via QEMU monitor.
```

并且我们可以通过gdb调试发现确实运行到了int指令的位置才发生了出错

```
The target architecture is assumed to be i8086
[f000:fff0]    0xffff0:	ljmp   $0xf000,$0xe05b
0x0000fff0 in ?? ()
+ symbol-file obj/kern/kernel
(gdb) b env_pop_tf 
Breakpoint 1 at 0xf0103e8c: file kern/env.c, line 469.
(gdb) c
Continuing.
The target architecture is assumed to be i386
=> 0xf0103e8c <env_pop_tf>:	push   %ebp

Breakpoint 1, env_pop_tf (tf=0xf01d2000) at kern/env.c:469
469	{
(gdb) si
=> 0xf0103e8d <env_pop_tf+1>:	mov    %esp,%ebp
0xf0103e8d	469	{
(gdb) disas
Dump of assembler code for function env_pop_tf:
   0xf0103e8c <+0>:	push   %ebp
=> 0xf0103e8d <+1>:	mov    %esp,%ebp
   0xf0103e8f <+3>:	push   %ebx
   0xf0103e90 <+4>:	sub    $0x8,%esp
   0xf0103e93 <+7>:	call   0xf0100167 <__x86.get_pc_thunk.bx>
   0xf0103e98 <+12>:	add    $0x89a48,%ebx
   0xf0103e9e <+18>:	mov    0x8(%ebp),%esp
   0xf0103ea1 <+21>:	popa   
   0xf0103ea2 <+22>:	pop    %es
   0xf0103ea3 <+23>:	pop    %ds
   0xf0103ea4 <+24>:	add    $0x8,%esp
   0xf0103ea7 <+27>:	iret   
   0xf0103ea8 <+28>:	lea    -0x86d57(%ebx),%eax
   0xf0103eae <+34>:	push   %eax
   0xf0103eaf <+35>:	push   $0x1de
   0xf0103eb4 <+40>:	lea    -0x86db9(%ebx),%eax
   0xf0103eba <+46>:	push   %eax
   0xf0103ebb <+47>:	call   0xf01000b1 <_panic>
End of assembler dump.
(gdb) b *0xf0103ea7
Breakpoint 2 at 0xf0103ea7: file kern/env.c, line 470.
(gdb) c
Continuing.
=> 0xf0103ea7 <env_pop_tf+27>:	iret   

Breakpoint 2, 0xf0103ea7 in env_pop_tf (
    tf=<error reading variable: Unknown argument list address for `tf'.>)
    at kern/env.c:470
470		asm volatile(
(gdb) si
=> 0x800020:	cmp    $0xeebfe000,%esp
0x00800020 in ?? ()
(gdb) disas
No function contains program counter for selected frame.
(gdb) si
=> 0x800026:	jne    0x80002c
0x00800026 in ?? ()
(gdb) b *0x800d03
Breakpoint 3 at 0x800d03
(gdb) c
Continuing.
=> 0x800d03:	int    $0x30

Breakpoint 3, 0x00800d03 in ?? ()
(gdb) si
=> 0x800d03:	int    $0x30

Breakpoint 3, 0x00800d03 in ?? ()
```

### Handling Interrupts and Exceptions

#### Exercise 3

提供的参考资料是关于中断和异常如何处理的介绍，handout后面几段内容就是在对资料里的内容进行总结，一个之后会用到的图是中断号和一些默认信息：

![image-20201128203854267](C:\Users\llgyc\AppData\Roaming\Typora\typora-user-images\image-20201128203854267.png)

![image-20201128203920372](C:\Users\llgyc\AppData\Roaming\Typora\typora-user-images\image-20201128203920372.png)

![image-20201128203937444](C:\Users\llgyc\AppData\Roaming\Typora\typora-user-images\image-20201128203937444.png)

### Setting Up the IDT

#### Exercise 4

这个练习是要完成中断向量表的设置，使得每个中断都在压栈完成后跳转到trap()函数中

观察到trapframe的格式如下，发生中断时，CPU会自动将第一个注释以下的部分压栈（可能不包括error code）

```
struct Trapframe {
	struct PushRegs tf_regs;
	uint16_t tf_es;
	uint16_t tf_padding1;
	uint16_t tf_ds;
	uint16_t tf_padding2;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding3;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding4;
} __attribute__((packed));
```

因此我们的handler首先要完成的就是将剩余部分压栈完成，首先是在trapentry.S中的代码

```
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
TRAPHANDLER_NOEC(HANDLER48, T_SYSCALL);
```

利用给定的宏将类型分为带error code和不带的两种情况，这里做的是将tf_trapno和可能缺失的tf_err压栈，一旦类型trapno被压入后，后面的代码部分是可以共用的，也就是下面的`_alltraps`函数，做的就是练习中提到的四个步骤

```
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
```

最后再在trap.c中把生成的函数利用SETGATE宏真正加入IDT中：

```
	void HANDLER0();
	void HANDLER1();
	void HANDLER2();
	void HANDLER3();
	void HANDLER4();
	void HANDLER5();
	void HANDLER6();
	void HANDLER7();
	void HANDLER8();
	void HANDLER10();
	void HANDLER11();
	void HANDLER12();
	void HANDLER13();
	void HANDLER14();
	void HANDLER16();
	void HANDLER17();
	void HANDLER18();
	void HANDLER19();
	void HANDLER48();
	
	SETGATE(idt[0], 0, GD_KT, HANDLER0, 0);
	SETGATE(idt[1], 0, GD_KT, HANDLER1, 0);
	SETGATE(idt[2], 0, GD_KT, HANDLER2, 0);
	SETGATE(idt[3], 0, GD_KT, HANDLER3, 0);
	SETGATE(idt[4], 0, GD_KT, HANDLER4, 0);
	SETGATE(idt[5], 0, GD_KT, HANDLER5, 0);
	SETGATE(idt[6], 0, GD_KT, HANDLER6, 0);
	SETGATE(idt[7], 0, GD_KT, HANDLER7, 0);
	SETGATE(idt[8], 0, GD_KT, HANDLER8, 0);
	SETGATE(idt[10], 0, GD_KT, HANDLER10, 0);
	SETGATE(idt[11], 0, GD_KT, HANDLER11, 0);
	SETGATE(idt[12], 0, GD_KT, HANDLER12, 0);
	SETGATE(idt[13], 0, GD_KT, HANDLER13, 0);
	SETGATE(idt[14], 0, GD_KT, HANDLER14, 0);
	SETGATE(idt[16], 0, GD_KT, HANDLER16, 0);
	SETGATE(idt[17], 0, GD_KT, HANDLER17, 0);
	SETGATE(idt[18], 0, GD_KT, HANDLER18, 0);
	SETGATE(idt[19], 0, GD_KT, HANDLER19, 0);
	SETGATE(idt[48], 0, GD_KT, HANDLER48, 0);
```

#### Question 1

**What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)**

因为首先不同类型中断/异常CPU的自动压栈方式不一样，需要区分处理，而且如果合用handler则无法记录中断类型，后续无法运行对应的中断处理程序。

#### Question 2

**Did you have to do anything to make the `user/softint` program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but `softint`'s code says `int $14`. *Why* should this produce interrupt vector 13? What happens if the kernel actually allows `softint`'s `int $14` instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?**

没有特殊处理，因为int 14是缺页异常，而用户程序没有权限申请该异常，因此触发了general protection exception，也就是13号中断，如果用户程序可以触发缺页异常，则容易操控虚存，进而攻击操作系统。

## Part B: Page Faults, Breakpoints Exceptions, and System Calls

### Handling Page Faults

#### Exercise 5

只需要在`trap_dispatch()`中加入以下分支即可

```
case T_PGFLT:
			page_fault_handler(tf);
			break;
```

### The Breakpoint Exception

#### Exercise 6

与上一小题完全类似，但注意到允许用户触发异常，因此上方的IDT设置中应改为

```
	SETGATE(idt[3], 0, GD_KT, HANDLER3, 3);
```

同时

```
case T_BRKPT:
			monitor(tf);
			break;`
```

#### Challenge

***Challenge!* Modify the JOS kernel monitor so that you can 'continue' execution from the current location (e.g., after the `int3`, if the kernel monitor was invoked via the breakpoint exception), and so that you can single-step one instruction at a time. You will need to understand certain bits of the `EFLAGS` register in order to implement single-stepping.**

需要添加continue和stepi两个指令，对应着gdb中这两个同名指令的功能

![image-20201129090611606](C:\Users\llgyc\AppData\Roaming\Typora\typora-user-images\image-20201129090611606.png)

![image-20201129090909354](C:\Users\llgyc\AppData\Roaming\Typora\typora-user-images\image-20201129090909354.png)

因此可以知道当需要继续执行的时候把TF reset，需要单步执行的时候set即可

添加代码如下：

```
int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if (tf == NULL) {
		cprintf("Error: No Env has been trapped\n");
		return 0; // stay in monitor
	}
	tf->tf_eflags &= ~(FL_TF);
	return -1; // exit monitor
}

int
mon_stepi(int argc, char **argv, struct Trapframe *tf)
{
	if (tf == NULL) {
		cprintf("Error: No Env has been trapped\n");
		return 0; // stay in monitor
	}
	tf->tf_eflags |= FL_TF;
	return -1; // exit monitor
}
```

就完成了该challenge

#### Question 3

**The break point test case will either generate a break point exception or a general protection fault depending on how you initialized the break point entry in the IDT (i.e., your call to `SETGATE` from `trap_init`). Why? How do you need to set it up in order to get the breakpoint exception to work as specified above and what incorrect setup would cause it to trigger a general protection fault?**

因为根据IDT中设置的DPL权限不同来决定breakpoint异常能不能由用户态发起，如果DPL=0，那么用户态执行了权限不够的指令，触发general protection fault，若DPL=3，则正常执行，触发breakpoint exception。因为题目要求用户态可以打断点，因此要在SETGATE对应的一行中改为DPL=3，即Exercise 6中提到的部分。

#### Question 4

**What do you think is the point of these mechanisms, particularly in light of what the `user/softint` test program does?**

这些机制的目的是为了防止恶意的用户程序随意对内核进行攻击，限制他们的权限等级，保护整个操作系统。

### System calls

#### Exercise 7

这里kern/trapentry.S和kern/trap.c中已经添加过相关代码了，注意到用户态可以调用syscall，因此作下列修改

```
	SETGATE(idt[48], 0, GD_KT, HANDLER48, 3);
```

根据`lib/syscall.c`，在trap_dispatch()中增加一个case:

```
	case T_SYSCALL:
		tf->tf_regs.reg_eax = 
		syscall(tf->tf_regs.reg_eax, tf->tf_regs.reg_edx, tf->tf_regs.reg_ecx,
			tf->tf_regs.reg_ebx, tf->tf_regs.reg_edi, tf->tf_regs.reg_esi);
		break;
```

同时syscall()函数中补全如下：

```
	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((const char *)a1, a2);
		return 0;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		assert(curenv);
		return sys_getenvid();
	case SYS_env_destroy:
		assert(curenv);
		return sys_env_destroy(a1);
	default:
		return -E_INVAL;
	}
```

### User-mode startup

#### Exercise 8

获取env指针只需要利用系统调用sys_getenvid即可，ENVX宏可以提取出在数组中的下标

```
	thisenv = envs + ENVX(sys_getenvid());
```

### Page faults and memory protection

#### Exercise 9

处理内核态page fault只需要增加一句，利用CS寄存器低两位来判断当前CPL，从而知道在什么状态出现的fault：

```
	if ((tf->tf_cs & 3) == 0)
		panic("Error: page fault in kernel mode.\n");
```

user_mem_check前面特判部分与region_alloc基本一致，后面就是枚举每一页对应的页表项看看权限是否正确，同时判断地址是否在ULIM下方
```
int
user_mem_check(struct Env *env, const void *va, size_t len, int perm)
{
	// LAB 3: Your code here.
	if (env == NULL)
		return -E_FAULT;
	if (len == 0)
		return 0;
	char *start_va = ROUNDDOWN((char *)va, PGSIZE);
	char *end_va = ROUNDUP((char *)va + len, PGSIZE);
	if (start_va > end_va)
		return -E_FAULT;
	perm |= PTE_P;
	for (; start_va < end_va; start_va += PGSIZE) {
		pte_t *p_pte = pgdir_walk(env->env_pgdir, start_va, 0);
		if ((uint32_t)start_va >= ULIM || !p_pte || ((*p_pte) & perm) != perm) {
			user_mem_check_addr = (uint32_t)va < (uint32_t)start_va
				? (uint32_t)start_va : (uint32_t)va;
			return -E_FAULT;
		}
	}

	return 0;
}

```

然后syscall中只有sys_cputs函数的指针传参需要检查：

```
	user_mem_assert(curenv, s, len, PTE_U);
```

最后debuginfo_eip中增加三个memory check：

```
		if (user_mem_check(curenv, usd, sizeof(struct UserStabData), PTE_U) < 0)
			return -1;
		if (user_mem_check(curenv, stabs, (stab_end - stabs) * sizeof(struct Stab), PTE_U) < 0)
			return -1;
		if (user_mem_check(curenv, stabstr, (stabstr_end - stabstr), PTE_U) < 0)
			return -1;
```

完成了以后，`make run-breakpoint`可以看到：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-breakpoint
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01d3000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfc0
  oesp 0xefffffdc
  ebx  0x00802004
  edx  0x00802034
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x00000003 Breakpoint
  err  0x00000000
  eip  0x00800037
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfc0
  ss   0x----0023
K> backtrace
Stack backtrace:
  ebp efffff00  eip f0101034  args 00000001 efffff28 f01d3000 ffffffff f011bf48
         kern/monitor.c:312: monitor+370
  ebp efffff80  eip f010495b  args f01d3000 efffffbc f0151914 00000092 f011bfd8
         kern/trap.c:188: trap+239
  ebp efffffb0  eip f0104a60  args efffffbc 00000000 00000000 eebfdfc0 efffffdc
         kern/syscall.c:69: syscall+0
  ebp eebfdfc0  eip 00800087  args 00000000 00000000 eebfdff0 00800058 00000000
         lib/libmain.c:26: libmain+78
  ebp eebfdff0  eip 00800031  args 00000000 00000000Incoming TRAP frame at 0xeffffe64
kernel panic at kern/trap.c:261: Error: page fault in kernel mode.

```

确实在page fault之前进入了libmain.c中，可以发现是在输出的时候触发了page fault，后面三个参数无法输出，我们看一下此时ebp的地址刚好里USTACKTOP还有4个32-bit word的距离，分别存着上个栈帧（如果有的话）的ebp，返回地址和libmain的两个参数，其中观察entry.S函数可以发现：

```
.text
.globl _start
_start:
	// See if we were started with arguments on the stack
	cmpl $USTACKTOP, %esp
	jne args_exist

	// If not, push dummy argc/argv arguments.
	// This happens when we are loaded by the kernel,
	// because the kernel does not know about passing arguments.
	pushl $0
	pushl $0

args_exist:
	call libmain
1:	jmp 1b

```

在内核调用用户程序时，只压了两个参数0就调用了libmain因此再往上的部分超出了栈边界，无法访问，因此在输出时会触发page fault。

#### Exercise 10

只需要运行`make run-evilhello`即可，可以看到如下结果：

```
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Incoming TRAP frame at 0xefffffbc
[00001000] user_mem_check assertion failure for va f010000c
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```

最后`make grade`可以看到

```
divzero: OK (2.4s) 
softint: OK (1.2s) 
badsegment: OK (1.2s) 
Part A score: 30/30

faultread: OK (1.6s) 
faultreadkernel: OK (1.5s) 
faultwrite: OK (2.3s) 
faultwritekernel: OK (1.3s) 
breakpoint: OK (1.9s) 
testbss: OK (2.4s) 
hello: OK (2.5s) 
buggyhello: OK (2.5s) 
buggyhello2: OK (2.8s) 
evilhello: OK (2.3s) 
Part B score: 50/50

Score: 80/80
```

## This Complete The Lab.

