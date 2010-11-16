#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	//cprintf("err = %x\n, envid = %x, fault_va = 0x%x\n", err, sys_getenvid(), addr);
	if ((err & FEC_WR) == 0)
		panic("Faulting access was not a WRITE!");
	if ((vpt[(uint32_t)addr/PGSIZE] & PTE_COW) == 0)
		panic("Faulting at page not COW! : %x", addr);
	if (((uint32_t)addr >= UTOP) && ((uint32_t)addr <= ULIM))
		panic("Write at READ_ONLY page!");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	void *va = (void *)ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	memmove(PFTEMP, va, PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, va, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why might we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	int perm = (vpt[pn] & PTE_USER);
	if ((perm & (PTE_W | PTE_COW)) != 0) {
		//if(vpt[pn]&PTE_W) cprintf("PTE_W\n");
		//if(vpt[pn]&PTE_COW) cprintf("PTE_COW\n");
		perm &= ~PTE_W;
		perm |= PTE_COW;
		if ((r = sys_page_map(0, (void *)(pn*PGSIZE), envid, (void *)(pn*PGSIZE), perm)) < 0)
			panic("sys_page_map: %e", r);
		if ((r = sys_page_map(0, (void *)(pn*PGSIZE), 0, (void *)(pn*PGSIZE), perm)) < 0)
			panic("sys_page_map: %e", r);
	} else {
		if ((r = sys_page_map(0, (void *)(pn*PGSIZE), envid, (void *)(pn*PGSIZE), perm)) < 0)
			panic("sys_page_map: %e", r);
	}
	return 0;
}


//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" and the user exception stack in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t cid;
	uint32_t va, pn, r;

	set_pgfault_handler(pgfault);
	cid = sys_exofork();

	if (cid < 0) panic("sys_exfork: %e", cid);
	if (cid > 0) {
		for (pn = UTEXT/PGSIZE; pn < UTOP/PGSIZE; pn++) {
			if (pn == (UXSTACKTOP - PGSIZE)/PGSIZE) {
				if ((r = sys_page_alloc(cid, (void *)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
					panic("sys_page_alloc: %e", r);
				continue;
			}
			if (((vpd[pn/NPTENTRIES] & PTE_P) != 0) && ((vpt[pn] & PTE_P) != 0)) {
				duppage(cid, pn);
			}
		}

		if ((r = sys_env_set_pgfault_upcall(cid, env->env_pgfault_upcall)) < 0)
			panic("sys_env_set_pgfault_upcall: %e", r);
		if ((r = sys_env_set_status(cid, ENV_RUNNABLE)) < 0)
			panic("sys_env_set_status: %e", r);
	} else if (cid == 0) {
		env = &envs[ENVX(sys_getenvid())];
	}

	return cid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
