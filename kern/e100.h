#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <kern/pci.h>

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
	uint8_t pkt_data[1518];
};

int nic_e100_enable(struct pci_func *);
int nic_e100_trans_pkt(void *, uint32_t);

// TCB Command in TCB structure
#define TCBCMD_NOP		0x0000
#define TCBCMD_IND_ADD_SETUP	0x0001
#define TCBCMD_CONFIGURE	0x0002
#define TCBCMD_MUL_ADD_SETUP	0x0003
#define TCBCMD_TRANSMIT		0x0004
#define TCBCMD_LD_MICROCODE	0x0005
#define TCBCMD_DUMP		0x0006
#define TCBCMD_DIAGNOSE		0x0007

#define TCBCMD_SUSPEND		0x4000

// TCB Status in TCB structure
#define TCBSTS_C		0x8000
#define TCBSTS_OK		0x2000

// SCB Command

#define SCBCMD_CU_NOP			0x00
#define SCBCMD_CU_START			0x10
#define SCBCMD_CU_RESUME		0x20
#define SCBCMD_CU_LOAD_COUNTER_ADD	0x40
#define SCBCMD_CU_DUMP_STAT_COUNTER	0x50
#define SCBCMD_CU_LOAD_BASE		0x60
#define SCBCMD_CU_DUMP_RESET_COUNTER	0x70
#define SCBCMD_CU_STATIC_RESUME		0xa0

#define SCBCMD_RU_NOP		0x00
#define SCBCMD_RU_START		0x01
#define SCBCMD_RU_RESUME	0x02
#define SCBCMD_RU_RDR		0x03
#define SCBCMD_RU_ABORT		0x04
#define SCBCMD_RU_LOAD_HDS	0x05
#define SCBCMD_RU_LOAD_BASE	0x06

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

#endif	// JOS_KERN_E100_H
