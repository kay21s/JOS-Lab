#include "ns.h"
#include <inc/lib.h>

#define PKTMAP 0x10000000

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	
	struct jif_pkt *pkt = (struct jif_pkt *)PKTMAP;
	int pktlen, r;

	cprintf("Hey, I am INPUT, %x\n", sys_getenvid());


	// We POLL here
	while (1) {
		if ((r = sys_page_alloc(0, pkt, PTE_U|PTE_P|PTE_W)) < 0)
			panic("[%x]:Sys_page_alloc failed in Input env!", sys_getenvid());

		pktlen = sys_receive_packet(pkt->jp_data);
		pkt->jp_len = pktlen;

		if (pktlen < 0) {
			cprintf("INPUT: Error in receiving packets\n");
		} else if (pktlen == 0) {
			// Still do not have packets yet, give out the CPU
			// and wait for the next opportunity to get the packet
			sys_yield();
		} else if (pktlen > 0) {
			cprintf("INPUT: PACKET RECEIVED: Len = %d\n", pktlen);
			ipc_send(ns_envid, NSREQ_INPUT, pkt, PTE_P|PTE_U|PTE_W);
		}

		if ((r = sys_page_unmap(0, pkt)) < 0)
			panic("[%x]:Sys_page_unmap failed in Input env!", sys_getenvid());
	}

}
