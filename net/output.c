#include "ns.h"
#include "inc/lib.h"

#define PKTMAP		0x10000000

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	
	uint32_t req, whom;
	int perm;
	struct jif_pkt *pkt;

	while (1) {
		perm = 0;
		req = ipc_recv((int32_t *) &whom, (void *) PKTMAP, &perm);

		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		switch (req) {
		case NSREQ_OUTPUT:
			pkt = (struct jif_pkt *) PKTMAP;
			sys_transmit_packet((void *)pkt->jp_data, (uint32_t)pkt->jp_len);
			break;
		default:
			cprintf("Invalid request code %d from %08x\n", whom, req);
		}

		sys_page_unmap(0, (void*) PKTMAP);
	}
}
