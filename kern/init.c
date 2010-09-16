/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/monitor.h>
#include <kern/console.h>

unsigned read_eip();
__inline void record_stack(struct Trapframe *) __attribute__((always_inline));
// Test the stack backtrace function (lab 1 only)
void
test_backtrace(int x)
{
	static int count = 0;
	int i;
	static Trapframe frame_stack[6]; //FIXME: Assume at most five calls

	count ++;
	cprintf("entering test_backtrace %d\n", x);
	if (x > 0) {
#if defined(USE_ANOTHER_INLINE_FUNC)
		record_stack(&(frame_stack[x]));
#else		
		frame_stack[x].ebp = read_ebp();
		frame_stack[x].eip = read_eip();	
		for(i = 1; i <= ARG_NUM; i ++) {
			frame_stack[x].args[i-1] = read_arg(i, frame_stack[x].ebp);
		}
#endif
		test_backtrace(x-1);
	} else {
#if defined(USE_ANOTHER_INLINE_FUNC)
		record_stack(&(frame_stack[x]));
#else		
		frame_stack[x].ebp = read_ebp();
		frame_stack[x].eip = read_eip();	
		for(i = 1; i <= ARG_NUM; i ++) {
			frame_stack[x].args[i-1] = read_arg(i, frame_stack[x].ebp);
		}
#endif
		//mon_backtrace(count, 0, frame_stack);
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

	// Drop into the kernel monitor.
	while (1)
		monitor(NULL);
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
