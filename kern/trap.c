#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/time.h>
#include <kern/e100.h>

extern int vectors[];	// in trapentry.S: array of 256 entry pointers
static struct Taskstate ts;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < sizeof(excnames)/sizeof(excnames[0]))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}


void
idt_init(void)
{
	extern struct Segdesc gdt[];
	
	// LAB 3: Your code here.
	int i;

	for (i = 0; i < 256; i++)
		SETGATE(idt[i], 0, GD_KT, vectors[i], 0)
	
	// Difference between interrupt gate and trap gate:
	// After transfering the control, an interrupt gate clears IF to disable interrupts
	// however, trap gate does not change the IF flag
	// There will be errors in handling interrput when what's currently running is an exception
	// JOS only allows interrupt happens in user space, and forbid it inkernel space, when a
	// timer interrupt arrives, it will run sched_yield and eax is currently the syscall number
	// which is >0, so user panic at lib/syscall.c
	SETGATE(idt[T_SYSCALL], 0, GD_KT, vectors[T_SYSCALL], 3)
	SETGATE(idt[T_BRKPT], 0, GD_KT, vectors[T_BRKPT], 3)
	SETGATE(idt[T_OFLOW], 0, GD_KT, vectors[T_OFLOW], 3);
	SETGATE(idt[T_BOUND], 0, GD_KT, vectors[T_BOUND], 3);

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts.ts_esp0 = KSTACKTOP;
	ts.ts_ss0 = GD_KD;

	// Initialize the TSS field of the gdt.
	gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
					sizeof(struct Taskstate), 0);
	gdt[GD_TSS >> 3].sd_s = 0;

	// Load the TSS
	ltr(GD_TSS);

	// Load the IDT
	asm volatile("lidt idt_pd");
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p\n", tf);
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	cprintf("  err  0x%08x\n", tf->tf_err);
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	cprintf("  esp  0x%08x\n", tf->tf_esp);
	cprintf("  ss   0x----%04x\n", tf->tf_ss);
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.
	// LAB 3: Your code here.
	struct PushRegs *regs;	
	switch (tf->tf_trapno) {
	case T_SYSCALL:
		regs = &(tf->tf_regs);
		regs->reg_eax = syscall(regs->reg_eax, regs->reg_edx, 
			regs->reg_ecx, regs->reg_ebx, regs->reg_edi, regs->reg_esi);
		return;
	case T_PGFLT:
		page_fault_handler(tf);
		return;
	case T_BRKPT:
		monitor(tf);
		return;
	case T_DEBUG:
		monitor(tf);
		return;

	// Handle clock interrupts.
	// LAB 4: Your code here.
	case IRQ_OFFSET + IRQ_TIMER:
	// Add time tick increment to clock interrupts.
	// LAB 6: Your code here.
		time_tick();
		sched_yield();
		return;
	}


	// Handle spurious interupts
	// The hardware sometimes raises these because of noise on the
	// IRQ line or other reasons. We don't care.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}

void
trap(struct Trapframe *tf)
{
	if ((tf->tf_cs & 3) == 3) {
		// Trapped from user mode.
		// Copy trap frame (which is currently on the stack)
		// into 'curenv->env_tf', so that running the environment
		// will restart at the trap point.
		assert(curenv);
		curenv->env_tf = *tf;
		// The trapframe on the stack should be ignored from here on.
		tf = &curenv->env_tf;
	}
	
	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNABLE)
		env_run(curenv);
	else
		sched_yield();
}


void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.
	
	// LAB 3: Your code here.
	if ((tf->tf_cs & 0x3) == 0)
		panic("Page fault in kernel\n");

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// The trap handler needs one word of scratch space at the top of the
	// trap-time stack in order to return.  In the non-recursive case, we
	// don't have to worry about this because the top of the regular user
	// stack is free.  In the recursive case, this means we have to leave
	// an extra word between the current top of the exception stack and
	// the new stack frame because the exception stack _is_ the trap-time
	// stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 4: Your code here.

	struct UTrapframe *uxstk_p;

	if (curenv->env_pgfault_upcall == NULL)
		goto destroy_env;

	// check if the pgfault_handler is OK
	user_mem_assert(curenv, ROUNDDOWN(curenv->env_pgfault_upcall, PGSIZE), PGSIZE, PTE_P);

	if ((tf->tf_esp >= UXSTACKTOP - PGSIZE) && (tf->tf_esp <= UXSTACKTOP - 1)) {
		// nested page fault in the user exception stack
		uxstk_p = (struct UTrapframe *)(tf->tf_esp - sizeof(uint32_t) - sizeof(struct UTrapframe));
	} else {
		uxstk_p = (struct UTrapframe *)(UXSTACKTOP - sizeof(struct UTrapframe));
	}

	// check if the exception stack can be used, not overflow
	user_mem_assert(curenv, (void *)uxstk_p, sizeof(struct UTrapframe), PTE_P | PTE_W);

	uxstk_p->utf_fault_va = fault_va;
	uxstk_p->utf_err = tf->tf_err;
	uxstk_p->utf_regs = tf->tf_regs;
	uxstk_p->utf_eip = tf->tf_eip;
	uxstk_p->utf_eflags = tf->tf_eflags;
	uxstk_p->utf_esp = tf->tf_esp;

	if (curenv->env_pgfault_upcall != NULL) {
		curenv->env_tf.tf_esp = (uint32_t)uxstk_p;
		curenv->env_tf.tf_eip = (uint32_t)curenv->env_pgfault_upcall;
		env_run(curenv);
	}

destroy_env:
	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

