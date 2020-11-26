// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display information about the stack trace", mon_backtrace },
	{ "showmappings", "Display physical mappings within a range", showmappings },
	{ "changeperm", "Change permission bits on a specified page", changeperm },
	{ "dumpmem", "Dump memory content within a range", dumpmem },
	{ "pagestatus", "Show the allocation status of a physical page", pagestatus }
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t *ebp = (uint32_t *)read_ebp(), *eip;
	struct Eipdebuginfo info;
	int i;
	
	cprintf("Stack backtrace:\n");
	while (ebp) {
		cprintf("  ebp %08x  eip %08x  args", 
			(uint32_t)ebp, eip = (uint32_t *)*(ebp + 1));
		for (i = 0; i < 5; i++) {
			cprintf(" %08x", *(ebp + i + 2));
		}
		cprintf("\n         ");
		if (debuginfo_eip((uintptr_t)eip, &info))
			panic("Unresolvable stab errors!");
		cprintf("%s:%d: ", info.eip_file, info.eip_line);
		cprintf("%.*s", info.eip_fn_namelen, info.eip_fn_name);
		cprintf("+%d\n", (int)eip - (int)info.eip_fn_addr);
			
		ebp = (uint32_t *)*ebp;
	}
	
	return 0;
}

static void
printp(pte_t *p_pte) {
	if (*p_pte & PTE_U) cputchar('U');
	else cputchar('-');
	if (*p_pte & PTE_W) cputchar('W');
	else cputchar('-');
}

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

int
dumpmem(int argc, char **argv, struct Trapframe *tf) 
{
	if (argc != 4 || (argv[1][0] != 'P' && argv[1][0] != 'V')) {
		cprintf("Usage: dumpmem [P|V] START_ADDR N_WORDS\n");
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


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("\x1b[31mThis \x1b[32mline \x1b[33mis \x1b[34mfor "
			"\x1b[35mtesting \x1b[36mthe \x1b[37mcolor.\n");
	cprintf("\x1b[33;41mThis \x1b[33;42mline \x1b[30;43mis \x1b[33;44mfor "
			"\x1b[33;45mtesting \x1b[33;46mthe \x1b[33;47mcolor.\n\x1b[37;40m");
	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
