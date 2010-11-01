// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/util.h>
#include <inc/error.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line
//#define USING_RECORDED_FRAME 1


extern pde_t *boot_pgdir;

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display stack information", mon_backtrace},
	{ "showmappings", "Display the physical page mappings and corresponding permission bits", mon_showmappings},
	{ "showcontents", "Display the contents in a memory region, -p to use physical address,\n\t -v to use virtual address", mon_showcontents},
	{ "alloc_page", "Alloc a page", mon_allocpage},
	{ "free_page", "Free a page of a given address", mon_freepage},
	{ "page_status", "Show if a page is freed or allocated", mon_pagestatus},
	{ "continue", "Continue to execute", debug_continue},
	{ "si", "Signle step execution", debug_si},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of debug kernel monitor commands *****/
int
debug_continue(int argc, char **argv, struct Trapframe *tf)
{
	tf->tf_eflags |= FL_RF;
	tf->tf_eflags &= ~FL_TF;
	env_run(curenv);
	return 0;
}

int
debug_si(int argc, char **argv, struct Trapframe *tf)
{
	tf->tf_eflags |= FL_TF;
	tf->tf_eflags &= ~FL_RF;
	env_run(curenv);
	return 0;
}


/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	struct Eipdebuginfo eip_info;

	// Trace the stack to its end
	int i=0, j=0;
	uint32_t ebp, eip;
	char temp;

	ebp = read_ebp();
	
	while (ebp != 0) {
		eip = read_ret_eip(ebp);
		cprintf("ebp %08x  eip %08x  args ", ebp, eip); 
		for(i = 1; i <= ARG_NUM; i ++) {
			cprintf("%08x ", read_arg(i, ebp));
		}
		cprintf("\n");

		debuginfo_eip(eip, &eip_info);
		cprintf("eip information : %s:%d: ", eip_info.eip_file, eip_info.eip_line);
		for(j=0; j<eip_info.eip_fn_namelen; j++)
			cprintf("%c", eip_info.eip_fn_name[j]);
		cprintf("+%d\n", eip - eip_info.eip_fn_addr);
		ebp = read_pre_ebp(ebp);
	}

	return 0;
}

int
mon_allocpage(int argc, char **argv, struct Trapframe *tf)
{
	struct Page *page;
	
	if (page_alloc(&page) == -E_NO_MEM)
		cprintf("Out of memory\n");
	else
		cprintf("\t0x%x\n", page2pa(page));
	return 0;
}

int
mon_freepage(int argc, char **argv, struct Trapframe *tf)
{
	physaddr_t phy;

	if (argc < 2)
		return 0;

	phy = atoi(argv[1]);
	page_free(pa2page(phy));
	return 0;
}

int
mon_pagestatus(int argc, char **argv, struct Trapframe *tf)
{
	physaddr_t phy;

	if (argc < 2)
		return 0;

	phy = atoi(argv[1]);
	cprintf("phy : 0x%x is ", phy);

	if (1 == page_status(pa2page(phy)))
		cprintf("free\n");
	else
		cprintf("allocated\n");

	return 0;
}

int
mon_showcontents(int argc, char **argv, struct Trapframe *tf)
{
	pde_t *pgdir = boot_pgdir;
	pte_t *pt_entry;
	uintptr_t va1, va2;
	uintptr_t va;
	physaddr_t offset1, offset2;
	physaddr_t offset;
	uint32_t length;
	int i = 0, j = 0, count = 0;

	if (argc < 4) {
		cprintf("Bad input\n");
		return 0;
	} else if(argc == 4) {
		if (argv[1][1] == 'v') { // using physical address
			
			va1 = atoi(argv[2]);
			va2 = atoi(argv[3]);
			offset1 = va1 & 0x0fff;
			offset2 = va2 & 0x0fff;
			length = va2 - va1 + 1;

			va = va1;
			if (offset1 == 0) {
				offset = PGSIZE;
			} else {
				offset = PGSIZE - offset1;
			}
			if (offset > va2 - va1) {
				offset = va2 - va1 + 1;
			}

			while (length > 0) {
			
				for (i=0; i<offset; i++) {
					cprintf("%c", va+i);
					count ++;
					if (count == 50) {
						count = 0;
						cprintf("\n");
					}
				}

				j ++;
				length -= i;
				va = va1 + j * PGSIZE - offset1;

				if (length <= PGSIZE && length == (offset2+1)) {
					offset = offset2+1;
				} else if (length > PGSIZE) {
					offset = PGSIZE;
				} else if (length == 0){
					return 0;
				} else {
					cprintf("length = %d, i = %d ,offset2 = %d\n", length, i, offset2);
				}

			}
			
		} else if (argv[1][1] == 'p') {
		}
	}
	return 0;
}

static void 
show_page_table_entry_privilege(pte_t pt_entry)
{
	if (PTE_P & pt_entry) {
		cprintf("PTE_P ");
	}
	if (PTE_W & pt_entry) {
		cprintf("PTE_W ");
	}
	if (PTE_U & pt_entry) {
		cprintf("PTE_U ");
	}
	if (PTE_PWT & pt_entry) {
		cprintf("PTE_PWT ");
	}
	if (PTE_A & pt_entry) {
		cprintf("PTE_A ");
	}
    	if (PTE_D & pt_entry) {
		cprintf("PTE_D ");
	}
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	pde_t *pgdir = boot_pgdir;
	pte_t *pt_entry;
	uintptr_t va1, va2;
	uintptr_t va;
	
	if (argc < 2) {
		cprintf("Bad input\n");
	} else if (argc == 2) {
		va1 = atoi(argv[1]);
		pt_entry = pgdir_walk(pgdir, (void *)va1, 0);
		if (pt_entry == NULL) {
			cprintf("Virtual address %s not mapped\n", argv[1]);
		} else {
			cprintf("0x%x --> 0x%x ", va1, PTE_ADDR(*pt_entry));
			show_page_table_entry_privilege(*pt_entry);
			cprintf("\n");
		}
	} else if (argc == 3) {
		va1 = atoi(argv[1]);
		va2 = atoi(argv[2]);
		for (va=va1; va<=va2; va+=PGSIZE) {
			pt_entry = pgdir_walk(pgdir, (void *)va, 0);
			if (pt_entry == NULL) {
				cprintf("Virtual address %s not mapped\n", argv[1]);
			} else {
				cprintf("0x%x --> 0x%x ", va, PTE_ADDR(*pt_entry));
				show_page_table_entry_privilege(*pt_entry);
				cprintf("\n");
			}
		}
	}
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
	for (i = 0; i < NCOMMANDS; i++) {
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

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
//unsigned
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
