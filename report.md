# Report for lab4, Yuchen Gu

完成的challenge如下：

```
Challenge! Implement a shared-memory fork() called sfork(). This version should have the parent and child share all their memory pages (so writes in one environment appear in the other) except for pages in the stack area, which should be treated in the usual copy-on-write manner. Modify user/forktree.c to use sfork() instead of regular fork(). Also, once you have finished implementing IPC in part C, use your sfork() to run user/pingpongs. You will have to find a new way to provide the functionality of the global thisenv pointer.
```

这个challenge是在完成lab之后实现的，由于需要修改user中以及其它多处的代码，所以是新建了一个分支实现的，分成两个压缩包提交，包含challenge的压缩包名称中含有`sfork`

sfork的实现和fork相比的区别就是除了栈空间的部分应该映射为同一个位置并且有相同的权限，这样如果在一边修改了就会体现在另一边也就体现了share-memory，具体体现在下面除了栈空间部分用了sduppage函数，栈空间依然用了duppage函数映射。

```
static int
sduppage(envid_t envid, unsigned pn)
{
	int r, perm;
	void *addr;
	
	addr = (void *)(pn * PGSIZE);
	perm = uvpt[pn] & PTE_SYSCALL;
	if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
		panic("sys_page_map: %e", r);
	return 0;
}

int
sfork(void)
{
	envid_t envid;
	uint8_t *addr;
	int r;
	extern void _pgfault_upcall(void);
	
	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The following line becomes meaningless with shared memory
		// thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	// We're the parent.
	for (addr = 0; addr < (uint8_t *)(USTACKTOP - PGSIZE); addr += PGSIZE)
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)
			&& (uvpt[PGNUM(addr)] & PTE_U))
			sduppage(envid, PGNUM(addr));
	duppage(envid, PGNUM(USTACKTOP - PGSIZE));
	
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U|PTE_P|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: %e", r);
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	return envid;
}
```

这里还有一个需要注意的地方是thisenv指针不再能使用，因为这个变量在sfork后是存放在共享空间中的，所以无论是否修改，体现的都是父子进程其中一边的值，因此在ipc_recv函数中不能再用这个变量，而应该每次需要这个含义的指针时，重新通过系统调用获取，体现在代码中就是在函数中新建了一个同名的临时变量覆盖。

```
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	int r;
	pg = pg ? pg : (void *)UTOP;
	if ((r = sys_ipc_recv(pg)) < 0) {
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
		return r;
	}
	const volatile struct Env *thisenv = envs + ENVX(sys_getenvid());
	if (from_env_store)
		*from_env_store = thisenv->env_ipc_from;
	if (perm_store)
		*perm_store = thisenv->env_ipc_perm;
	return thisenv->env_ipc_value;
}
```

修改了这个以后`make run-pingpong`才能通过测试

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-forktree
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
1000: I am ''
[00001000] new env 00001001
[00001000] new env 00001002
1001: I am '0'
[00001001] new env 00001003
[00001001] new env 00001004
1003: I am '00'
[00001003] new env 00001005
[00001000] exiting gracefully
[00001000] free env 00001000
1002: I am '1'
[00001002] new env 00002000
[00001002] new env 00001006
[00001003] new env 00001007
1005: I am '000'
[00001005] exiting gracefully
[00001005] free env 00001005
2000: I am '10'
[00002000] new env 00002005
[00001001] exiting gracefully
[00001001] free env 00001001
[00001002] exiting gracefully
[00001002] free env 00001002
[00001003] exiting gracefully
[00001003] free env 00001003
[00002000] new env 00002003
1004: I am '01'
[00001004] new env 00002002
2005: I am '100'
[00002005] exiting gracefully
[00002005] free env 00002005
[00001004] new env 00003005
1006: I am '11'
[00001006] new env 00002001
[00001006] new env 00001008
1007: I am '001'
[00001007] exiting gracefully
[00001007] free env 00001007
2001: I am '110'
[00002001] exiting gracefully
[00002001] free env 00002001
2002: I am '010'
[00002002] exiting gracefully
[00002002] free env 00002002
[00002000] exiting gracefully
[00002000] free env 00002000
2003: I am '101'
[00002003] exiting gracefully
[00002003] free env 00002003
[00001006] exiting gracefully
[00001006] free env 00001006
1008: I am '111'
[00001008] exiting gracefully
[00001008] free env 00001008
[00001004] exiting gracefully
[00001004] free env 00001004
3005: I am '011'
[00003005] exiting gracefully
[00003005] free env 00003005
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-pingpong
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] new env 00001001
send 0 from 1000 to 1001
1001 got 0 from 1000
1000 got 1 from 1001
1001 got 2 from 1000
1000 got 3 from 1001
1001 got 4 from 1000
1000 got 5 from 1001
1001 got 6 from 1000
1000 got 7 from 1001
1001 got 8 from 1000
1000 got 9 from 1001
[00001000] exiting gracefully
[00001000] free env 00001000
1001 got 10 from 1000
[00001001] exiting gracefully
[00001001] free env 00001001
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

除了提到的`forktree`和`pingpong`两个测试中我们把fork改成sfork，我们还写了一个简单的用户程序测试`user/sforktest.c`

```
#include <inc/lib.h>

int shared_value = 0;

void
umain(int argc, char **argv)
{
	envid_t who;

	if ((who = sfork()) != 0) {
		cprintf("read shared_value=%d from %x\n", shared_value, sys_getenvid());
		shared_value = 1;
		ipc_send(who, 0, 0, 0);
	} else {
		uint32_t i = ipc_recv(&who, 0, 0);
		cprintf("read shared_value=%d from %x\n", shared_value, sys_getenvid());
	}

}
```

这里我们先在父进程中读取shared_value的值，然后修改后给子进程发信号，让它来读，发现读出来的是修改后的值，可以说明我们成功share了这部分内存

测试结果如下：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-sforktest
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc[USER] user/sforktest.c
+ ld obj/user/sforktest
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] new env 00001001
read shared_value=0 from 1000
[00001000] exiting gracefully
[00001000] free env 00001000
read shared_value=1 from 1001
[00001001] exiting gracefully
[00001001] free env 00001001
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

可以看到确实读取出了修改后的值

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

## Part A: Multiprocessor Support and Cooperative Multitasking

### Multiprocessor Support

这一部分是要支持SMP多处理器模型，每个CPU都有平等的权限访问所以的系统资源，但有一个bootstrap processor(BSP)负责启动其它所有的application processor(AP)。每个CPU都有一个对应的LAPIC负责传送中断信号，同时提供对应CPU的唯一标识符。处理器通过MMIO机制来访问对应LAPIC，它位于物理地址0xfe000000处，虚拟地址MMIOBASE=0xef800000开始的4M字节hole中

#### Exercise 1

这个exercise是要完成一个映射函数，注意将size向上取整然后直接利用boot_map_region函数即可

```
void *
mmio_map_region(physaddr_t pa, size_t size)
{
	static uintptr_t base = MMIOBASE;
	if (size == 0)
		return (void *)base;
	size = ROUNDUP(size, PGSIZE);
	if (size > MMIOLIM - base)
		panic("mmio_map_region: memory overflow\n");
	boot_map_region(kern_pgdir, base, size, pa, PTE_PCD|PTE_PWT|PTE_W);
	void *ret = (void *)base;
	base += size;
	return ret;
}
```

#### Application Processor Bootstrap

首先BSP需要收集关于多处理器的信息，包括CPU总数、APIC ID以及LAPIC的MMIO地址，mp_init()通过读取BIOS部分的内存中存放的MP configuration table来获取这些信息。boot_aps()驱动所有AP启动过程，将AP的entry code复制到MPENTRY_PADDR的位置，然后一个个启动，通过向每一个发送STARTUP IPI以及entry_code的起始地址，等待AP启动调用mp_main以后设置CPU_STARTED的flag后再启动下一个。

#### Exercise 2

需要设置一页为已用使得不会加到空闲链表中，只需要修改

```
if ((i == 0) || (i >= IOPHYSMEM / PGSIZE && i < kernel_end))
```

为

```
if ((i == 0) || (i == MPENTRY_PADDR / PGSIZE) ||
			(i >= IOPHYSMEM / PGSIZE && i < kernel_end)) 
```

即可

#### Question 1

**Compare `kern/mpentry.S` side by side with `boot/boot.S`. Bearing in mind that `kern/mpentry.S` is compiled and linked to run above `KERNBASE` just like everything else in the kernel, what is the purpose of macro `MPBOOTPHYS`? Why is it necessary in `kern/mpentry.S` but not in `boot/boot.S`? In other words, what could go wrong if it were omitted in `kern/mpentry.S`?**

区别：mpentry.S与boot.S相比，少了A20启用，在每个符号的位置都用了MPBOOTPHYS宏，设置好栈后调用了mp_main()而不是bootmain()。

MPBOOTPHYS是为了计算每个利用绝对地址表示的符号实际物理地址的位置，因为mpentry.S是和kernel一起链接在高地址处的，因此符号的值实际上都是高地址的值，虽然将这部分代码拷贝进了MPENTRY_PADDR开始的低物理地址处，但符号的值还是高地址位置的值，因此需要对这些符号的值进行调整。而在boot.S中，加载地址和链接地址都在低地址处，因此符号的值无需变化。如果mpentry.S中不这么处理，就会在刚开始启动的实模式状态下访问高地址处导致出错。

#### Special Question 1

`mpentry.S`中有这样一个问题

```
	# Call mp_main().  (Exercise for the reader: why the indirect call?)
	movl    $mp_main, %eax
	call    *%eax
```

原因和问题1非常类似，因为如果直接跳转的话，生成的汇编代码是相对地址，在拷贝到MPENTRY_PADDR后，相对地址发生变化，因此无法成功跳转，而这样先复制到寄存器中，则填写的是绝对地址

#### Per-CPU State and Initialization

每个CPU有一些各自独立的状态信息：

- 内核栈：防止不同CPU同时陷入内核互相干扰
- TSS：JOS指利用TSS确定当前CPU内核栈的位置
- 环境指针：当前CPU上运行的环境
- 系统寄存器：当前CPU的寄存器

#### Exercise 3

和之前相同的方法映射就好了，这次只是多个循环

```
	for (int i = 0; i < NCPU; i++) {
		uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
		boot_map_region(kern_pgdir, kstacktop_i - KSTKSIZE, KSTKSIZE, 
			PADDR(&percpu_kstacks[i]), PTE_W);
	}
```

#### Exercise 4

lab的注释非常详细，按照步骤一步步完成即可

```
void
trap_init_percpu(void)
{
	uint32_t i = thiscpu->cpu_id;
	thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
	thiscpu->cpu_ts.ts_ss0 = GD_KD;
	thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);
	gdt[(GD_TSS0 >> 3) + i]  = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
						sizeof(struct Taskstate) - 1, 0);
	gdt[(GD_TSS0 >> 3) + i].sd_s = 0;
	ltr(GD_TSS0 + (i << 3));
	lidt(&idt_pd);
}
```

#### Locking

所有CPU共用一个大的kernel lock，保证每个时间点最多一个CPU在内核态执行

#### Exercise 5

直接按照handout写的去做就行了

#### Question 2

**It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.**

因为比如当多个CPU上的用户态程序同时出现异常时，硬件会自动将Trapframe中下面许多部分自动压栈，而且我们在lab3中trapentry.S写的代码会将剩余部分也压栈，在进入trap()函数之前将整个Trapframe压进去：

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

这时候如果只有一个内核栈，而又尚未进入trap函数，意味着还没有上锁，那么这些自动压栈的指令有可能会交错执行，发生混乱。

### Round-Robin Scheduling

这里我们要实现一个非抢占的round robin，每次schedule时，kernel从env数组中循环查找下一个处于ENV_RUNNABLE状态的进程，如果不存在则看原来的进程是否处于ENV_RUNNING，如果是则继续执行，否则调用sched_halt()停止CPU。

#### Exercise 6

sched_yield函数按照注释在数组里循环搜索即可

```
void
sched_yield(void)
{
	struct Env *idle;
	uint32_t id = curenv ? ENVX(curenv->env_id) : 0;
	for (uint32_t i = 0; i < NENV; i++) {
		if (envs[(id + i) % NENV].env_status == ENV_RUNNABLE)
			env_run(&envs[(id + i) % NENV]);
	}
	
	if (curenv && curenv->env_status == ENV_RUNNING) {
		env_run(curenv);
	}
	sched_halt();
}
```

syscall()中加上：

```
	case SYS_yield:
		sys_yield();
		return 0;
```

然后init.c中修改如下：

```
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
#endif // TEST*

```

来调用用户程序yield.c

结果如下：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make qemu
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00000000] new env 00001001
[00000000] new env 00001002
Hello, I am environment 00001000.
Hello, I am environment 00001001.
Hello, I am environment 00001002.
Back in environment 00001000, iteration 0.
Back in environment 00001001, iteration 0.
Back in environment 00001002, iteration 0.
Back in environment 00001000, iteration 1.
Back in environment 00001001, iteration 1.
Back in environment 00001002, iteration 1.
Back in environment 00001000, iteration 2.
Back in environment 00001001, iteration 2.
Back in environment 00001002, iteration 2.
Back in environment 00001000, iteration 3.
Back in environment 00001001, iteration 3.
Back in environment 00001002, iteration 3.
Back in environment 00001000, iteration 4.
All done in environment 00001000.
[00001000] exiting gracefully
[00001000] free env 00001000
Back in environment 00001001, iteration 4.
All done in environment 00001001.
[00001001] exiting gracefully
[00001001] free env 00001001
Back in environment 00001002, iteration 4.
All done in environment 00001002.
[00001002] exiting gracefully
[00001002] free env 00001002
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
llgyc@ubuntu:~/Desktop/6.828/lab$ make qemu CPUS=2
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 2 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 2 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
[00000000] new env 00001000
[00000000] new env 00001001
[00000000] new env 00001002
Hello, I am environment 00001000.
Hello, I am environment 00001001.
Back in environment 00001000, iteration 0.
Hello, I am environment 00001002.
Back in environment 00001001, iteration 0.
Back in environment 00001000, iteration 1.
Back in environment 00001002, iteration 0.
Back in environment 00001001, iteration 1.
Back in environment 00001000, iteration 2.
Back in environment 00001002, iteration 1.
Back in environment 00001001, iteration 2.
Back in environment 00001000, iteration 3.
Back in environment 00001002, iteration 2.
Back in environment 00001001, iteration 3.
Back in environment 00001000, iteration 4.
Back in environment 00001002, iteration 3.
All done in environment 00001000.
[00001000] exiting gracefully
[00001000] free env 00001000
Back in environment 00001001, iteration 4.
Back in environment 00001002, iteration 4.
All done in environment 00001001.
All done in environment 00001002.
[00001001] exiting gracefully
[00001001] free env 00001001
[00001002] exiting gracefully
[00001002] free env 00001002
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

#### Question 3

**In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch?**

传递来的e地址都位于KERNBASE之上，而这上面的地址在内核页表被映射到-KERNBASE处，同时这个页表被复制到了各个进程的页表映射之中，因此在切换之后依然能够正确访问到这些值。

#### Question 4

**Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?**

只有保存了才能恢复当时进程的上下文，能够继续执行，这部分信息被保存在Trapframe中，在陷入内核时，硬件以及trapentry.S中的指令保存了相关的信息，然后在trap.c中的trap()函数中下面的代码中被保存到对应的env对象中

```
curenv->env_tf = *tf;
```

然后在调度新进程时，通过env_run()中的

```
env_pop_tf(&e->env_tf);
```

恢复现场

### System Calls for Environment Creation

这一部分是要实现一些primitive的函数，使得指和能更好地实现fork以及其它环境环境创建功能

#### Exercise 7

几个函数的实现细节都在提供的注释中写的非常详细，只要注意按照要求完成即可

```
static envid_t
sys_exofork(void)
{
	struct Env *e;
	int ret = env_alloc(&e, sys_getenvid());
	if (ret < 0)
		return ret;
	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = curenv->env_tf;
	e->env_tf.tf_regs.reg_eax = 0;
	return e->env_id;
}
static int
sys_env_set_status(envid_t envid, int status)
{
	struct Env *e;
	int ret = envid2env(envid, &e, 1);
	if (ret < 0)
		return ret;
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;
	e->env_status = status;
	return 0;
}
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	struct Env *e;
	int ret = envid2env(envid, &e, 1);
	if (ret < 0)
		return ret;
	if ((uint32_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
		return -E_INVAL;
	if (!(perm & PTE_U || perm & PTE_P))
		return -E_INVAL;
	if (perm & ~PTE_SYSCALL)
		return -E_INVAL;
	struct PageInfo *pp = page_alloc(ALLOC_ZERO);
	if (pp == NULL)
		return -E_NO_MEM;
	ret = page_insert(e->env_pgdir, pp, va, perm);
	if (ret < 0) {
		page_free(pp);
		return ret;
	}
	return 0;
}
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	struct Env *srce, *dste;
	int ret;
	if ((ret = envid2env(srcenvid, &srce, 1)) < 0)
		return ret;
	if ((ret = envid2env(dstenvid, &dste, 1)) < 0)
		return ret;
	if ((uint32_t)srcva >= UTOP || (uint32_t)dstva >= UTOP 
		|| srcva != ROUNDUP(srcva, PGSIZE)
		|| dstva != ROUNDUP(dstva, PGSIZE))
		return -E_INVAL;
	pte_t *pte;
	struct PageInfo *pp = page_lookup(srce->env_pgdir, srcva, &pte);
	if (pp == NULL)
		return -E_INVAL;
	if (!(perm & PTE_U || perm & PTE_P))
		return -E_INVAL;
	if (perm & ~PTE_SYSCALL)
		return -E_INVAL;
	if (perm & PTE_W && !(*pte & PTE_W))
		return -E_INVAL;
	ret = page_insert(dste->env_pgdir, pp, dstva, perm);
	return ret;
}
static int
sys_page_unmap(envid_t envid, void *va)
{
	struct Env *e;
	int ret = envid2env(envid, &e, 1);
	if (ret < 0)
		return ret;
	if ((uint32_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
		return -E_INVAL;
	page_remove(e->env_pgdir, va);
	return 0;
}
```

然后还需要在syscall()函数中加上：

```
	case SYS_page_alloc:
		return sys_page_alloc(a1, (void *)a2, a3); 
	case SYS_page_map:
		return sys_page_map(a1, (void *)a2, a3, (void *)a4, a5);
	case SYS_page_unmap:
		return sys_page_unmap(a1, (void *)a2);
	case SYS_exofork:
		return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status(a1, a2);
```

同时修改init.c中创建dumbfork用户程序

```
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	ENV_CREATE(user_dumbfork, ENV_TYPE_USER);
#endif // TEST*
```

运行结果如下：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make qemu
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] new env 00001001
0: I am the parent!
0: I am the child!
1: I am the parent!
1: I am the child!
2: I am the parent!
2: I am the child!
3: I am the parent!
3: I am the child!
4: I am the parent!
4: I am the child!
5: I am the parent!
5: I am the child!
6: I am the parent!
6: I am the child!
7: I am the parent!
7: I am the child!
8: I am the parent!
8: I am the child!
9: I am the parent!
9: I am the child!
[00001000] exiting gracefully
[00001000] free env 00001000
10: I am the child!
11: I am the child!
12: I am the child!
13: I am the child!
14: I am the child!
15: I am the child!
16: I am the child!
17: I am the child!
18: I am the child!
19: I am the child!
[00001001] exiting gracefully
[00001001] free env 00001001
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

#### Special Question 2

注意到`lib/syscall.c`中没有`sys_exofork()`的定义，它是由`inc/lib.h`中的定义inline进入程序的，注释上有一个问题：

```
// This must be inlined.  Exercise for reader: why?
static inline envid_t __attribute__((always_inline))
sys_exofork(void)
{
	envid_t ret;
	asm volatile("int %2"
		     : "=a" (ret)
		     : "a" (SYS_exofork), "i" (T_SYSCALL));
	return ret;
}
```

因为如果不是inline的，可能导致会发生因调用函数导致的压栈过程，从而使得register state不是调用sys_exofork call时的register state，语义发生变化。

## Part B: Copy-on-Write Fork

在fork的时候如果完整复制空间是一个很大的overhead，Unix使用了Copy-On-Write机制，只有当进程开始写的时候才新创建空间复制过去使用，否则处于共享可读的状态。而且使用COW使得程序在这个基础上还可以实现自己想要的fork语义

### User-level page fault handling

缺页异常有多种用途，栈空间缺页导致栈按需增长，BSS区域缺页导致分配一个新页并且清零，代码段缺页导致会读取二进制文件并映射，等等。

#### Setting the Page Fault Handler

为了处理page fault，用户态进程可以通过系统调用sys_env_set_pgfault_upcall()来register一个handler

#### Exercise 8

直接修改环境中的env_pgfault_upcall成员即可

```
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	struct Env *e;
	int ret = envid2env(envid, &e, 1);
	if (ret < 0)
		return ret;
	e->env_pgfault_upcall = func;
	return 0;
}
```

#### Normal and Exception Stacks in User Environments

平常执行时，用户进程使用用户栈，栈底的初始位置在USTACKTOP，当page fault发生时，内核会使用用户设置的handler重启用户进程，跑在用户异常栈上，它的栈底初始位于UXSTACKTOP处，运行结束后回到出错的位置并重新使用用户栈。

#### Invoking the User Page Fault Handler

出现fault时的状态叫做trap-time state。如果没有注册page fault handler，那么JOS会结束用户进程，否则像上文中提到的会在异常栈上摆上一个trap frame，用来提示是什么错误以及恢复现场。如果在handler里继续出错则在异常栈中继续压栈而非从UXSTACKTOP开始压栈。

#### Exercise 9

page_fault_handler()中加入以下代码：

```
	if (curenv->env_pgfault_upcall) {
		struct UTrapframe *utf;
		if (ROUNDDOWN(tf->tf_esp, PGSIZE) == UXSTACKTOP - PGSIZE)
			utf = (struct UTrapframe *)
				(tf->tf_esp 
				- sizeof(struct UTrapframe) 
				- sizeof(uint32_t));
		else
			utf = (struct UTrapframe *)UXSTACKTOP;
		user_mem_assert(curenv, (void *)utf, 1, PTE_W);
		utf->utf_fault_va = fault_va;
		utf->utf_err = tf->tf_err;
		utf->utf_regs = tf->tf_regs;
		utf->utf_eip = tf->tf_eip;
		utf->utf_eflags = tf->tf_eflags;
		utf->utf_esp = tf->tf_esp;
		curenv->env_tf.tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
		curenv->env_tf.tf_esp = (uintptr_t)utf;
		env_run(curenv);
	}
```

user_mem_assert中长度设为1即可判断是否可写/有映射/溢出，因为异常栈是整页一起映射的

需要注意的是如果发生了嵌套page fault，用户异常栈栈顶需要预留4字节用于恢复的时候方便在这里填写返回地址，而如果来自用户栈则无需特殊处理

**What happens if the user environment runs out of space on the exception stack?**

如果用户环境让异常栈溢出了，按照注释的描述应该直接destroy环境

#### User-mode Page Fault Entrypoint

#### Exercise 10

按照注释的提示，大致依照UTrapframe中的顺序恢复，关键点在于返回出错位置时的方法是先加载目标esp，把eip压在返回环境使用的栈的栈顶，然后通过ret指令同时完成恢复eip和弹栈恢复esp

```
.text
.globl _pgfault_upcall
_pgfault_upcall:
	// Call the C page fault handler.
	pushl %esp			// function argument: pointer to UTF
	movl _pgfault_handler, %eax
	call *%eax
	addl $4, %esp			// pop function argument
	
	subl $0x4, 0x30(%esp)
	movl 0x30(%esp), %eax
	movl 0x28(%esp), %edx
	movl %edx, (%eax)

	add $0x8, %esp
	popal

	add $0x4, %esp
	popfl

	popl %esp

	ret
```

#### Exercise 11

第一次设置时，先分配异常栈的内存，然后再把wrapper function利用syscall设置好

```
	if (_pgfault_handler == 0) {
		if ((r = sys_page_alloc(0, (void *)(UXSTACKTOP - PGSIZE), PTE_U|PTE_P|PTE_W)) < 0)
			panic("set_pgfault_handler: %e", r);
		if ((r = sys_env_set_pgfault_upcall(0, _pgfault_upcall)) < 0)
			panic("set_pgfault_handler: %e", r);
		_pgfault_handler = handler;
	}
```

#### Testing

`user/faultread`的测试：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-faultread
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] user fault va 00000000 ip 00800039
TRAP frame at 0xf02b8000 from CPU 0
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x0000000e Page Fault
  cr2  0x00000000
  err  0x00000004 [user, read, not-present]
  eip  0x00800039
  cs   0x----001b
  flag 0x00000086
  esp  0xeebfdfc0
  ss   0x----0023
[00001000] free env 00001000
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

`user/faultdie`的测试：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-faultdie
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/trap.c
+ cc kern/syscall.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
i faulted at va deadbeef, err 6
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

`user/faultalloc`的测试：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-faultalloc
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
fault deadbeef
this string was faulted in at deadbeef
fault cafebffe
fault cafec000
this string was faulted in at cafebffe
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

`user/faultallocbad`的测试：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-faultallocbad
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] user_mem_check assertion failure for va deadbeef
[00001000] free env 00001000
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

#### Special Question 3

`user/faultalloc.c`里面有这样的一个问题：

```
// doesn't work because we sys_cputs instead of cprintf (exercise: why?)
```

同时也是handout中的提问

`user/faultalloc`和`user/faultallocbad`表现不同的原因是faultallocbad中使用的sys_cputs是系统调用，会检查参数是否合法，如果不合法直接结束该恶意程序，因此直接出错；而faultalloc里面使用的cprintf是用户态函数，因此当访问到未映射的地址时会触发page fault，然后调用预先设置好的handler新建映射，从而可以正常访问。

### Implementing Copy-on-Write Fork

现在可以实现使用COW机制的fork()函数了。fork需要把父进程的地址空间映射全部拷贝给子进程，当写发生时才有实际内容的复制。fork的控制流如下：

- 父进程先把pgfault()当作page fault handler
- 父进程调用sys_exofork()创建子进程
- 对于每个UTOP以下的可写或者COW页面，父进程调用duppage，把页面COW映射到子进程的地址空间中，然后再在自己地址空间中也修改为COW映射，标记PTE_COW**【这里的顺序很重要，因为如果先设置父进程COW，在设置子进程之前，父进程可能修改了对应的页（e.g. 为了传参压栈）导致触发page fault，复制到新的位置，而原来的页计数清零，子进程复制时则会指向父进程现在的新页上，那么就处于一个父进程可写，子进程COW的状态，语义是有问题的，因为这时候父进程修改会影响子进程的内容】**
- 特别地，异常栈需要在子进程中重新分配一个新页，而不是复制
- 父进程为子进程设置page fault entrypoint
- 父进程设置子进程为RUNNABLE状态

每次有环境向COW页面写时会触发page fault，异常处理的流程如下：

- 内核调用fork的pgfault handler
- pgfault检查是否是写触发的异常以及页是否标记了COW，否则panic
- pgfault分配一个新页，复制内容，设置正确的权限

#### Exercise 12

先是fork函数，由于子进程全部是COW，刚开始执行就会page fault，设置异常栈以及page fault handler都得父进程代为执行

```
envid_t
fork(void)
{	
	envid_t envid;
	uint8_t *addr;
	int r;
	extern void _pgfault_upcall(void);
	
	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	// We're the parent.
	for (addr = 0; addr < (uint8_t *)USTACKTOP; addr += PGSIZE)
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)
			&& (uvpt[PGNUM(addr)] & PTE_U))
			duppage(envid, PGNUM(addr));
	
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U|PTE_P|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: %e", r);
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	return envid;
}
```

duppage中，针对可写/COW以及其它情况分两类进行映射：

```
static int
duppage(envid_t envid, unsigned pn)
{
	int r, perm;
	void *addr;
	
	addr = (void *)(pn * PGSIZE);
	perm = uvpt[pn] & PTE_SYSCALL;
	if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) {
		perm &= ~PTE_W;
		perm |= PTE_COW;
		if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
			panic("sys_page_map: %e", r);
		if ((r = sys_page_map(0, addr, 0, addr, perm)) < 0)
			panic("sys_page_map: %e", r);		
	} else if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
		panic("sys_page_map: %e", r);
	return 0;
}
```

最后pgfault handler中完成实际的复制：

```
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	if (!((err & FEC_WR) && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault: not a COW page");
	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_U|PTE_W|PTE_P)) < 0)
		panic("pgfault: %e", r);
	memmove(PFTEMP, addr, PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, addr, PTE_U|PTE_W|PTE_P)) < 0)
		panic("pgfault: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("pgfault: %e", r);
}
```

结果如下：

```
llgyc@ubuntu:~/Desktop/6.828/lab$ make run-forktree
make[1]: Entering directory '/home/llgyc/Desktop/6.828/lab'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/llgyc/Desktop/6.828/lab'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
1000: I am ''
[00001000] new env 00001001
[00001000] new env 00001002
[00001000] exiting gracefully
[00001000] free env 00001000
1001: I am '0'
[00001001] new env 00002000
[00001001] new env 00001003
[00001001] exiting gracefully
[00001001] free env 00001001
2000: I am '00'
[00002000] new env 00002001
[00002000] new env 00001004
[00002000] exiting gracefully
[00002000] free env 00002000
2001: I am '000'
[00002001] exiting gracefully
[00002001] free env 00002001
1002: I am '1'
[00001002] new env 00003001
[00001002] new env 00003000
[00001002] exiting gracefully
[00001002] free env 00001002
3000: I am '11'
[00003000] new env 00002002
[00003000] new env 00001005
[00003000] exiting gracefully
[00003000] free env 00003000
3001: I am '10'
[00003001] new env 00004000
[00003001] new env 00001006
[00003001] exiting gracefully
[00003001] free env 00003001
4000: I am '100'
[00004000] exiting gracefully
[00004000] free env 00004000
2002: I am '110'
[00002002] exiting gracefully
[00002002] free env 00002002
1003: I am '01'
[00001003] new env 00003002
[00001003] new env 00005000
[00001003] exiting gracefully
[00001003] free env 00001003
5000: I am '011'
[00005000] exiting gracefully
[00005000] free env 00005000
3002: I am '010'
[00003002] exiting gracefully
[00003002] free env 00003002
1004: I am '001'
[00001004] exiting gracefully
[00001004] free env 00001004
1005: I am '111'
[00001005] exiting gracefully
[00001005] free env 00001005
1006: I am '101'
[00001006] exiting gracefully
[00001006] free env 00001006
No runnable environments in the system!
This line is for testing the color.
This line is for testing the color.
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

#### Special Question 4

`lib/fork.c`里面有这样一个问题：

```
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
```

理由其实和上面提到的一样，如果本来是COW，这一页也可能在判断是COW后到给子进程复制这一映射的过程中被写入，导致形成同时指向一页但父进程可写，子进程COW的状态，造成错误。

## Part C: Preemptive Multitasking and Inter-Process communication (IPC)

### Clock Interrupts and Preemption

此时的CPU是非抢占的，如果用户执行了像`user/spin`这样的程序，就会死循环并且一直不归还对CPU的控制。因此我们必须支持来自时钟的外部中断。

#### Interrupt discipline

用IRQ来指示外部中断，一共有16个值，0~15.`picirq.c`中的代码将IRQs 0-15对应至IRQ_OFFSET到IRQ_OFFSET+15的IDT表项。

在JOS中，内核态时外部设备中断总是关闭的，用户态启用。外部中断由EFLAGS上的FL_IF标志位控制，set的时候表示启用中断。

#### Exercise 13

在trapentry.S中加入

```
TRAPHANDLER_NOEC(HANDLER32, IRQ_OFFSET+IRQ_TIMER);
TRAPHANDLER_NOEC(HANDLER33, IRQ_OFFSET+IRQ_KBD);
TRAPHANDLER_NOEC(HANDLER36, IRQ_OFFSET+IRQ_SERIAL);
TRAPHANDLER_NOEC(HANDLER39, IRQ_OFFSET+IRQ_SPURIOUS);
TRAPHANDLER_NOEC(HANDLER46, IRQ_OFFSET+IRQ_IDE);
TRAPHANDLER_NOEC(HANDLER51, IRQ_OFFSET+IRQ_ERROR);
```

trap.c中加入

```
	SETGATE(idt[32], 0, GD_KT, HANDLER32, 0);
	SETGATE(idt[33], 0, GD_KT, HANDLER33, 0);
	SETGATE(idt[36], 0, GD_KT, HANDLER36, 0);
	SETGATE(idt[39], 0, GD_KT, HANDLER39, 0);
	SETGATE(idt[46], 0, GD_KT, HANDLER46, 0);
	SETGATE(idt[51], 0, GD_KT, HANDLER51, 0);	
```

env_alloc()中设置用户态进程的eflags中的FL_IF项都为1

```
e->env_tf.tf_eflags = FL_IF;
```

以及去掉sched_halt()中sti前的注释即可

#### Handling Clock Interrupts

lapic_init和pic_init函数设置好了时钟并会产生中断

#### Exercise 14

只需要增加以下内容即可：

```
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
		lapic_eoi();
		sched_yield();
		return;
	}
```

### Inter-Process communication (IPC)

操作系统给环境提供一种独占机器的抽象，但也允许进程与其它进程之间进行通信

#### IPC in JOS

我们要实现两个系统调用sys_ipc_recv和sys_ipc_try_send以及两个库wrapper ipc_recv和ipc_send.

用户环境可以利用IPC发送的消息包括两部分，一个32bit的值和一个可选的单页映射。允许传递映射使得能够更高效地传输更多数据，并且使得设置共享内存变得更容易

#### Sending and Receiving Messages

为了收到一条消息，环境调用sys_ipc_recv，系统不再调度改进程直到收到一条消息。当一个进程在等待收消息时，任何其它进程都可以向它发消息，没有permission checking。IPC调用被仔细设计使得它“安全”：经常不能够通过只向另一个进程发消息就使得它malfunction

为了发送一个值，环境用接受方id以及要发送的值作为参数调用sys_ipc_try_send，如果对应接受方此时确实在收消息，那么就把消息发过去并返回0，否则返回-E_IPC_NOT_RECV表明对面不在等一个值。

库函数ipc_recv帮忙调用sys_ipc_recv，并在当前环境的Env结构中查找对应值的有关信息。

类似地，ipc_send会帮助仿佛调用sys_ipc_try_send直到发送成功。

#### Transferring Pages

当环境调用sys_ipc_recv并且提供一个dstva作为参数，就说明在等待一个页映射，如果发送方发了一个页，那么它会被映射到接受方的dstva地址处。如果接受方在dstva已经有映射，则先取消映射。

当一个环境调用sys_ipc_try_send并提供一个srcva作为参数，表明发送方想要把现在映射在srcva的页发送给接受方，并且权限是perm。成功的IPC后，发送方映射不变，接受方得到了该物理页到dstva的映射，因此这一页能够被发送方和接受方共享。

如果两者中有一者没有表明需要传输页，那么就不会发生页的传输。任意IPC后，内核把接受方env结构中env_ipc_perm的值设置为页的权限（如果有页接受），否则设为0（没有页接受）。

#### Implementing IPC

#### Exercise 15

按照注释写好所有特判即可，没有其它特殊情况

```
static int
sys_ipc_recv(void *dstva)
{
	if ((uint32_t)dstva < UTOP && dstva != ROUNDUP(dstva, PGSIZE))
		return -E_INVAL;
	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;
	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
	return 0;
}

static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	struct Env *dste;
	struct PageInfo *pp;
	pte_t *p_pte;
	int r;
	bool with_page = 0;
	if ((r = envid2env(envid, &dste, 0)) < 0)
		return r;
	if (!dste->env_ipc_recving)
		return -E_IPC_NOT_RECV;
	if ((uint32_t)srcva < UTOP) {
		if (srcva != ROUNDUP(srcva, PGSIZE))
			return -E_INVAL;
		if (!((perm & PTE_U) && (perm & PTE_P)))
			return -E_INVAL;
		if (perm & ~PTE_SYSCALL)
			return -E_INVAL;
		pp = page_lookup(curenv->env_pgdir, srcva, &p_pte);
		if (!((*p_pte & PTE_P) && (*p_pte & PTE_U)))
			return -E_INVAL;
		if ((perm & PTE_W) && (!(*p_pte & PTE_W)))
			return -E_INVAL;
		if ((uint32_t)dste->env_ipc_dstva < UTOP) {
			r = page_insert(dste->env_pgdir, pp, dste->env_ipc_dstva, perm);
			if (r < 0)
				return r;
			with_page = 1;
		}
	}
	dste->env_ipc_recving = 0;
	dste->env_ipc_from = curenv->env_id;
	dste->env_ipc_value = value;
	dste->env_ipc_perm = with_page ? perm : 0;
	dste->env_status = ENV_RUNNABLE;
	dste->env_tf.tf_regs.reg_eax = 0;
	return 0;
}

int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	int r;
	pg = pg ? pg : (void *)UTOP;
	if ((r = sys_ipc_recv(pg)) < 0) {
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
		return r;
	}
	if (from_env_store)
		*from_env_store = thisenv->env_ipc_from;
	if (perm_store)
		*perm_store = thisenv->env_ipc_perm;
	return thisenv->env_ipc_value;
}

void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	int r;
	pg = pg ? pg : (void *)UTOP;
	while ((r = sys_ipc_try_send(to_env, val, pg, perm)) < 0) {
		if (r != -E_IPC_NOT_RECV)
			panic("sys_ipc_try_send: %e", r);
		sys_yield();
	}
}

```

最终结果如下：

```
dumbfork: OK (2.5s) 
Part A score: 5/5

faultread: OK (2.4s) 
faultwrite: OK (2.4s) 
faultdie: OK (2.4s) 
faultregs: OK (2.1s) 
faultalloc: OK (3.0s) 
faultallocbad: OK (1.9s) 
faultnostack: OK (2.7s) 
faultbadhandler: OK (3.0s) 
faultevilhandler: OK (3.0s) 
forktree: OK (3.0s) 
Part B score: 50/50

spin: OK (2.7s) 
stresssched: OK (3.5s) 
sendpage: OK (2.8s) 
    (Old jos.out.sendpage failure log removed)
pingpong: OK (3.0s) 
    (Old jos.out.pingpong failure log removed)
primes: OK (5.1s) 
    (Old jos.out.primes failure log removed)
Part C score: 25/25

Score: 80/80
```

## This Complete The Lab.

