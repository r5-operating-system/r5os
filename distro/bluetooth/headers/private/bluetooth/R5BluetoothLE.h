/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Low Energy between the kernel (btCoreData, l2cap) and
 * bluetooth_server (docs/BLUETOOTH.md, steps 1c and 6).
 *
 * An LE link is an HciConnection with type R5_HCI_LINK_LE. Its address in
 * received buffers is a sockaddr_l2cap_le, which carries the address type
 * the plain sockaddr_l2cap has no room for.
 *
 * The L2CAP socket (AF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP)
 * takes a sockaddr_l2cap_le in three ways:
 *
 * - Fixed channel: bind() with l2cap_cid R5_L2CAP_CID_ATT or
 *   R5_L2CAP_CID_SMP and BDADDR_ANY. Only uid 0 may bind one, and only one
 *   socket per channel. recvfrom() gives each PDU of that channel from
 *   every LE link, with the peer's sockaddr_l2cap_le; sendto() with that
 *   address sends one PDU on its link.
 * - LE credit-based channel, listening: bind() with l2cap_psm (an LE PSM,
 *   1 to 0xFF), l2cap_cid 0, then listen() and accept().
 * - LE credit-based channel, connecting: connect() with the peer's
 *   address, l2cap_psm and l2cap_mode R5_L2CAP_MODE_LE_CREDIT or
 *   R5_L2CAP_MODE_ENHANCED_CREDIT (EATT uses the second on PSM 0x27).
 *   The LE link must already be up (bluetooth_server makes it).
 * One send() is one SDU; the kernel splits it into K-frames by credits.
 */
#ifndef _R5_BLUETOOTH_LE_H
#define _R5_BLUETOOTH_LE_H


#include <bluetooth/bluetooth.h>


// HciConnection::type of an LE link; the BR/EDR links use bt_packet_t
// values (BT_ACL), which stay below 0x10.
#define R5_HCI_LINK_LE					0x10

// Address types (Core 6.0, Vol 4, Part E, 7.8.12): l2cap_bdaddr_type.
#define R5_BDADDR_BREDR					0x00
#define R5_BDADDR_LE_PUBLIC				0x01
#define R5_BDADDR_LE_RANDOM				0x02

// LE fixed channels (Vol 3, Part A, 2.1).
#define R5_L2CAP_CID_ATT				0x0004
#define R5_L2CAP_CID_LE_SIGNALING		0x0005
#define R5_L2CAP_CID_SMP				0x0006

// Dynamic LE channels are 0x0040 to 0x007F (Vol 3, Part A, 2.1).
#define R5_L2CAP_LE_FIRST_CID			0x0040
#define R5_L2CAP_LE_LAST_CID			0x007F

// The PSM of EATT (Assigned Numbers, 3.1).
#define R5_L2CAP_PSM_EATT				0x0027

// l2cap_mode for connect().
#define R5_L2CAP_MODE_LE_CREDIT			0x01	// LE Credit Based (0x14)
#define R5_L2CAP_MODE_ENHANCED_CREDIT	0x02	// Enhanced Credit Based (0x17)


struct sockaddr_l2cap_le {
	uint8		l2cap_len;			// sizeof(struct sockaddr_l2cap_le)
	uint8		l2cap_family;		// AF_BLUETOOTH
	uint16		l2cap_psm;			// LE PSM, or 0 for a fixed channel
	bdaddr_t	l2cap_bdaddr;
	uint16		l2cap_cid;			// fixed channel, or 0
	uint8		l2cap_bdaddr_type;	// R5_BDADDR_*
	uint8		l2cap_mode;			// R5_L2CAP_MODE_*, connect() only
};


#endif	// _R5_BLUETOOTH_LE_H
