/**********************************************************************

 * file:  sr_router.c

 * date:  Mon Feb 18 12:50:42 PST 2002

 * Contact: casado@stanford.edu

 *

 * Description:

 *

 * This file contains all the functions that interact directly

 * with the routing table, as well as the main entry method

 * for routing.

 *

 **********************************************************************/



#include <stdio.h>

#include <assert.h>

#include <stdlib.h>

#include <string.h>



#include "sr_if.h"

#include "sr_rt.h"

#include "sr_router.h"

#include "sr_protocol.h"

#include "sr_arpcache.h"

#include "sr_utils.h"

#include "sr_dumper.h"



/*---------------------------------------------------------------------

 * Method: sr_init(void)

 * Scope:  Global

 *

 * Initialize the routing subsystem

 *

 *---------------------------------------------------------------------*/



void sr_init(struct sr_instance* sr)

{

    /* REQUIRES */

    assert(sr);



    /* Initialize cache and cache cleanup thread */

    sr_arpcache_init(&(sr->cache));



    pthread_attr_init(&(sr->attr));

    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);

    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);

    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);

    pthread_t thread;



    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

    

    /* Add initialization code here! */



} /* -- sr_init -- */



/*---------------------------------------------------------------------

 * Method: sr_handlepacket(uint8_t* p,char* interface)

 * Scope:  Global

 *

 * This method is called each time the router receives a packet on the

 * interface.  The packet buffer, the packet length and the receiving

 * interface are passed in as parameters. The packet is complete with

 * ethernet headers.

 *

 * Note: Both the packet buffer and the character's memory are handled

 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the

 * packet instead if you intend to keep it around beyond the scope of

 * the method call.

 *

 *---------------------------------------------------------------------*/



void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  printf("*** -> Received packet of length %d \n",len);

  /* Get the ethernet header from the packet */
  sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *) packet;

  /* Get the ethernet interface */
  struct sr_if *sr_ether_if = sr_get_interface(sr, interface);

  /* Check whether we found an interface corresponding to the name */
  if (sr_ether_if) {
	printf("Interface name: %s\n", sr_ether_if->name);
  } else {
	printf("Invalid interface found.\n");
	return;
  }

  /* Packet type check: IP, ARP, or neither */
  switch (ntohs(ether_hdr->ether_type)) {
	case ethertype_arp:
	
		/* ARP packet */

		printf("Received ARP packet\n");

		/* Check minimum length */
		if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t))) {
			printf("Invalid ARP Packet\n");
			return;
		}

		/* Get the ARP header from the packet */
		sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));

		/* Check to make sure we are handling Ethernet format */
		if (ntohs(arp_hdr->ar_hrd) != arp_hrd_ethernet) {
			printf ("Wrong hardware address format. Only Ethernet is supported.\n");
			return;
		}

		sr_handleARP(sr, ether_hdr, sr_ether_if, arp_hdr);
		
		break;
		
	case ethertype_ip:

		/* IP packet */

		printf("Received IP packet\n");

		printf("Length is %u\n", len);

		printf("Should be length %lu\n", (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));

		/* Minimum length */
		if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t))) {
			printf("Invalid IP Packet\n");
			return;
		}

		sr_ip_hdr_t *ip_packet_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));

		print_hdr_ip((uint8_t *)ip_packet_hdr);

        sr_handleIP(sr, ip_packet_hdr, len, ether_hdr, sr_ether_if);
		
        break;

    default:
	
		/* if it's neither, just ignore it */

		printf("Incorrect protocol type received: %u\n", (unsigned)ntohs(ether_hdr->ether_type));

        break;	
  }
}/* end sr_ForwardPacket */



/*---------------------------------------------------------------------

 * 

 * 					ETHERNET HEADER 

 *

 *---------------------------------------------------------------------*/

void set_eth_header(uint8_t *packet, uint8_t *ether_shost, uint8_t *ether_dhost, uint16_t type) {
	/* Sets the fields in the ethernet header */
	
	/* Set up the Ethernet header */
	sr_ethernet_hdr_t *ether_arp_reply = (sr_ethernet_hdr_t *)packet;

	printf("ETHER ADDR %lu, memcopied %lu", ETHER_ADDR_LEN, (sizeof(uint8_t) * ETHER_ADDR_LEN));
	/* note: uint8_t is not 1 bit so use the size */
	memcpy(ether_arp_reply->ether_shost, ether_shost, ETHER_ADDR_LEN); /* dest ethernet address */
	memcpy(ether_arp_reply->ether_dhost, ether_dhost, ETHER_ADDR_LEN); /* source ethernet address */
	ether_arp_reply->ether_type = htons(type); /* packet type */

}





/*---------------------------------------------------------------------

 * 

 * 					INTERNET PROTOCOL

 *

 *---------------------------------------------------------------------*/



 void sr_handleIP(struct sr_instance* sr, sr_ip_hdr_t *ip_packet_hdr, unsigned int len, sr_ethernet_hdr_t *ether_hdr, struct sr_if *ether_if) {

	/* Handles IP packets */ 

	

    /* Checking validation: TTL, checksum*/

    

	/* TTL */

    if (ip_packet_hdr->ip_ttl <= 1){

        sr_send_icmp_packet(sr, ip_packet_hdr, ICMP_TIME_EXCEEDED, ICMP_TIME_EXCEEDED_CODE);

		return;

    }


	printf("CHECKSUM FOR IP\n");

    /* Checksum */
	if (!validate_checksum((uint8_t *)ip_packet_hdr, ip_packet_hdr->ip_hl*4, ethertype_ip)) {
		printf("INVALID IP\n");
		return;
	};

    /* Check destination */ 
    struct sr_if * local_interface = sr_search_interface_by_ip(sr, (ip_packet_hdr->ip_dst)); /*htons*/

    if (local_interface)
    {
		printf("FOUND LOCAL INTERFACE FOR THE IP ADDRESS\n");
        /* Destination is local interface */
        switch(ip_packet_hdr->ip_p)
        {				
			unsigned int icmp_len;
			uint8_t *icmp;
			
            case ip_protocol_icmp:
				/* ICMP is an echo request */
				printf ("ICMP ECHO REQUEST RECEIVED\n");
				/* Check length */
				
				if (len-sizeof(sr_ethernet_hdr_t) < (sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t))){
					perror("Invalid ICMP packet\n");
					return;
				}
				

				/* If echo */
				icmp_hdr_t *icmp_packet = (icmp_hdr_t *) ((uint8_t *)ip_packet_hdr + ip_packet_hdr->ip_hl*4);

				if(icmp_packet->icmp_type == ICMP_ECHO){
					sr_send_icmp_packet(sr, ip_packet_hdr, ICMP_ECHO, ICMP_ECHO);
				} else {
					sr_send_icmp_packet(sr, ip_packet_hdr, ICMP_DEST_UNREACHABLE, ICMP_DEST_PORT_UNREACHABLE_CODE);
				}

                break;

            default: ;

				/* Otherwise send dest unreachable */
				sr_send_icmp_packet(sr, ip_packet_hdr, ICMP_DEST_UNREACHABLE, ICMP_DEST_PORT_UNREACHABLE_CODE);
                break;
        }
    }
    else
    {
		/* Destination is elsewhere: forward packet */
		printf("FORWARDING IP PACKET\n");
		
        struct sr_rt *rt_node = sr_search_route_table(sr, ip_packet_hdr->ip_dst);
		if (rt_node)
		{
			/* Create new header */
			int len = sizeof(sr_ethernet_hdr_t) + ntohs(ip_packet_hdr->ip_len);
			uint8_t *buf = malloc(len);

			sr_ip_hdr_t *ip_hdr_fwd = (sr_ip_hdr_t *)(buf + sizeof(sr_ethernet_hdr_t));
			sr_ethernet_hdr_t *ether_hdr_fwd = (sr_ethernet_hdr_t *)buf;
			memcpy(ip_hdr_fwd, ip_packet_hdr, ntohs(ip_packet_hdr->ip_len));



			/* Decrement, calculate new checksum, forward */

			ip_hdr_fwd->ip_ttl--;
			
			/* Check if TTL is 0 after decrementing, in which case we drop
			the packet instead and send an icmp */
			
			if (ip_hdr_fwd->ip_ttl <= 0) {
				sr_send_icmp_packet(sr, ip_hdr_fwd, ICMP_TIME_EXCEEDED, ICMP_TIME_EXCEEDED_CODE);
				return;
			}

			ip_hdr_fwd->ip_sum = 0;
			ip_hdr_fwd->ip_sum = cksum(ip_hdr_fwd, ip_hdr_fwd->ip_hl * 4);

			struct sr_arpentry *entry = sr_arpcache_lookup(&sr->cache, rt_node->gw.s_addr);
			struct sr_if *outgoing = sr_get_interface(sr, rt_node->interface);
		
			printf("Got our arp entry!\n");
			if (entry) {
				printf("Foward packet to the next hop!\n");
				
				set_eth_header((uint8_t *)ether_hdr_fwd, outgoing->addr, entry->mac, ethertype_ip);
				
				printf("Our completed packet is:");
				print_hdrs(buf, len);
				
				sr_send_packet(sr, buf, len, outgoing->name);
				free(buf);
				return;
			} else {
				printf("SENDING ARP REQUEST TO FIND IP->MAC MAPPING.\n");
				set_eth_header((uint8_t *)ether_hdr_fwd, outgoing->addr, (uint8_t *)EMPTY, ethertype_ip);
				printf("Our packet with dest empty is is:");
				print_hdrs(buf, len);
				
				struct sr_arpreq * req = sr_arpcache_queuereq(&sr->cache, rt_node->gw.s_addr, buf, len, rt_node->interface);
				handle_arpreq(sr, req);
			}
		}
        else
        {
			sr_send_icmp_packet(sr, ip_packet_hdr, ICMP_DEST_UNREACHABLE, ICMP_DEST_PORT_UNREACHABLE_CODE);

        }
    }
	return;
}



int sr_check_arp_send(struct sr_instance *sr,
	sr_ip_hdr_t * ip_packet,
	unsigned int len,
	struct sr_rt * rt_entry,
	char * interface)
{
	struct sr_if * local_interface = sr_get_interface(sr, interface);
	if (!local_interface) {
		perror("Invalid interface");
		return -1;
	}

	unsigned int frame_length = sizeof(sr_ethernet_hdr_t) + len;
	sr_ethernet_hdr_t * frame = (sr_ethernet_hdr_t *)malloc(frame_length);
	frame->ether_type = htons(ethertype_ip);
	memcpy((uint8_t *)frame + sizeof(sr_ethernet_hdr_t), ip_packet, len);
	memcpy(frame->ether_shost, local_interface->addr, ETHER_ADDR_LEN);

	uint32_t ip_to_arp = sr_search_interface_by_ip(sr, rt_entry->gw.s_addr) ?
		ip_packet->ip_dst : rt_entry->gw.s_addr;

	struct sr_arpentry * entry = sr_arpcache_lookup(
		&sr->cache, ip_to_arp);
	if (entry) {
		memcpy(frame->ether_dhost, entry->mac, ETHER_ADDR_LEN);
		free(entry);
		/*print_hdrs((uint8_t *)frame, frame_length);
		*/
		return sr_send_packet(sr, (uint8_t *)frame, frame_length, interface);
	}
	else {
		struct sr_arpreq * req = sr_arpcache_queuereq(&sr->cache,
			ip_packet->ip_dst, (uint8_t *)frame, frame_length, interface);
		handle_arpreq(sr, req);
		return 0;
	}
}

void set_ip_header(uint8_t *packet, unsigned int len, uint8_t protocol, uint32_t src, uint32_t dst) {
	/* Sets header info for IP*/
	
    sr_ip_hdr_t *ip_packet = (sr_ip_hdr_t *)packet;
	
	ip_packet->ip_tos = 0;
    ip_packet->ip_v = 4;
    ip_packet->ip_hl = sizeof(sr_ip_hdr_t)/4;
    ip_packet->ip_len = htons(sizeof(sr_ip_hdr_t) + len);
    ip_packet->ip_id = 0;
    ip_packet->ip_off = htons(IP_DF);
    ip_packet->ip_ttl = 64;
    ip_packet->ip_p = protocol;
    ip_packet->ip_src = src;
    ip_packet->ip_dst = dst;
	ip_packet->ip_sum = 0;
    ip_packet->ip_sum = htons(cksum(ip_packet, 20));
}



/*---------------------------------------------------------------------

 * 

 * 					ADDRESS RESOLUTION PROTOCOL

 *

 *---------------------------------------------------------------------*/


 void sr_handleARP(struct sr_instance* sr, sr_ethernet_hdr_t *ether_hdr, struct sr_if *sr_ether_if, sr_arp_hdr_t *arp_hdr) {

	/* Handles ARP requests and ARP replies */
	
	/* Opcode check: Request, reply, or neither */
	switch (ntohs(arp_hdr->ar_op)) {
		case arp_op_request: ;

			/* ARP request  */

			printf("ARP REQUEST\n");

			/* Check if the request is for this routers IP */
			struct sr_if *router_if = sr_search_interface_by_ip(sr, arp_hdr->ar_tip);

			/* Send a reply back to the sender IP address */
			if (router_if) {
				
				/*
				if (!sr_arpcache_lookup(&sr->cache, arp_hdr->ar_sip)) {
					sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip); 
				}
				*/ 
				
				printf("Sending a reply back to sender IP address\n");
				unsigned int len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
				uint8_t *packet = malloc(len);

				/* Set up Ethernet header */

				set_eth_header(packet, router_if->addr, ether_hdr->ether_shost, ethertype_arp);

				/* Set up the ARP header */

				set_arp_header(packet+sizeof(sr_ethernet_hdr_t), arp_op_reply, router_if->addr, router_if->ip, arp_hdr->ar_sha, arp_hdr->ar_sip);

				print_hdrs(packet, len);

				/* Send packet and free the packet from memory */
				if (sr_send_packet(sr, packet, len, router_if->name) == -1) {
					printf ("\n\n\nSENDING FAILED\n\n\n");
				}
				
				free(packet);
			}
			break;

		case arp_op_reply:

			/* ARP reply */
			
			printf("ARP reply to %lu\n", (unsigned long)arp_hdr->ar_sip);

			/* Queue the packet for this IP */

			struct sr_arpreq *cached;
			cached = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip);

			/*
			   # When servicing an arp reply that gives us an IP->MAC mapping
			   req = arpcache_insert(ip, mac)
			   if req:
				   send all packets on the req->packets linked list
				   arpreq_destroy(req)
			*/
			struct sr_packet * to_send_packet;
			
			if (cached) {
				for (to_send_packet = cached->packets; to_send_packet != NULL; to_send_packet = to_send_packet->next) {
					printf("\n\n????????SENDING FOR THE IP: \n");
					print_addr_ip_int(arp_hdr->ar_sip);
					/*
					uint8_t *buf = malloc(sizeof(uint8_t) * packet->len);

					sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *)buf;

					set_eth_header(buf, sr_ether_if->addr, arp_hdr->ar_sha, ethertype_ip);
					set_eth_header(buf, arp_hdr->ar_sip, arp_hdr->ar_sha, ethertype_ip); 
					print_hdr_eth(buf);

					*/
					sr_ethernet_hdr_t * ether_frame =
						(sr_ethernet_hdr_t *)to_send_packet->buf;			
					memcpy(ether_frame->ether_dhost, arp_hdr->ar_sha, ETHER_ADDR_LEN);
					print_hdr_eth((uint8_t *)ether_frame);
					if (sr_send_packet(sr, to_send_packet->buf, to_send_packet->len, to_send_packet->iface) == -1) {

						printf ("\n\n\nSENDING FAILED AT ARP REPLY\n\n\n");

					}

					

					/*free(buf);*/				

				}			

				sr_arpreq_destroy(&(sr->cache), cached);		

			}

		

			break;

		default:

			printf("Incorrect ARP opcode. Only ARP requests and replies are handled.\n");

	}
	return;
}



void set_arp_header(uint8_t *packet, unsigned short op, unsigned char *sha, uint32_t sip, unsigned char *tha, uint32_t tip) {

	/* Sets the fields in the arp header for arp packets */

	sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)packet;
	
	arp_hdr->ar_hrd = htons(arp_hrd_ethernet); /* hardware address */
	arp_hdr->ar_pro = htons(ethertype_ip); /* ethernet type */
	arp_hdr->ar_hln = ETHER_ADDR_LEN; /*len of hardware address */
	/* I'm not sure if this is the proper protocol address length?? */
	arp_hdr->ar_pln = 4; /* protocol address len */
	arp_hdr->ar_op =  htons(op); /* opcode */
	memcpy (arp_hdr->ar_sha, sha, ETHER_ADDR_LEN); /*sender hardware address */
	arp_hdr->ar_sip = sip; /* sender ip address */
	memcpy (arp_hdr->ar_tha, tha, ETHER_ADDR_LEN); /* target hardware address */
	arp_hdr->ar_tip = tip; /* target ip address	*/
}



void send_arp_request(struct sr_instance *sr, struct sr_arpreq *dest, struct sr_if *src) {
	/* Send an ARP request*/

	/* Send the ARP request to the Gateway. Has to have MAC address ff-ff-ff-ff (broadcast) */
	unsigned int len = sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t);
	uint8_t *packet = malloc(len);

	/* Set the ARP Header */
	set_arp_header(packet + sizeof(sr_ethernet_hdr_t), arp_op_request, src->addr, src->ip, (unsigned char *)BROADCAST, dest->ip); 
	/* given struct sr_rt *next_hop, next_hop->gw.s_addr */

	/* Set the Ethernet header */
	set_eth_header(packet, src->addr, (unsigned char *)BROADCAST, ethertype_arp);

	/* Send the packet */
	sr_send_packet(sr, packet, len, src->name);
	free(packet);
}



/*---------------------------------------------------------------------

 * 

 * 						ICMP HANDLING

 *

 *---------------------------------------------------------------------*/



int get_icmp_len(uint8_t type, uint8_t code, sr_ip_hdr_t *orig_ip_hdr) {

	/* Get the length of the ICMP given it's type and code since it

	differs for every type */
	
	printf("%lu is icmp3, %lu is icmp. Our result is %lu.\n",sizeof(sr_icmp_t3_hdr_t),
	sizeof(icmp_hdr_t), type == ICMP_DEST_UNREACHABLE ? sizeof(sr_icmp_t3_hdr_t) : sizeof(icmp_hdr_t));

	return type == ICMP_DEST_UNREACHABLE ? sizeof(sr_icmp_t3_hdr_t) : sizeof(icmp_hdr_t); 
}





void create_icmp(uint8_t *packet, uint8_t type, uint8_t code, sr_ip_hdr_t *orig_ip_hdr, unsigned int len){

	/* Creates the ICMP error based on code. 
	- Creates the IPv4 header, an 8-byte header
	- All types contain IP header
	- All types contain first 8 bytes of original datagram's data
	- Copies the old IP header into the ICMP data section
	- Performs a checksum
	*/

	sr_icmp_t3_hdr_t *icmp_packet;
	
	switch(type) 
	{
		case ICMP_DEST_UNREACHABLE: ;

			/* next-hop MTU is the size of the packet that's too large for the IP MTU on the router interface.
			Tell it to expect the original packet size*/

			/* Convert from default type header to type 3 header */

			icmp_packet = (sr_icmp_t3_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
		
			/* Set MTU */

			icmp_packet->next_mtu = orig_ip_hdr->ip_len;

			break;

		default: ;

			icmp_packet = (icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));

	}

	/* Set the common parts of the ICMP packet */

	icmp_packet->icmp_type = type;
	icmp_packet->icmp_code = code;
	
	/* Copy the first 8 bytes of the original datagram and the old IP header */

	memcpy(icmp_packet->data, (uint8_t *)orig_ip_hdr, 
		min(ICMP_DATA_SIZE, orig_ip_hdr->ip_len));

	/* Checksum */

	icmp_packet->icmp_sum = 0;
	icmp_packet->icmp_sum = cksum((uint8_t *)icmp_packet, len);
}



void sr_send_icmp_packet(struct sr_instance *sr, sr_ip_hdr_t * ip_packet_hdr, uint8_t icmp_type, uint8_t icmp_code) {

	/* Sends an ICMP packet */
	printf("Start sending icmp packet.\n");

	struct sr_rt * route = sr_search_route_table(sr, ip_packet_hdr->ip_src);

	if(route) {
		struct sr_if * local_if = sr_get_interface(sr, route->interface);
		
		printf("Got our interface!\n");

		if (!local_if) {
			perror("Invalid interface");
			return;
		}

		unsigned int icmp_len;
		unsigned int len;
		uint8_t *icmp;
		
		printf ("We are trying to send an ICMP message of type: %u", icmp_type);

        switch(icmp_type)
		{
            case ICMP_ECHO: ; 
			
				printf ("Checking our received ICMP header.\n");

				/* Get the ICMP header we received */
				icmp_hdr_t *icmp_hdr = (icmp_hdr_t *) ((uint8_t *)ip_packet_hdr + ip_packet_hdr->ip_hl*4);

				/* Get the ICMP length*/
				icmp_len = get_icmp_len(icmp_type, icmp_code, ip_packet_hdr);

				/* Check the ICMP checksum as well */
				if (!validate_checksum((uint8_t *)icmp_hdr, icmp_len, ip_protocol_icmp)) {
					printf("INVALID ICMP\n");
					return;
				}
				
				printf ("All good. Creating a reply.\n");

				/* Create ICMP reply*/
				len = icmp_len + sizeof(sr_ethernet_hdr_t) +  sizeof(sr_ip_hdr_t);
				icmp = malloc(len); /*allocate memory*/

				icmp_hdr_t *icmp_hdr_reply = (icmp_hdr_t *)(icmp + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)); /*create ICMP reply*/

				/* Set the Ethernet header information */
				/*
				set_eth_header(icmp, ether_hdr->ether_dhost, ether_hdr->ether_shost, ethertype_ip);
				*/

				/* Input all IP information here*/

				/*
				set_ip_header(icmp + sizeof(sr_ethernet_hdr_t), icmp_len, ip_protocol_icmp, ip_packet_hdr->ip_src, ip_packet_hdr->ip_dst);
				*/
				
				set_ip_header(icmp + sizeof(sr_ethernet_hdr_t), icmp_len, ip_protocol_icmp, local_if->ip, ip_packet_hdr->ip_src);
				

				/* Set the ICMP header information and change the ICMP to reply,

				echo also has no code but we'll set to 0 by default*/

				create_icmp((uint8_t *)icmp_hdr_reply, icmp_type, icmp_code, ip_packet_hdr, icmp_len);

				

				/* Send the ICMP reply back */
				/*
				sr_send_packet(sr, icmp, icmp_reply_len, ether_if->name);
				
				free(icmp);
				*/
				

                break;

            default: ;

				/* Otherwise handle DEST UNREACHABLE and TIME EXCEEDED the same way*/
				
				printf("Creating an ICMP(dest unreachable or time exceeded)\n");

				icmp_len = get_icmp_len(icmp_type, icmp_code, ip_packet_hdr);
				printf("%ud is icmp len but we need,", icmp_len);
				printf("%lu len.,", sizeof(sr_icmp_t3_hdr_t));
				printf ("Wtf is going on here\n");
				printf("We need %ud total len.", icmp_len + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
				len = icmp_len + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t);
				printf ("Whatttttttt\n");
				printf("We need %ud icmp len.", icmp_len);
				printf("We need %ud len.", len);
				icmp = malloc(len);

				
				printf("We need %ud memory.", len);
				/* Set the Ethernet header information 

				set_eth_header(icmp, ether_hdr->ether_dhost, ether_hdr->ether_shost, ethertype_ip);
				*/
				
				printf("Set our ethernet header!\n");

				/* Set IP information 

				set_ip_header(icmp + sizeof(sr_ethernet_hdr_t), icmp_len, ip_protocol_icmp, ip_packet_hdr->ip_src, ip_packet_hdr->ip_dst);
				*/
				
				set_ip_header(icmp + sizeof(sr_ethernet_hdr_t), icmp_len, ip_protocol_icmp, local_if->ip, ip_packet_hdr->ip_src);
				
				

				/* Set ICMP information */

				create_icmp(icmp, icmp_type, icmp_code, ip_packet_hdr, icmp_len);

				

				/* Send the ICMP reply back */
				/*
				sr_send_packet(sr, icmp, len, ether_if->name);

				free(icmp);
				*/
				

                break;

        }

		
		/*
        struct sr_rt *entry = sr_search_route_table(sr, ip_packet_hdr->ip_src);
		*/
		printf("Searching for our entry!\n");
		
		struct sr_arpentry *entry = sr_arpcache_lookup(&sr->cache, route->gw.s_addr);
		
		printf("Got our arp entry!\n");
        if (entry) {
			printf("Foward packet to the next hop!\n");
			set_eth_header(icmp, local_if->addr, entry->mac, ethertype_ip);
			printf("Our completed ICMP packet is:");
			print_hdrs(icmp, len);
			
			sr_send_packet(sr, icmp, len, local_if->name);
			free(icmp);
			return;
        } else {
			printf("SENDING ARP REQUEST TO FIND IP->MAC MAPPING.\n");
			set_eth_header(icmp, local_if->addr, (uint8_t *)EMPTY, ethertype_ip);
			printf("Our packet with dest empty is is:");
			print_hdrs(icmp, len);
			
			struct sr_arpreq * req = sr_arpcache_queuereq(&sr->cache, route->gw.s_addr, icmp, len, local_if->name);
			handle_arpreq(sr, req);
		}
		/*return sr_check_arp_send(sr, (sr_ip_hdr_t *)icmp+sizeof(sr_ethernet_hdr_t), len, entry, entry->interface); */
    }

    return;

}





/*---------------------------------------------------------------------

 * 

 * 					SR UTILITY FUNCTIONS 

 *

 *---------------------------------------------------------------------*/



struct sr_if * sr_search_interface_by_ip(struct sr_instance *sr, uint32_t ip)

{

	/* Find the interface the IP address corresponds to*/

	struct sr_if *interface;

	

	printf ("SEARCH INTERFACE BY IP\n");

	print_addr_ip_int(ip);

	

	for (interface = sr->if_list; interface != NULL; interface = interface->next) {

		if (interface->ip == (ip)) { 

			break;

		}

	}

	

	return interface;

}



struct sr_rt * sr_search_route_table(struct sr_instance *sr,uint32_t ip)

{

	/* Searches the routing table for the node containing the IP address */

    struct sr_rt * entry = sr->routing_table;

    struct sr_rt * match = 0;

    while(entry){

        if((entry->dest.s_addr & entry->mask.s_addr) == (ip & entry->mask.s_addr)){

            if(! match || entry->mask.s_addr > match->mask.s_addr){

            match = entry;

            }

        }

        entry = entry->next;

    }

    return match;

}





/*---------------------------------------------------------------------

 * 

 * 					OTHER UTILITIES

 *

 *---------------------------------------------------------------------*/

 

 int validate_checksum(uint8_t *buf, unsigned int len, uint16_t protocol) {

	/* Validate checksum */
	
	uint8_t *packet = malloc(len);
	memcpy(packet, buf, len);
	unsigned int result = 0;

	switch(protocol){
		case ethertype_ip: ;
			sr_ip_hdr_t *ip_packet = (sr_ip_hdr_t *)packet;
			ip_packet->ip_sum = 0;
			result = (((sr_ip_hdr_t *)buf)->ip_sum != cksum(ip_packet, len)) ? 0:1;
			break;

		case ip_protocol_icmp: ;
			icmp_hdr_t *icmp_packet = (icmp_hdr_t *)packet;
			icmp_packet->icmp_sum = 0;
			result = (((icmp_hdr_t *)icmp_packet)->icmp_sum != cksum(icmp_packet, len)) ? 0:1;
			break;
	}

	free (packet);
	return result;
 }
