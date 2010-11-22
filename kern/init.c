/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/picirq.h>

unsigned read_eip();
__inline void record_stack(struct Trapframe *) __attribute__((always_inline));
// Test the stack backtrace function (lab 1 only)
void
test_backtrace(int x)
{
	int i;

	cprintf("entering test_backtrace %d\n", x);
	if (x > 0) {
		test_backtrace(x-1);
	} else {
		mon_backtrace(0,0,0);
	}
	cprintf("leaving test_backtrace %d\n", x);
}

void
i386_init(void)
{
	extern char edata[], end[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

#if defined(LAB1_ONLY)
	cprintf("6828 decimal is %o octal!\n", 6828);

	/* ----- Exercise 8 of Lab 1 -----*/
	int x=1,y=3,z=4;
	cprintf("x %d, y %x, z %d\n", x, y, z);

	unsigned int i = 0x00646c72;
	cprintf("H%x Wo%s\n", 57616, &i);
	// Shows  He110 World, while the demical 0d57616 = 0xe110
	// 'r' = 0x72, 'l' = 0x6c, 'd' = 0x64, '\0' = 0x00 which indicates the end of the string
	// If the x86 is big-endian, 57616 -> 4321, 0x00646c72 -> 0x726c6400 

        // Test the stack backtrace function (lab 1 only)
	test_backtrace(5);
#endif

	// Lab 2 memory management initialization functions
	i386_detect_memory();
	i386_vm_init();

	// Lab 3 user environment initialization functions
	env_init();
	idt_init();
 
	// Lab 4 multitasking initialization functions
	pic_init();
	kclock_init();

	// Should always have an idle process as first one.
	ENV_CREATE(user_idle);

	// Start fs
	ENV_CREATE(fs_fs);

	// Start init
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE2(TEST, TESTSIZE);
#else
	// Touch all you want.
	// ENV_CREATE(user_writemotd);
	// ENV_CREATE(user_testfsipc);
	// ENV_CREATE(user_icode);
#endif // TEST*
 
	// Schedule and run the first user environment!
	sched_yield();


}


/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
static const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	va_start(ap, fmt);
	cprintf("kernel panic at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void
_warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
