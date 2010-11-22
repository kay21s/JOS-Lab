/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/monitor.h>
#include <kern/sched.h>

struct Env *envs = NULL;		// All environments
struct Env *curenv = NULL;		// The current env
static struct Env_list env_free_list;	// Free list

#define ENVGENSHIFT	12		// >= LOGNENV

//
// Converts an envid to an env pointer.
//
// RETURNS
//   0 on success, -E_BAD_ENV on error.
//   On success, sets *penv to the environment.
//   On error, sets *penv to NULL.
//
int
envid2env(envid_t envid, struct Env **env_store, bool checkperm)
{
	struct Env *e;

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = curenv;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check the env_id field in that struct Env
	// to ensure that the envid is not stale
	// (i.e., does not refer to a _previous_ environment
	// that used the same slot in the envs[] array).
	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	*env_store = e;
	return 0;
}

//
// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the env_free_list.
// Insert in reverse order, so that the first call to env_alloc()
// returns envs[0].
//
void
env_init(void)
{
	// LAB 3: Your code here.
	int i = 0;

	LIST_INIT(&env_free_list);
	for (i=NENV-1; i>=0; i--) {
		envs[i].env_id = 0;
		envs[i].env_runs = 0;
		envs[i].env_status = ENV_FREE;
		LIST_INSERT_HEAD(&env_free_list, &envs[i], env_link);
	}
}

//
// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir and e->env_cr3 accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-E_NO_MEM if page directory or table could not be allocated.
//
static int
env_setup_vm(struct Env *e)
{
	int i, r;
	struct Page *p = NULL;

	// Allocate a page for the page directory
	if ((r = page_alloc(&p)) < 0)
		return r;

	// Now, set e->env_pgdir and e->env_cr3,
	// and initialize the page directory.
	//
	// Hint:
	//    - Remember that page_alloc doesn't zero the page.
	//    - The VA space of all envs is identical above UTOP
	//	(except at VPT and UVPT, which we've set below).
	//	See inc/memlayout.h for permissions and layout.
	//	Can you use boot_pgdir as a template?  Hint: Yes.
	//	(Make sure you got the permissions right in Lab 2.)
	//    - The initial VA below UTOP is empty.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: In general, pp_ref is not maintained for
	//	physical pages mapped only above UTOP, but env_pgdir
	//	is an exception -- you need to increment env_pgdir's
	//	pp_ref for env_free to work correctly.

	// LAB 3: Your code here.
	memset(page2kva(p), 0, PGSIZE);
	e->env_pgdir = page2kva(p);
	e->env_cr3 = page2pa(p);
	p->pp_ref ++;

#if 0
	boot_map_segment(e->env_pgdir, UPAGES, ROUNDUP(npage*sizeof(struct Page), PGSIZE), (physaddr_t)PADDR(pages), PTE_U);
	boot_map_segment(e->env_pgdir, UENVS, ROUNDUP(NENV*sizeof(struct Env), PGSIZE), (physaddr_t)PADDR(envs), PTE_U);
	boot_map_segment(e->env_pgdir, KSTACKTOP-KSTKSIZE, KSTKSIZE, (physaddr_t)PADDR(bootstack), PTE_W);
	boot_map_segment(e->env_pgdir, KSTACKTOP-PTSIZE, PTSIZE-KSTKSIZE, 0, 0);
	boot_map_segment(e->env_pgdir, KERNBASE, 0xffffffff-KERNBASE+1, 0, PTE_W);
#else
	for (i=PDX(UTOP); i<NPDENTRIES; i++)
		e->env_pgdir[i] = boot_pgdir[i];
#endif

	// VPT and UVPT map the env's own page table, with
	// different permissions.
	e->env_pgdir[PDX(VPT)]  = e->env_cr3 | PTE_P | PTE_W;
	e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_P | PTE_U;

	return 0;
}

//
// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-E_NO_FREE_ENV if all NENVS environments are allocated
//	-E_NO_MEM on memory exhaustion
//
int
env_alloc(struct Env **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	struct Env *e;

	if (!(e = LIST_FIRST(&env_free_list)))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);
	
	// Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and 
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_eip later.

	// Enable interrupts while in user mode.
	// LAB 4: Your code here.
	e->env_tf.tf_eflags |= FL_IF;

	// Clear the page fault handler until user installs one.
	e->env_pgfault_upcall = 0;

	// Also clear the IPC receiving flag.
	e->env_ipc_recving = 0;

	// If this is the file server (e == &envs[1]) give it I/O privileges.
	// LAB 5: Your code here.

	// commit the allocation
	LIST_REMOVE(e, env_link);
	*newenv_store = e;

	// cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}

//
// Allocate len bytes of physical memory for environment env,
// and map it at virtual address va in the environment's address space.
// Does not zero or otherwise initialize the mapped pages in any way.
// Pages should be writable by user and kernel.
// Panic if any allocation attempt fails.
//
static void
segment_alloc(struct Env *e, void *va, size_t len)
{
	// LAB 3: Your code here.
	// (But only if you need it for load_icode.)
	//
	// Hint: It is easier to use segment_alloc if the caller can pass
	//   'va' and 'len' values that are not page-aligned.
	//   You should round va down, and round len up.
	void *aligned_va = ROUNDDOWN(va, PGSIZE);
	size_t i, aligned_len = ROUNDUP(len, PGSIZE);
	struct Page *page;
	
	for (i=0; i<aligned_len; i+=PGSIZE) {
		if (-E_NO_MEM == page_alloc(&page)) {
			panic("No memory\n");
		}
		page_insert(e->env_pgdir, page, aligned_va+i, PTE_U | PTE_W);
	}
}

//
// Set up the initial program binary, stack, and processor flags
// for a user process.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
//
// This function loads all loadable segments from the ELF binary image
// into the environment's user memory, starting at the appropriate
// virtual addresses indicated in the ELF program header.
// At the same time it clears to zero any portions of these segments
// that are marked in the program header as being mapped
// but not actually present in the ELF file - i.e., the program's bss section.
//
// All this is very similar to what our boot loader does, except the boot
// loader also needs to read the code from disk.  Take a look at
// boot/main.c to get ideas.
//
// Finally, this function maps one page for the program's initial stack.
//
// load_icode panics if it encounters problems.
//  - How might load_icode fail?  What might be wrong with the given input?
//
static void
load_icode(struct Env *e, uint8_t *binary, size_t size)
{
	// Hints: 
	//  Load each program segment into virtual memory
	//  at the address specified in the ELF section header.
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like segment_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	// Hint:
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() and env_pop_tf() below.)

	// LAB 3: Your code here.
	struct Proghdr *ph, *eph;
	struct Elf *elfhdr = (struct Elf *)binary;
	struct Page *page;
	int copy_size, copy_count;
	uintptr_t page_offset; 
	void *copy_to, *copy_from;

	// is this a valid ELF?
	if (elfhdr->e_magic != ELF_MAGIC)
		panic("Bad ELF Format in kern/env.c/load_icode()!\n");

	// load each program segment (ignores ph flags)
	ph = (struct Proghdr *) ((uint8_t *) elfhdr + elfhdr->e_phoff);
	eph = ph + elfhdr->e_phnum;
	for (; ph < eph; ph ++) {
		if (ph->p_type == ELF_PROG_LOAD) {
			// allocate memory in the Env's page table
			// FIXME:jump the first page -- the first segment is loaded into 0x24
			segment_alloc(e, (void *)ph->p_va, ph->p_memsz);

			// copy binary+ph->offset, length: ph->p_filesz to ph->p_va in the Env's page table
			void *copy_ptr = binary + ph->p_offset;

			for (copy_count = 0; copy_count < ph->p_filesz; ) {
				if (NULL == (page = page_lookup(e->env_pgdir, (void *)(ph->p_va + copy_count), NULL))) {
					panic("Page cannot be find\n");
				}

				// calculate copy_size and copy_to according to start add.
				// the first time the p_va maybe not at the beginning of a page
				// if p_va+copy_count is at the beginning of a page, copy_size is PGSIZE
				page_offset = PGOFF(ph->p_va + copy_count);
				copy_size = PGSIZE - page_offset;

				// fix copy_size and copy_to according to end add.
				// if the end is within this page
				if (copy_count + copy_size > ph->p_filesz) {
					copy_size = ph->p_filesz - copy_count;
				}

				copy_from = copy_ptr + copy_count;
				copy_to = page2kva(page) + page_offset;

				memmove(copy_to, copy_from, copy_size);

				copy_count += copy_size;
			}

			if (copy_count != ph->p_filesz) 
				panic("Unequal of copy_count and ph->p_filesz\n");
			// set p_filesz to p_memsz zero
			for ( ; copy_count < ph->p_memsz; ) {
				if (NULL == (page = page_lookup(e->env_pgdir, (void *)(ph->p_va + copy_count), NULL))) {
					panic("Page cannot be find\n");
				}

				// calculate copy_size and copy_to according to start add.
				// the first time the p_va maybe not at the beginning of a page
				// if p_va+copy_count is at the beginning of a page, copy_size is PGSIZE
				page_offset = PGOFF(ph->p_va + copy_count);
				copy_size = PGSIZE - page_offset;

				// fix copy_size and copy_to according to end add.
				// if the end is within this page
				if (copy_count + copy_size > ph->p_memsz) {
					copy_size = ph->p_memsz - copy_count;
				}

				copy_to = page2kva(page) + page_offset;

				memset(copy_to, 0, copy_size);

				copy_count += copy_size;
			}

			if (copy_count != ph->p_memsz) 
				panic("Unequal of copy_count and ph->p_filesz\n");
		}
	}

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	// LAB 3: Your code here.
	segment_alloc(e, (uintptr_t *)(USTACKTOP - PGSIZE), PGSIZE);

	// Set the program's entry point e->env_tf.tf_eip
	e->env_tf.tf_eip = elfhdr->e_entry;
}

//
// Allocates a new env and loads the named elf binary into it.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
// The new env's parent ID is set to 0.
//
void
env_create(uint8_t *binary, size_t size)
{
	// LAB 3: Your code here.
	struct Env *env;
	switch(env_alloc(&env, 0))
	{
		case -E_NO_FREE_ENV:
			panic("All NENVS environments are allocated\n");
			return;
		case -E_NO_MEM:
			panic("No memory\n");
			return;
		default:
			break;
	}

	env->env_status = ENV_RUNNABLE;
	load_icode(env, binary, size);
}

//
// Frees env e and all memory it uses.
// 
void
env_free(struct Env *e)
{
	pte_t *pt;
	uint32_t pdeno, pteno;
	physaddr_t pa;
	
	// If freeing the current environment, switch to boot_pgdir
	// before freeing the page directory, just in case the page
	// gets reused.
	if (e == curenv)
		lcr3(boot_cr3);

	// Note the environment's demise.
	// cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

	// Flush all mapped pages in the user portion of the address space
	static_assert(UTOP % PTSIZE == 0);
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) {

		// only look at mapped page tables
		if (!(e->env_pgdir[pdeno] & PTE_P))
			continue;

		// find the pa and va of the page table
		pa = PTE_ADDR(e->env_pgdir[pdeno]);
		pt = (pte_t*) KADDR(pa);

		// unmap all PTEs in this page table
		for (pteno = 0; pteno <= PTX(~0); pteno++) {
			if (pt[pteno] & PTE_P)
				page_remove(e->env_pgdir, PGADDR(pdeno, pteno, 0));
		}

		// free the page table itself
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa));
	}

	// free the page directory
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->env_status = ENV_FREE;
	LIST_INSERT_HEAD(&env_free_list, e, env_link);
}

//
// Frees environment e.
// If e was the current env, then runs a new environment (and does not return
// to the caller).
//
void
env_destroy(struct Env *e) 
{
	env_free(e);

	if (curenv == e) {
		curenv = NULL;
		sched_yield();
	}
}


//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
// This function does not return.
// popal -- pop eax,ebx,ecx,edx,esp,ebp,esi,edi
//
void
env_pop_tf(struct Trapframe *tf)
{
	__asm __volatile("movl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//  (This function does not return.)
//
void
env_run(struct Env *e)
{
	// Step 1: If this is a context switch (a new environment is running),
	//	   then set 'curenv' to the new environment,
	//	   update its 'env_runs' counter, and
	//	   and use lcr3() to switch to its address space.
	// Step 2: Use env_pop_tf() to restore the environment's
	//	   registers and drop into user mode in the
	//	   environment.

	// Hint: This function loads the new environment's state from
	//	e->env_tf.  Go back through the code you wrote above
	//	and make sure you have set the relevant parts of
	//	e->env_tf to sensible values.
	
	// LAB 3: Your code here.

	if (e->env_status != ENV_RUNNABLE)
		panic("not runable\n");
	curenv = e;
	e->env_runs ++;
	lcr3(e->env_cr3);
	env_pop_tf(&(e->env_tf));
}

