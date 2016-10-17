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
  
  /* Check if packet is smaller than it should be */
  if (sizeof(packet)/sizeof(uint8_t) != len) {
	printf("Received a corrupted packet.\n");
	return;
  } 
  
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
			printf("Invalid IP Packet\n");
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
		
		/* Minimum length */
		if(len < (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t))) {
			printf("Invalid IP Packet\n");
			return;
		}
  
        sr_handleIP(sr, packet, len, interface, ether_hdr, sr_ether_if);
        break;
    
    default:
		/* if it's neither, just ignore it */
		printf("Incorrect protocol type received: %u\n", (unsigned)ether_hdr->ether_type);
        break;	
  }

}/* end sr_ForwardPacket */

/*---------------------------------------------------------------------
 * 
 * 					ETHERNET HEADER 
 *
 *---------------------------------------------------------------------*/

void set_eth_header(uint8_t *packet, uint8_t *ether_shost, uint8_t *ether_dhost) {
	/* Sets the fields in the ethernet header */
	
	/* Set up the Ethernet header */
	sr_ethernet_hdr_t *ether_arp_reply = (sr_ethernet_hdr_t *)packet;
	
	/* note: uint8_t is not 1 bit so use the size */
	memcpy(ether_arp_reply->ether_dhost, ether_shost, (sizeof(uint8_t) * ETHER_ADDR_LEN)); /* dest ethernet address */
	memcpy(ether_arp_reply->ether_shost, ether_dhost, (sizeof(uint8_t) * ETHER_ADDR_LEN)); /* source ethernet address */
	ether_arp_reply->ether_type = htons(ethertype_arp); /* packet type */
}


/*---------------------------------------------------------------------
 * 
 * 					INTERNET PROTOCOL
 *
 *---------------------------------------------------------------------*/

 void sr_handleIP(struct sr_instance* sr, uint8_t *packet, unsigned int len, 
        char * interface, sr_ethernet_hdr_t *ether_hdr, struct sr_if *sr_ether_if) {
	/* Handles IP packets */ 
	
    /* Checking validation: TTL, checksum*/
	
	/* Retrieve IP header */
    sr_ip_hdr_t * ip_packet_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    
	/* TTL */
	/* Note: don't need to convert ntohs here*/
    if (ip_packet_hdr->ip_ttl <= 1){
		/* 
        sr_send_icmp_packet(sr, (uint8_t *)ip_packet_hdr, ip_packet_hdr->ip_src, icmp_time_exceed, 0);
        */
		
		return;
    }
	
    /* Checksum */
	/*
    uint8_t *data=(uint8_t *)(ip_packet_hdr+sizof(sr_ip_hdr_t));
    uint16_t compute_cksum=cksum(data, ip_packet_hdr->ip_hl*4); */
	/* Note: checksum only refers to the IP header*/
    if(ntohs(ip_packet_hdr->ip_sum) != cksum(ip_packet, ip_packet_hdr->ip_hl*4)
    {
        printf("Invalid IP Packet\n");
        return;
    }
    
    /* Check destination */ 
    struct sr_if * local_interface = sr_search_interface_by_ip(sr, ip_packet_hdr->ip_dst);
    if (local_interface)
    {
        /* Destination is local interface */
        switch(ip_packet_hdr->ip_p)
        {
            case ip_protocol_icmp:
				/* ICMP is an echo request */
				/*
				void sr_handle_icmp(struct sr_instance * sr,
                uint8_t * packet,
                unsigned int len,
                char * interface)
				{
					if(len < sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)){
						perror("Invalid icmp packet\n");
						return;
					}
					sr_ip_hdr_t * ip_packet = (sr_ip_hdr_t *)packet;
					sr_icmp_hdr_t * icmp_packet = (sr_icmp_hdr_t *)(packet + sizeof(sr_ip_hdr_t));
					if(icmp_packet->icmp_type == icmp_echo_request){
						sr_send_icmp_packet(sr, (uint8_t *)ip_packet, ip_packet->ip_src,icmp_echo_reply, 0);
					}
					return;
				}
				*/
                sr_handle_icmp(sr, ip_packet_hdr,len-sizeof(sr_ethernet_hdr_t,interface));
				
				printf ("ICMP ECHO RECEIVED\n");
				
				icmp_hdr_t *icmp_hdr = (icmp_hdr_t *) (ip_packet_hdr + ip_packet_hdr->ip_hl*4);
				/* ICMP length =  */
				unsigned int icmp_len = ntohs(ip_packet_hdr->ip_len) - (ip_packet_hdr->ip_hl*4);
				
				
                break;
            default:
                sr_send_icmp_packet(sr, (unit8_t *)ip_packet_hdr, 
                        ip_packet_hdr->ip_src, icmp_destination_unreachalble,3);
                break;
        }
    }
    else
    {
		/*
        //destination is elsewhere: forward packet
        struc sr_rt *entry = sr_search_route_table(sr, ip_packet_hdr->ip_dst);
        if(entry)
        {
            ip_packet_hdr->ip_ttl -=1;
            ip_packet_hdr->ip_sum = 0;
            ip_packet_hdr->ip_sum = cksum(ip_packet_hdr, ip_packet_hdr->ip_hl*4);
            sr_check_arp_send(sr, ip_packet_hdr, len-sizeof(sr_ethernet_hdr_t),
                    entry, entry->interface);
        }
        else
        {
            sr_send_icmp_packet(sr, (uint8_t *)ip_packet_hdr,
                    ip_packet_hdr->ip_src, icmp_destination_unreachalble, 0);
        }
		*/
		return;
    }

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
			
			/* Check if the request is for this routers IP */
			struct sr_if *router_if = sr_search_interface_by_ip(sr, arp_hdr->ar_tip);
			
			/* Send a reply back to the sender IP address */
			if (router_if) {
				unsigned int len = sizeof(sr_ethernet_hdr_t *) + sizeof(sr_arp_hdr_t *);
				uint8_t *packet = malloc(sizeof(uint8_t) * len);
				
				/* Set up reply with proper information */
				set_eth_header(packet, ether_hdr->ether_shost, ether_hdr->ether_dhost);
				
				/* Set up the ARP header */
				set_arp_header(packet+sizeof(sr_ethernet_hdr_t), arp_op_reply, router_if->addr, router_if->ip, arp_hdr->ar_sha, arp_hdr->ar_sip);
								
				/* Send packet and free the packet from memory */
				sr_send_packet(sr, packet, len, router_if->name);
				free(packet);
				
			}
			
			break;
		case arp_op_reply:
			/* ARP reply */
			
			printf("ARP reply to %lu\n", (unsigned long)arp_hdr->ar_sip);
			
			/* Queue the packet for this IP */
			struct sr_arpreq *cached;
			cached = sr_arpcache_insert(&sr->cache, arp_hdr->ar_sha, arp_hdr->ar_sip);
		
			break;
		default:
			printf("Incorrect ARP opcode. Only ARP requests and replies are handled.\n");
	}
}

void set_arp_header(uint8_t *packet, unsigned short op, unsigned char *sha, uint32_t sip, unsigned char *tha, uint32_t tip) {
	/* Sets the fields in the arp header for arp packets */
	
	sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)packet;
	
	arp_hdr->ar_hrd = htons(arp_hrd_ethernet); /* hardware address */
	arp_hdr->ar_pro = htons(ethertype_arp); /* ethernet type */
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

	/* Set the Ethernet header */
	set_eth_header(packet, src->addr, (unsigned char *)BROADCAST);
	
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
	
	unsigned int icmp_pack_len;
	unsigned int ip_pack_len = min(orig_ip_hdr->ip_len - (orig_ip_hdr->ip_hl * 4), 8) + 
								(orig_ip_hdr->ip_hl * 4);
	
	switch(type)
	{
		case ICMP_DEST_UNREACHABLE:
			icmp_pack_len = sizeof(sr_icmp_t3_hdr_t);
			break;
		default: 
			/* Use the default ICMP header for the rest*/
			icmp_pack_len = sizeof(icmp_hdr_t);              
			break;
	}
	
	return icmp_pack_len + ip_pack_len;
	
}


void create_icmp(uint8_t *packet, uint8_t type, uint8_t code, sr_ip_hdr_t *orig_ip_hdr, unsigned int len){
	/* Creates the ICMP error based on code. 
	- Creates the IPv4 header, an 8-byte header
	- All types contain IP header
	- All types contain first 8 bytes of original datagram's data
	- Copies the old IP header into the ICMP data section
	- Performs a checksum
	*/
		
	/* Set the proper ICMP header and data */
	
	icmp_hdr_t *icmp_packet = (icmp_hdr_t *)(packet + 
			sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
			
	/* Set the common parts of the ICMP packet */
	icmp_packet->icmp_type = type;
	icmp_packet->icmp_code = code;
	
	switch(type)
	{
		case ICMP_DEST_UNREACHABLE: ;
			/* next-hop MTU is the size of the packet that's too large for the IP MTU
			on the router interface.
			Tell it to expect the original packet size*/
			/* Convert from default type header to type 3 header */
			sr_icmp_t3_hdr_t *icmp3_packet = (sr_icmp_t3_hdr_t *)(packet + 
					sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
					
			/* Set MTU */
			icmp3_packet->next_mtu = orig_ip_hdr->ip_len;
			
			/* Copy the first 8 bytes of the original datagram and the old IP header */
			memcpy(icmp3_packet->data, (uint8_t *)orig_ip_hdr, 
				min(ICMP_DATA_SIZE, orig_ip_hdr->ip_len));
				
			/* Checksum */
			icmp3_packet->icmp_sum = cksum((uint8_t *)icmp3_packet, len);
			break;
		default:
			/* Copy the first 8 bytes of the original datagram and the old IP header */
			memcpy(icmp_packet->data, (uint8_t *)orig_ip_hdr, 
				min(ICMP_DATA_SIZE, orig_ip_hdr->ip_len));
				
			/* Checksum */
			icmp_packet->icmp_sum = cksum((uint8_t *)icmp_packet, len);
	}
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
	
	for (interface = sr->if_list; interface != NULL; interface = interface->next) {
		if (interface->ip == ip) {
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