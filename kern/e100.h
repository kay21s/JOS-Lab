#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <kern/pci.h>

#define MAX_ETH_FRAME	1518

struct cb {
	volatile uint16_t status;
	uint16_t cmd;
	uint32_t link;
};

struct tcb {
	struct cb cb;
	uint32_t tbd_array_addr;
	uint16_t tbd_byte_count;
	uint16_t tbd_thrs;
	uint8_t pkt_data[MAX_ETH_FRAME];
};

struct rfd {
	struct cb cb;
	uint32_t reserved;
	uint16_t actual_count;
	uint16_t buffer_size;
	uint8_t pkt_data[MAX_ETH_FRAME];
};

struct pci_record {
	uint32_t reg_base[6];
	uint32_t reg_size[6];
	uint8_t irq_line;
};

struct pci_record pcircd;


// Public Functions
int nic_e100_enable(struct pci_func *);
int nic_e100_trans_pkt(void *, uint32_t);
int nic_e100_recv_pkt(void *);

// TCB Command in TCB structure
#define TCBCMD_NOP		0x0000
#define TCBCMD_IND_ADD_SETUP	0x0001
#define TCBCMD_CONFIGURE	0x0002
#define TCBCMD_MUL_ADD_SETUP	0x0003
#define TCBCMD_TRANSMIT		0x0004
#define TCBCMD_LD_MICROCODE	0x0005
#define TCBCMD_DUMP		0x0006
#define TCBCMD_DIAGNOSE		0x0007

// Go into Idle state after this frame is processed
#define TCBCMD_EL		0x8000 
// Go into Suspended state after this frame is processed
#define TCBCMD_S		0x4000

// CB Status in CB structure
#define CBSTS_C			0x8000
#define CBSTS_OK		0x2000

// SCB Command

#define SCBCMD_CU_NOP			0x0000
#define SCBCMD_CU_START			0x0010
#define SCBCMD_CU_RESUME		0x0020
#define SCBCMD_CU_LOAD_COUNTER_ADD	0x0040
#define SCBCMD_CU_DUMP_STAT_COUNTER	0x0050
#define SCBCMD_CU_LOAD_BASE		0x0060
#define SCBCMD_CU_DUMP_RESET_COUNTER	0x0070
#define SCBCMD_CU_STATIC_RESUME		0x00a0

#define SCBCMD_RU_NOP		0x0000
#define SCBCMD_RU_START		0x0001
#define SCBCMD_RU_RESUME	0x0002
#define SCBCMD_RU_RDR		0x0003
#define SCBCMD_RU_ABORT		0x0004
#define SCBCMD_RU_LOAD_HDS	0x0005
#define SCBCMD_RU_LOAD_BASE	0x0006

// Enable Interrupts in SCB Command
#define SCBINT_CX		0x8000  // CU interrupts when an action completed
#define SCBINT_FR		0x4000  // RU interrupts when a frame is received
#define SCBINT_CNA		0x2000  // CU interrupts when its status changed
#define SCBINT_RNR		0x1000  // RU is not ready
#define SCBINT_ER		0x0800  // Same with FR
#define SCBINT_FCP		0x0400  // A flow control pause frame
#define SCBINT_SI		0x0200  // For software interrupt
#define SCBINT_M		0x0100  // Interrupt mask bit


// SCB Status

#define SCBSTS_CU_IDLE		0x00
#define SCBSTS_CU_SUSP		0x40
#define SCBSTS_CU_LPQA		0x80
#define SCBSTS_CU_HQPA		0xc0

#define SCBSTS_CU_MASK		0xC0

#define SCBSTS_RU_IDLE		0x00
#define SCBSTS_RU_SUSP		0x04
#define SCBSTS_RU_NORES		0x08
#define SCBSTS_RU_READY		0x10

#define SCBSTS_RU_MASK		0x3C

// RFD field
#define RFD_EOF			0x8000
#define RFD_F			0x4000
#define RFD_LEN_MASK		0x3FFF

#endif	// JOS_KERN_E100_H
