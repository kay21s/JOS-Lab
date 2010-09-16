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
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();
__inline void record_stack(struct Trapframe *) __attribute__((always_inline));

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
#if defined(USING_RECORDED_FRAME)
	// Print out the stack using recorded frame list
	int i = 0, j = 0;
	uint32_t ebp;

	ebp = read_ebp();
	cprintf("ebp %08x  eip %08x  args ", ebp, read_ret_eip(ebp)); 
	for(i = 1; i <= ARG_NUM; i ++) {
		cprintf("%08x ", read_arg(i, ebp));
	}
	cprintf("\n");

	for(i = 0; i < argc; i++) {
		cprintf("ebp %08x  eip %08x  args ", tf[i].ebp, tf[i].eip); 
		for(j = 0; j < ARG_NUM; j ++) {
			cprintf("%08x ", tf[i].args[j]);
		}
		cprintf("\n");
	}
#else 
	// Trace the stack to its end
	int i = 0;
	uint32_t ebp, eip;

	ebp = read_ebp();
	
	while (ebp != 0) {
		eip = read_ret_eip(ebp);
		cprintf("ebp %08x  eip %08x  args ", ebp, eip); 
		for(i = 1; i <= ARG_NUM; i ++) {
			cprintf("%08x ", read_arg(i, ebp));
		}
		cprintf("\n");

		debuginfo_eip(eip, &eip_info);
		cprintf("eip information : %s: %s:%d\n", eip_info.eip_file, eip_info.eip_fn_name, eip_info.eip_line);

		ebp = read_pre_ebp(ebp);
	}
#endif
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


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

__inline void
record_stack(struct Trapframe *tf)
{
	int i;

	tf->ebp = read_ebp();
	tf->eip = read_eip();
	
	for(i = 1; i <= ARG_NUM; i ++) {
		tf->args[i-1] = read_arg(i, tf->ebp);
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
