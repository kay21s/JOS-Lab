// LAB 6: Your driver code here
#include <kern/e100.h>
#include <kern/pci.h>
#include <kern/pmap.h>

#include <inc/stdio.h>
#include <inc/x86.h>
#include <inc/types.h>
#include <inc/string.h>

#define TCB_COUNT 16
#define RFD_COUNT 16

uint32_t csr_port;

void *cu_base;
void *ru_base;

struct tcb *cu_ptr; // pointer where to put the next transmit packet
struct rfd *ru_ptr;

static struct rfd *pre_ru_ptr;

int debug = 1;

static int
nic_alloc_cbl(void)
{
	int i;
	struct tcb *cb_p;
	/* tcb_size is 1518+8+8=1534 bytes
	 * However, because we use 32-bit machine, alignment
	 * is required. Here we use 4 bytes alignment
	 */
	uint32_t tcb_size = ROUNDUP(sizeof(struct tcb), 4);

	// Use the flash memory, it is 16K, the physical memory is 4K
	cu_base = (void *)(pcircd.reg_base[2]);

	// Set CU_ptr to cu_base for further transmission
	cu_ptr = cb_p = (struct tcb *)cu_base;

	// Construct the DMA TX Ring
	for (i = 0; i < TCB_COUNT - 1; i ++) {
		cb_p->cb.cmd = 0;
		cb_p->cb.status = 0;
		cb_p->cb.link = tcb_size * (i + 1);
		cb_p++;
	}
	// Finish the Ring
	cb_p->cb.link = 0;
	return 0;
}

static int
nic_alloc_rfa(void)
{
	int i;
	struct rfd *cb_p;
	/* rfd_size is 1518+8+8=1534 bytes
	 * However, because we use 32-bit machine, alignment
	 * is required. Here we use 4 bytes alignment
	 */
	uint32_t rfd_size = ROUNDUP(sizeof(struct rfd), 4);
	uint32_t tcb_size = ROUNDUP(sizeof(struct tcb), 4);

	// Use the flash memory, set it to the end of the area
	// which has been taken by tcb ring
	ru_base = (void *)(pcircd.reg_base[2] + tcb_size * TCB_COUNT);

	// Set ru_ptr to ru_base for further recepted processing
	ru_ptr = cb_p = (struct rfd *)ru_base;

	// Construct the DMA RX Ring
	for (i = 0; i < RFD_COUNT - 1; i ++) {
		cb_p->cb.cmd = 0;
		cb_p->cb.status = 0;
		cb_p->cb.link = rfd_size * (i + 1);
		cb_p->reserved = 0xFFFFFFFF;
		cb_p->buffer_size = MAX_ETH_FRAME;
		cb_p++;
	}
	// Finish the Ring
	cb_p->cb.link = 0;
	cb_p->cb.cmd |= TCBCMD_EL;
	
	pre_ru_ptr = cb_p;

	return 0;
}

int
nic_e100_enable(struct pci_func *pcif)
{
	int i;

	pci_func_enable(pcif);

	// Record the Memory and I/O information
	for (i = 0; i < 6; i ++) {
		pcircd.reg_base[i] = pcif->reg_base[i];
		pcircd.reg_size[i] = pcif->reg_size[i];
	}
	// Record the IRQ Number to receive interrupts from device
	pcircd.irq_line = pcif->irq_line;

	csr_port = pcif->reg_base[1];

	// reg_base[0] is the CSR Memory Mapped Base Address Register
	// reg_base[1] is the CSR I/O Mapped Base Address Register
	// reg_base[2] is the Flash Memory Mapped Base Address Register
	
	// Reset the NIC card by writing 0x0000 into the PORT area in CSR
	outl(csr_port + 0x8, 0x0);

	// Alloc the TCB Ring
	nic_alloc_cbl();

	// Load CU base
	// First write the General Pointer field in SCB
	outl(csr_port + 0x4, PADDR((uint32_t)cu_base));
	// Second, write the SCB command to load the pointer
	outw(csr_port + 0x2, SCBCMD_CU_LOAD_BASE);
	
	// Alloc the RFA
	nic_alloc_rfa();

	if (debug)
		cprintf("DBG: ru_base = %x, irq_line = %d\n", ru_base, pcircd.irq_line);
	// Load RU base
	// First write the General Pointer field in SCB
	outl(csr_port + 0x4, PADDR((uint32_t)ru_base));
	// Second, write the SCB command to load the pointer
	outw(csr_port + 0x2, SCBCMD_RU_LOAD_BASE);

	// Start RU, start receiving packets
	// FIXME: Why I cannot use RU_START?
	outw(csr_port + 0x2, SCBCMD_RU_RESUME);
	
	return 0;
}

// Before we transmit a packet, we first reclaim any buffers which 
// have been marked as transmitted by the E100. But we do not actually
// reclaim it, just make a mark since what we use is a ring buffer
static void
tx_reclaim_transmited_buffers(struct tcb *tcb_ptr)
{
	int i;
	for (i = 0; i < TCB_COUNT; i ++, tcb_ptr = (cu_base + tcb_ptr->cb.link)) {
		if ((tcb_ptr->cb.status & CBSTS_C) && (tcb_ptr->cb.status & CBSTS_OK)) {
			// Completed Successful

			// Clear the bit
			tcb_ptr->cb.status = 0;
			tcb_ptr->cb.cmd = 0;
	 	} else if((tcb_ptr->cb.status & CBSTS_C) && !(tcb_ptr->cb.status & CBSTS_OK)) {
			// Error Occured, just reuse this block
			cprintf("One Frame Transmit Error! : %d\n", i);
			tcb_ptr->cb.status = 0;
			tcb_ptr->cb.cmd = 0;
		}
	}
}

// If the tcb block can be used by the driver to transmit packet
static int
tcb_avail(struct tcb *tcb_ptr)
{
	return !(tcb_ptr->cb.cmd);
}

static void
construct_tcb_header(struct tcb *tcb_ptr, uint16_t datalen)
{
	tcb_ptr->cb.cmd = TCBCMD_TRANSMIT | TCBCMD_S;
	tcb_ptr->cb.status = 0;
	tcb_ptr->tbd_array_addr = 0xFFFFFFFF;
	tcb_ptr->tbd_thrs = 0xE0;
	tcb_ptr->tbd_byte_count = datalen;

	if (debug)
		cprintf("DBG: Construct a tcb header : datalen = %d, this ptr = %x, next = %x\n", 
			tcb_ptr->tbd_byte_count, tcb_ptr, tcb_ptr->cb.link);
}

int
nic_e100_trans_pkt(void *pkt_data, uint32_t datalen)
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

	uint8_t cu_status = SCBSTS_CU_MASK & inb(csr_port + 0x0);

	if (cu_status == SCBSTS_CU_IDLE) {
	// Start CU if it is idle, CU is not associated with a CB in the CBL
	
		if (debug)
			cprintf("DBG: CU is Idle, Start it\n");

		// Resume the CU
		// FIXME: Cannot use CU_START command here, why?
		outw(csr_port + 0x2, SCBCMD_CU_RESUME);

#if 0
		// Start the CU
		outw(csr_port + 0x2, SCBCMD_CU_START);
#endif
	} else if (cu_status == SCBSTS_CU_SUSP) {
	// Resume CU if it is suspended, CU has read the next link in the CBL
	
		if (debug)
			cprintf("DBG: CU is Suspended, Resume it\n");

		// Resume the CU
		outw(csr_port + 0x2, SCBCMD_CU_RESUME);
	}
	// else, CU is working, we leave her alone =)

	// Update cu_ptr to point to next frame, so we can use it when we transmit
	// a packet next time
	cu_ptr = cu_base + cu_ptr->cb.link;

	return 0;
}

static int
rfd_avail(struct rfd *rfd_ptr)
{
	if ((rfd_ptr->cb.status & CBSTS_C) && (rfd_ptr->cb.status & CBSTS_OK)) {
		// A packet is received in this buffer
		if (debug)
			cprintf("DBG: frame available in the rfa, wait...\n");
		return 1;

 	} else if((rfd_ptr->cb.status & CBSTS_C) && !(rfd_ptr->cb.status & CBSTS_OK)) {
		// Error Occured, just reuse this block
		if (debug)
			cprintf("DBG: One Frame Receive Error! : \n");
		return 1;
	}

	return 0;
}

void
nic_clear_rfd(struct rfd *rfd_ptr)
{
	rfd_ptr->cb.cmd = 0;
	rfd_ptr->cb.status = 0;
	rfd_ptr->reserved = 0xFFFFFFFF;
	rfd_ptr->buffer_size = MAX_ETH_FRAME;
	// Most Important: the EOF and F field must be set to 0 
	// to reuse the rfd in the rfa
	rfd_ptr->actual_count = 0;
	rfd_ptr->cb.cmd |= TCBCMD_EL;
}

int
nic_e100_recv_pkt(void *pkt_buf)
{
	int8_t ru_status;
	int16_t pkt_actual_count;

	if (!rfd_avail(ru_ptr)) {
		// There is no available slot in the DMA rfa ring
	
		if (debug)
			cprintf("DBG: No frame available in the rfa, wait...\n");

		return 0;
	}

	pkt_actual_count = RFD_LEN_MASK & ru_ptr->actual_count;
	if (debug)
		cprintf("DBG: A packet with length %d received\n", pkt_actual_count);

	memmove(pkt_buf, ru_ptr->pkt_data, pkt_actual_count);

	nic_clear_rfd(ru_ptr);

	// Mark the previous RU as not suspend after packets received
	pre_ru_ptr->cb.cmd &= ~TCBCMD_EL;
	// Record the previous ru we write to clear
	pre_ru_ptr = ru_ptr;
	// Update ru_ptr to point to next frame, so we can use it when we receive next pkt.
	ru_ptr = ru_base + ru_ptr->cb.link;

	ru_status = SCBSTS_RU_MASK & inb(csr_port + 0x0);

	if (ru_status == SCBSTS_RU_SUSP) {
		// Resume RU, start receiving packets
		outw(csr_port + 0x2, SCBCMD_RU_RESUME);
	} else if (ru_status == SCBSTS_RU_IDLE) {
		outw(csr_port + 0x2, SCBCMD_RU_RESUME);
	}
	// else, RU is working normally and receiving packets, leave her alone =)
	
	return (int)pkt_actual_count;
}
