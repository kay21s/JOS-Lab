// LAB 6: Your driver code here
#include <kern/e100.h>
#include <kern/pci.h>
#include <kern/pmap.h>

#include <inc/stdio.h>
#include <inc/x86.h>
#include <inc/types.h>
#include <inc/string.h>

#define CB_COUNT 16

struct pci_record {
	uint32_t reg_base[6];
	uint32_t reg_size[6];
	uint8_t irq_line;
};

struct pci_record pcircd;
struct tcb *cbl;
uint32_t csr_port;
void *cu_base;
struct tcb *cu_ptr; // pointer where to put the next transmit packet
struct tcb *ru_ptr;

int debug = 1;

static int tx_alloc_cbl(void)
{
	int i;
	struct tcb *cb_p;
	/* cb_size is 1518+8+8=1534 bytes
	 * However, because we use 32-bit machine, alignment
	 * is required. Here we use 4 bytes alignment
	 */
	uint32_t cb_size = ROUNDUP(sizeof(struct tcb), 4);

	// Use the flash memory, it is 16K, the physical memory is 4K
	cbl = (struct tcb *)(pcircd.reg_base[2]);
	cb_p = cbl;

	// Set CU Base and CU_ptr for further transmission
	cu_base = cu_ptr = cbl;

	// Construct the DMA TX Ring
	for (i = 0; i < CB_COUNT - 1; i ++) {
		cb_p->cb.cmd = 0;
		cb_p->cb.status = 0;
		cb_p->cb.link = cb_size * (i + 1);
		cb_p++;
	}
	// Finish the Ring
	cb_p->cb.link = 0;
	return 0;
}

int nic_e100_enable(struct pci_func *pcif)
{
	int i;

	pci_func_enable(pcif);

	// Record the information
	for (i = 0; i < 6; i ++) {
		pcircd.reg_base[i] = pcif->reg_base[i];
		pcircd.reg_size[i] = pcif->reg_size[i];
	}
	pcircd.irq_line = pcif->irq_line;

	csr_port = pcif->reg_base[1];

	// reg_base[0] is the CSR Memory Mapped Base Address Register
	// reg_base[1] is the CSR I/O Mapped Base Address Register
	// reg_base[2] is the Flash Memory Mapped Base Address Register
	
	// Reset the NIC card by writing 0x0000 into the PORT area in CSR
	outl(csr_port + 0x8, 0x0);

	// Alloc the CB Ring
	tx_alloc_cbl();

	// Load CU base
	// First write the General Pointer field in SCB
	outl(csr_port + 0x4, PADDR((uint32_t)cu_base));
	// Second, write the SCB command to load the pointer
	outb(csr_port + 0x2, SCBCMD_CU_LOAD_BASE);
	
	return 0;
}

// Before we transmit a packet, we first reclaim any buffers which 
// have been marked as transmitted by the E100
static void tx_reclaim_transmited_buffers(struct tcb *tcb_ptr)
{
	int i;
	for (i = 0; i < CB_COUNT; i ++, tcb_ptr = (cu_base + tcb_ptr->cb.link)) {
		if ((tcb_ptr->cb.status & TCBSTS_C) && (tcb_ptr->cb.status & TCBSTS_OK)) {
			// Completed Successful

			// Clear the bit
			tcb_ptr->cb.status = 0;
			tcb_ptr->cb.cmd = 0;
	 	} else if((tcb_ptr->cb.status & TCBSTS_C) && !(tcb_ptr->cb.status & TCBSTS_OK)) {
			// Error Occured, just reuse this block
			cprintf("One Frame Transmit Error! : %d\n", i);
			tcb_ptr->cb.status = 0;
			tcb_ptr->cb.cmd = 0;
		}
	}
}

// If the tcb block can be used by the driver to transmit packet
static int tcb_avail(struct tcb *tcb_ptr)
{
	return !(tcb_ptr->cb.cmd);
}

static void construct_tcb_header(struct tcb *tcb_ptr, uint16_t datalen)
{
	tcb_ptr->cb.cmd = TCBCMD_TRANSMIT | TCBCMD_SUSPEND;
	tcb_ptr->cb.status = 0;
	tcb_ptr->tbd_array_addr = 0xFFFFFFFF;
	tcb_ptr->tbd_thrs = 0xE0;
	tcb_ptr->tbd_byte_count = datalen;

	if (debug)
		cprintf("DBG: Construct a tcb header : datalen = %d, this ptr = %x, next = %x\n", 
			tcb_ptr->tbd_byte_count, tcb_ptr, tcb_ptr->cb.link);
}

int nic_e100_trans_pkt(void *pkt_data, uint32_t datalen)
{
	if (debug)
		cprintf("DBG: Transmit a packet, datalen = %d\n", datalen);

	// First, reclaim any buffer that is marked transmitted by E100
	tx_reclaim_transmited_buffers(cu_ptr);

	if (!tcb_avail(cu_ptr)) {
		// There is no available slot in the DMA transmit ring
		// Drop the packet
	
		if (debug)
			cprintf("DBG: No slot in DMA ring available, Drop the packet\n");

		return 0;
	}

	// Put the pkt_data into the available slot
	construct_tcb_header(cu_ptr, (uint16_t)datalen);
	// Copy the data into the
	memmove(cu_ptr->pkt_data, pkt_data, datalen);

	uint8_t cu_status = SCBSTS_CU_MASK & inb(csr_port + 0x1);

	if (cu_status == SCBSTS_CU_IDLE) {
	// Start CU if it is idle, CU is not associated with a CB in the CBL
	
		if (debug)
			cprintf("DBG: CU is Idle, Start it\n");

		// Resume the CU
		// FIXME: Cannot use start CU command here, why?
		outb(csr_port + 0x2, SCBCMD_CU_RESUME);
#if 0
		// Start the CU
		outb(csr_port + 0x2, SCBCMD_CU_START);
#endif
	} else if (cu_status == SCBSTS_CU_SUSP) {
	// Resume CU if it is suspended, CU has read the next link in the CBL
	
		if (debug)
			cprintf("DBG: CU is Suspended, Resume it\n");

		// Resume the CU
		outb(csr_port + 0x2, SCBCMD_CU_RESUME);
	}
	// else, CU is working, we leave her alone =)

	// Update cu_ptr to point to next frame, so we can use it when we transmit
	// a packet next time
	cu_ptr = cu_base + cu_ptr->cb.link;

	return 0;
}
