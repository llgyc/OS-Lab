# Report for lab2, Yuchen Gu

完成的challenge如下：

```
Challenge! Extend the JOS kernel monitor with commands to:

- Display in a useful and easy-to-read format all of the physical page mappings (or lack thereof) that apply to a particular range of virtual/linear addresses in the currently active address space. For example, you might enter 'showmappings 0x3000 0x5000' to display the physical page mappings and corresponding permission bits that apply to the pages at virtual addresses 0x3000, 0x4000, and 0x5000.
- Explicitly set, clear, or change the permissions of any mapping in the current address space.
- Dump the contents of a range of memory given either a virtual or physical address range. Be sure the dump code behaves correctly when the range extends across page boundaries!
- Do anything else that you think might be useful later for debugging the kernel. 
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

## Part 1: Physical Page Management

一上来就是Exercise 1

#### Exercise 1

第一个是在page_free_list建立之前使用的boot_alloc函数，其中0x400000代表目前进行了映射的4MB空间，超出了就会报错，按照注释要求完成即可。

```
static void *
boot_alloc(uint32_t n)
{
	static char *nextfree;
	char *result;
	
	if (!nextfree) {
		extern char end[];
		nextfree = ROUNDUP((char *) end, PGSIZE);
	}

	uint32_t max_mem = 0x400000 - (uint32_t) PADDR(nextfree);
	if (n > max_mem)
		panic("boot_alloc: Out of Memory.\n");
	result = nextfree;
	nextfree = ROUNDUP(nextfree + n, PGSIZE);

	return result;
}
```

接下来给页目录分配了空间并初始化，在其中填写了一项指向自己的页表项

```
	kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
	memset(kern_pgdir, 0, PGSIZE);
	kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir) | PTE_U | PTE_P;
```

下一个要完成的部分是分配空间记录每个页的空闲、引用状态：

```
	pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));
	memset(pages, 0, npages * sizeof(struct PageInfo));
```

然后是page_init中对刚分配的page数组的设置：

```
void
page_init(void)
{
	size_t i;
	size_t kernel_end = PADDR(boot_alloc(0)) / PGSIZE; //内核数据结构末尾
	for (i = 0; i < npages; i++) {
		if ((i == 0) || (i >= IOPHYSMEM / PGSIZE && i < kernel_end)) { //IO hole直到内核结束的部分
			pages[i].pp_ref = 0;
			pages[i].pp_link = NULL;
		} else {
			pages[i].pp_ref = 0;
			pages[i].pp_link = page_free_list;
			page_free_list = &pages[i];
		}
	}
}
```

根据注释的提示知道除了第0页和IOPHYSMEM直到kernel中刚刚分配的page数组的部分是已用的，剩下的加入page_free_list中，这里pp_ref都设为0是因为memlayout.h中写着：

```
struct PageInfo {
	// Next page on the free list.
	struct PageInfo *pp_link;

	// pp_ref is the count of pointers (usually in page table entries)
	// to this page, for pages allocated using page_alloc.
	// Pages allocated at boot time using pmap.c's
	// boot_alloc do not have valid reference count fields.

	uint16_t pp_ref;
};
```

而这时候除了页表的UVPT项还未建立，因此我们认为还没有指向这些页的pointer。通过后面的reference counting一节中也可以知道，这一部分暂时是不用管pp_ref的。

剩下两个函数是page_alloc和page_free，按照注释完成即可：

```
struct PageInfo *
page_alloc(int alloc_flags)
{
	if (page_free_list == NULL) return NULL;
	
	struct PageInfo *pp = page_free_list;
	page_free_list = pp->pp_link;
	pp->pp_link = NULL;
	
	if (alloc_flags & ALLOC_ZERO) 
		memset(page2kva(pp), 0, PGSIZE);
	
	return pp;
}

void
page_free(struct PageInfo *pp)
{
	if (pp->pp_ref)
		panic("page_free: pp_ref is nonzero (page is still in use)\n");
	if (pp->pp_link)
		panic("page_free: pp_link is not NULL (double-free bugs)\n");
	pp->pp_link = page_free_list;
	page_free_list = pp;
}
```

## Part 2: Virtual Memory

### Virtual, Linear, and Physical Addresses

#### Exercise 2

这是一个reading task. 5.2节主要讲的是二级页表讲linear address翻译成physical address的过程，以及页表项的内容。需要注意的一个是页表项中存放的是物理地址，另一个是关于后面的标志位手册写的不完全对应JOS，需要在`inc/mmu.h`中根据下面的内容来理解：

```
// Page table/directory entry flags.
#define PTE_P		0x001	// Present
#define PTE_W		0x002	// Writeable
#define PTE_U		0x004	// User
#define PTE_PWT		0x008	// Write-Through
#define PTE_PCD		0x010	// Cache-Disable
#define PTE_A		0x020	// Accessed
#define PTE_D		0x040	// Dirty
#define PTE_PS		0x080	// Page Size
#define PTE_G		0x100	// Global
```

6.4节讲的是页表项的U/S位，R/W位和特权级的关系。U/S=1时均可以寻址，U/S=0时只有特权级（CPL为0,1,2）时可以寻址。处于特权级时，所有页都可以读写，用户级时只有当U/S=1并且R/W=1时才可读写（这一点是和lab2文档中描述有所不同的，文档提到特权级和用户级读写都受R/W控制，问了助教以后直到还有一个CR0中的WP位控制，当WP=1时，R/W会影响特权级下读写，WP=0时无影响，刚好对应两种情况，而JOS代码中后面有设置CR0.WP=1，因此是相吻合的）

#### Exercise 3

这一问是要使用QEMU的xp命令查看物理地址的内容，我们在开启分页以后，查看一下内核被拷进的位置：

```
(qemu) xp/4x 0x00100000
0000000000100000: 0x1badb002 0x00000000 0xe4524ffe 0x7205c766
```

对应gdb中的结果是：

```
(gdb) x/4x 0x00100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
(gdb) x/4x 0xf0100000
0xf0100000 <_start+4026531828>:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
```

可以发现是一致的

#### Question 1

类型是`uintptr_t`，因为在kernel中已经进入保护模式，所以只能访问到虚拟地址

### Reference counting

这一节是在说pp_ref记录的是每个物理页有多少处于UTOP之下的虚拟地址被映射到该页上

### Page Table Management

#### Exercise 4

这个练习需要完成一系列管理页表的函数，依次按照注释完成即可

第一个是在利用pgdir对应的页目录找到va对应的页表项，create来控制是否在不存在时创建页表，这一问需要注意的是页表项中存放的是物理地址，因此需要多次转换。而且由于查看权限时，会同时查看页目录和页表中的，因此只需要设置最宽的权限即可，也就是U/S=1,R/W=1

```
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	// Fill this function in
	pde_t *p_pde = pgdir + PDX(va);
	pte_t *p_pte;
	if ((*p_pde) & PTE_P) {
		p_pte = (pte_t *)KADDR(PTE_ADDR(*p_pde)) + PTX(va);
	} else {
		if (create) {
			struct PageInfo *pp = page_alloc(ALLOC_ZERO);
			if (pp == NULL)
				return NULL;
			pp->pp_ref++;
			p_pte = (pte_t *)page2kva(pp) + PTX(va);
			*p_pde = page2pa(pp) | PTE_P | PTE_W | PTE_U;
		} else 
			return NULL;
	}
	return p_pte;
}
```

第二个函数是讲一块虚拟地址映射到一块物理地址上，并且带有perm的权限位，一页一页利用pgdir_walk映射即可

```
static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	size_t i;
	for (i = 0; i < size; i += PGSIZE) {
		pte_t *pte = pgdir_walk(pgdir, (void *)va, 1);
		*pte = pa | perm | PTE_P;		
		va += PGSIZE, pa += PGSIZE;
	}
}
```

第三个是在给定的页目录中找到虚拟地址va对应的物理页返回，并在需要时同时返回页表项，同样利用pgdir_walk以及页表项中物理地址和pa2page找到物理页返回

```
struct PageInfo *
page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t *p_pte = pgdir_walk(pgdir, va, 0);
	if (p_pte == NULL)
		return NULL;
	if (pte_store)
		*pte_store = p_pte;
	if (*p_pte & PTE_P) 
		return pa2page(PTE_ADDR(*p_pte));
	return NULL;
}
```

第四个函数是删除虚拟地址va的映射，直接找到对应的页表项和物理页修改即可

```
void
page_remove(pde_t *pgdir, void *va)
{
	pte_t *p_pte;
	struct PageInfo *pp = page_lookup(pgdir, va, &p_pte);
	if (pp == NULL)
		return;
	page_decref(pp);
	*p_pte = 0;
	tlb_invalidate(pgdir, va);
}
```

最后一个函数是增加一个映射到页表中去，需要注意的是va原来的映射可能就是同一个物理页，这时候如果先page_remove里面会调用page_free函数从而导致物理页被放入空闲页列表中导致出错，因此需要先加pp_ref然后再调用page_remove

```
int
page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
{
	pte_t *p_pte = pgdir_walk(pgdir, va, 1);
	if (p_pte == NULL)
		return -E_NO_MEM;
	pp->pp_ref++;
	if (*p_pte & PTE_P) 	
		page_remove(pgdir, va);
	*p_pte = page2pa(pp) | perm | PTE_P;	
	return 0;
}
```

## Part 3: Kernel Address Space

### Permissions and Fault Isolation

这一段讲的是如何映射，以及相应的权限，关于这部分内容memlayout.h有更加直观的图解释：

```
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xef400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xef000000
 *                     |           RO ENVS            | R-/R-  PTSIZE
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 *
```

### Initializing the Kernel Address Space

#### Exercise 5

这一问就是要利用之前写的boot_map_region函数和上一小节的内存映射图来完成三部分映射：

```
	boot_map_region(kern_pgdir, UPAGES, PTSIZE, PADDR(pages), PTE_U);
	boot_map_region(kern_pgdir, KSTACKTOP-KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);
	boot_map_region(kern_pgdir, KERNBASE, 0x10000000, 0, PTE_W);
```

第一行把UPAGES开始的PTSIZE个字节映射到pages数组上

第二行是把KSTACKTOP往下KSTKSIZE映射到之前使用过的bootstack上作为内核的栈

第三行是把之前一直想映射的高256M虚拟地址映射到最低256M的物理地址上去

#### Question 2

| Entry | Base Virtual Address | Points to (logically)                    |
| ----- | -------------------- | ---------------------------------------- |
| 1023  | 0xFFC00000           | Page table for top 4MB of phys memory    |
| ...   | ...                  | ...                                      |
| 960   | 0xF0000000           | Page table for [0MB, 4MB) of phys memory |
| 959   | 0xEFC00000           | CPU0's kernel stack and invalid memory   |
| 958   | 0xEF800000           | N/A                                      |
| 957   | 0xEF400000           | kernel page directory                    |
| 956   | 0xEF000000           | `pages` array                            |
| ...   | ...                  | N/A                                      |
| 0     | 0x00000000           | N/A                                      |

#### Question 3

根据页目录项和页表项中的U/S位和R/W位来设置每个页的访问权限，每次访问时硬件会根据当前的CPL来最终确定是否能读/写，对kernel内存对应的页表项标志应该是U/S=0，因此用户态程序无法读写。

#### Question 4

256M，因为目前页表映射的内容是将KERNBASE往上的空间映射到物理内存从0开始的地址处，而且KADDR计算地址的时候使用的是KERNBASE + pa，而KERNBASE之上的空间只有256M，因此我们认为os能够支持的最大物理内存是256M

（当然这个问题还可以换一种理解方式，就是如果我在页表中如果能增加更多映射，理论上限制物理内存的是RO PAGES部分的大小也就是PTSIZE里面能放下多少个struct PageInfo就是最大多少个物理页，也即$4096*1024/8*1024=2G$的内存。在这种情况下，需要保证内核数据结构例如页表等被分配到小于256M的物理地址即可。对于剩下的物理地址，依赖新的映射可以使用，这时候访问里面的内容只需要对着虚拟地址解引用即可，从这种角度来说也可以support 2G的内存）

#### Question 5

理解为256M时，overhead由

1）pages数组$256M/(4K/page)*(8B/page)=512K$ 

2）kern_pgdir 4K

3）刚还是只启用最少这256M的映射 $256M/(4M/PT)*(4K/PT)=256K$ 或者 启动的页表最多直到1024个页表$1024*4K=4M$

总共至少512K+4K+256K = 772K, 至多512K+4K+4M = 4612K的overhead

#### Question 6

看到entry.S中的代码，我们可以发现

```
	# Now paging is enabled, but we're still running at a low EIP
	# (why is this okay?).  Jump up above KERNBASE before entering
	# C code.
	mov	$relocated, %eax
	jmp	*%eax
```

这里利用jmp指令使得EIP跳到了KERNBASE之上，因为entrypgdir.c中设置了两个映射，把虚拟地址[KERNBASE,KERNBASE+4M)和[0,4M)都映射到了物理地址[0,4M)，因此开启分页后还是可以继续在低地址执行指令。这个跳转是必要的因为之后会加载完整的新页表进来，而新的页表中缺少va[0,4M)向pa[0,4M)的映射

#### Challenge

代码全部实现在`kern/monitor.c`中，基本上只需要按照要求模拟即可

**Display in a useful and easy-to-read format all of the physical page mappings (or lack thereof) that apply to a particular range of virtual/linear addresses in the currently active address space. For example, you might enter 'showmappings 0x3000 0x5000' to display the physical page mappings and corresponding permission bits that apply to the pages at virtual addresses 0x3000, 0x4000, and 0x5000.**

该问对应着showmappings函数

```
int
showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("Usage: showmappings START_VA END_VA\n");
		return 0;
	}
	uintptr_t start_va = strtol(argv[1], NULL, 0); 
	uintptr_t end_va = strtol(argv[2], NULL, 0);
	if (start_va % PGSIZE || end_va % PGSIZE) {
		cprintf("Error: START_VA and END_VA should be page-aligned\n");
		return 0;
	}
	if (start_va > end_va) {
		cprintf("Error: START_VA should be smaller than END_VA\n");
		return 0;
	}
	while (start_va <= end_va) {
		cprintf("VA: 0x%08x ", start_va);
		pte_t *p_pte = pgdir_walk(kern_pgdir, (void *)start_va, 0);
		if (!p_pte || !(*p_pte & PTE_P)) 
			cprintf("page not mapped\n");
		else {
			cprintf("PA: 0x%08x ", PTE_ADDR(*p_pte));
			printp(p_pte);
			cputchar('\n');
		}
		start_va += PGSIZE;
	}
	
	return 0;
}
```

测试如下：

```
K> showmappings 0xf0100000 0xf0108000
VA: 0xf0100000 PA: 0x00100000 -W
VA: 0xf0101000 PA: 0x00101000 -W
VA: 0xf0102000 PA: 0x00102000 -W
VA: 0xf0103000 PA: 0x00103000 -W
VA: 0xf0104000 PA: 0x00104000 -W
VA: 0xf0105000 PA: 0x00105000 -W
VA: 0xf0106000 PA: 0x00106000 -W
VA: 0xf0107000 PA: 0x00107000 -W
VA: 0xf0108000 PA: 0x00108000 -W
K> showmappings 0xef000000 0xef008000
VA: 0xef000000 PA: 0x0011d000 U-
VA: 0xef001000 PA: 0x0011e000 U-
VA: 0xef002000 PA: 0x0011f000 U-
VA: 0xef003000 PA: 0x00120000 U-
VA: 0xef004000 PA: 0x00121000 U-
VA: 0xef005000 PA: 0x00122000 U-
VA: 0xef006000 PA: 0x00123000 U-
VA: 0xef007000 PA: 0x00124000 U-
VA: 0xef008000 PA: 0x00125000 U-
K> QEMU 2.3.0 monitor - type 'help' for more information
(qemu) info pg
VPN range     Entry         Flags        Physical page
[ef000-ef3ff]  PDE[3bc]     -------UWP
  [ef000-ef3ff]  PTE[000-3ff] -------U-P 0011d-0051c
[ef400-ef7ff]  PDE[3bd]     -------U-P
  [ef7bc-ef7bc]  PTE[3bc]     -------UWP 003fd
  [ef7bd-ef7bd]  PTE[3bd]     -------U-P 0011c
  [ef7bf-ef7bf]  PTE[3bf]     -------UWP 003fe
  [ef7c0-ef7df]  PTE[3c0-3df] ----A--UWP 003ff 003fc 003fb 003fa 003f9 003f8 ..
  [ef7e0-ef7ff]  PTE[3e0-3ff] -------UWP 003dd 003dc 003db 003da 003d9 003d8 ..
[efc00-effff]  PDE[3bf]     -------UWP
  [efff8-effff]  PTE[3f8-3ff] --------WP 00110-00117
[f0000-f03ff]  PDE[3c0]     ----A--UWP
  [f0000-f0000]  PTE[000]     --------WP 00000
  [f0001-f009f]  PTE[001-09f] ---DA---WP 00001-0009f
  [f00a0-f00b7]  PTE[0a0-0b7] --------WP 000a0-000b7
  [f00b8-f00b8]  PTE[0b8]     ---DA---WP 000b8
  [f00b9-f00ff]  PTE[0b9-0ff] --------WP 000b9-000ff
  [f0100-f0101]  PTE[100-101] ----A---WP 00100-00101
  [f0102-f0102]  PTE[102]     --------WP 00102
  [f0103-f0105]  PTE[103-105] ----A---WP 00103-00105
  [f0106-f0116]  PTE[106-116] --------WP 00106-00116
...
```

**Explicitly set, clear, or change the permissions of any mapping in the current address space.**

这一问对应changeperm函数

```
int
changeperm(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 4) {
		cprintf("Usage: changepermission VADDR [U|W] [0|1]\n");
		return 0;
	}
	uintptr_t va = strtol(argv[1], NULL, 0); 
	if (argv[2][0] != 'U' && argv[2][0] != 'W') {
		cprintf("Usage: changepermission VADDR [U|W] [0|1]\n");
		return 0;
	}
	if (argv[3][0] != '0' && argv[3][0] != '1') {
		cprintf("Usage: changepermission VADDR [U|W] [0|1]\n");
		return 0;
	}
	pte_t *p_pte = pgdir_walk(kern_pgdir, (void *)va, 0);
	if (!p_pte || !(*p_pte & PTE_P)) {
		cprintf("Error: Page not mapped\n");
		return 0;
	}
	cprintf("Permission bits before operation: ");
	printp(p_pte); cprintf("\n");
	
	pte_t perm;
	if (argv[2][0] == 'U') perm = PTE_U;
	else perm = PTE_W;
	if (argv[3][0] == '0') *p_pte &= ~perm;
	else *p_pte |= perm;
	
	cprintf("Permission bits after operation: ");
	printp(p_pte); cprintf("\n");
	
	return 0;
}
```

测试如下：

```
(qemu) info pg
VPN range     Entry         Flags        Physical page
...
[f0000-f03ff]  PDE[3c0]     ----A--UWP
  [f0000-f0000]  PTE[000]     --------WP 00000
...
K> changepermission 0xf0000000 W 0
Permission bits before operation: -W
Permission bits after operation: --
K> (qemu) info pg
VPN range     Entry         Flags        Physical page
...
[f0000-f03ff]  PDE[3c0]     ----A--UWP
  [f0000-f0000]  PTE[000]     ---------P 00000
...
```

**Dump the contents of a range of memory given either a virtual or physical address range. Be sure the dump code behaves correctly when the range extends across page boundaries!**

这一问对应dumpmem函数，给定的范围中可能有没有权限的部分或者为映射的部分，这里我设置对应的字节显示的值为??

```
int
dumpmem(int argc, char **argv, struct Trapframe *tf) {
	if (argc != 4 || (argv[1][0] != 'P' && argv[1][0] != 'V')) {
		cprintf("Usage: dumpmem [P|V] START_VA N_WORDS\n");
		return 0;
	}
	uintptr_t start_va = strtol(argv[2], NULL, 0); 
	uint32_t num = strtol(argv[3], NULL, 0);
	if (argv[1][0] == 'P') {
		if (PGNUM(start_va) + num >= npages) {
			cprintf("Error: Physical address out of bound\n");
			return 0;
		}
		start_va = (uintptr_t)KADDR((physaddr_t)start_va);
	}
	while (num--) {
		cprintf("0x%08x: 0x", start_va);
		for (int i = 3; i >= 0; i--) {
			void *ptr = (void *)(start_va + i);
			pte_t *p_pte = pgdir_walk(kern_pgdir, ptr, 0);
			if (!p_pte || !(*p_pte & PTE_P))
				cprintf("??");
			else
				cprintf("%02x", *(uint8_t *)ptr);
		}
		cputchar('\n');
		start_va += 4;
	}
	
	return 0;
}
```

测试如下：

```
K> dumpmem V 0xf0100000 1
0xf0100000: 0x1badb002
K> dumpmem V 0xee000000 1
0xee000000: 0x????????
K> dumpmem V 0xeeffffff 1
0xeeffffff: 0x000000??
K> dumpmem V 0xef000000 1
0xef000000: 0x00000000
K> dumpmem V 0xf0500000 1
0xf0500000: 0x97979797
K> dumpmem P 0x100000 1
0xf0100000: 0x1badb002
```

对应的gdb输出如下：

```
(gdb) x/1w 0xf0100000
0xf0100000 <_start+4026531828>:	0x1badb002
(gdb) x/1w 0xee000000
0xee000000:	Cannot access memory at address 0xee000000
(gdb) x/1w 0xeeffffff
0xeeffffff:	Cannot access memory at address 0xeeffffff
(gdb) x/1w 0xef000000
0xef000000:	0x00000000
(gdb) x/1w 0xf0500000
0xf0500000:	0x97979797
```

**Do anything else that you think might be useful later for debugging the kernel.**

我添加了一个pagestatus指令检测某个物理页是空闲还是占用状态

```
int
pagestatus(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 2) {
		cprintf("Usage: pagestatus PADDR\n");
		return 0;
	}
	physaddr_t pa = strtol(argv[1], NULL, 0);
	if (pa % PGSIZE) {
		cprintf("Error: PADDR not aligned\n");
		return 0;
	}
	if (PGNUM(pa) >= npages) {
		cprintf("Error: PADDR out of bound\n");
		return 0;
	}
	if (page_status(pa))
		cprintf("allocated\n");
	else 
		cprintf("free\n");
	
	return 0;
}
```

它利用了在pmap.c中新加入的函数page_status：

```
int
page_status(physaddr_t pa)
{
	struct PageInfo *pp = page_lookup(kern_pgdir, KADDR(pa), NULL), *pp2;
	for (pp2 = page_free_list; pp2; pp2 = pp2->pp_link)
		if (pp2 == pp) return 0;
	return 1;
}
```

测试如下：

```
K> pagestatus 0x100000
allocated
K> pagestatus 0x1100000
free
```

## This Complete The Lab.

