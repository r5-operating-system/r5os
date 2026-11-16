/*
 * Copyright 2008, Oliver Ruiz Dorantes. All rights reserved.
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef L2CAP_ENDPOINT_H
#define L2CAP_ENDPOINT_H


#include <lock.h>
#include <condition_variable.h>
#include <util/AVLTree.h>

#include <net_protocol.h>
#include <net_socket.h>
#include <ProtocolUtilities.h>

#include <bluetooth/L2CAP/btL2CAP.h>
#include "l2cap_internal.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "l2cap_parse.h"
// SPDX-SnippetEnd


class L2capEndpoint;


class L2capEndpoint : public net_protocol, public ProtocolSocket, public AVLTreeNode {
public:
						L2capEndpoint(net_socket* socket);
	virtual				~L2capEndpoint();

			status_t	Open();
			status_t	Shutdown();
			status_t	Close();
			status_t	Free();

			status_t	Bind(const struct sockaddr* _address);
			status_t	Unbind();
			status_t	Listen(int backlog);
			status_t	Connect(const struct sockaddr* address);
			status_t	Accept(net_socket** _acceptedSocket);

			uint16		ChannelID() const { return fChannelID; }
			uint16		DestinationChannelID() const { return fDestinationChannelID; }

			ssize_t		ReadData(size_t numBytes, uint32 flags, net_buffer** _buffer);
			status_t	SendData(net_buffer* buffer);
			status_t	ReceiveData(net_buffer* buffer);
			ssize_t		Sendable();
			ssize_t		Receivable();

public:
			void		_HandleCommandRejected(uint8 ident, uint16 reason,
							const l2cap_command_reject_data& data);
			void 		_HandleConnectionReq(HciConnection* connection,
							uint8 ident, uint16 psm, uint16 scid);
			void		_HandleConnectionRsp(uint8 ident, const l2cap_connection_rsp& response);
			void		_HandleConfigurationReq(uint8 ident, uint16 flags,
							uint16* mtu, uint16* flush_timeout, l2cap_qos* flow);
			void		_HandleConfigurationRsp(uint8 ident, uint16 scid, uint16 flags,
							uint16 result, uint16* mtu, uint16* flush_timeout, l2cap_qos* flow);
			void		_HandleDisconnectionReq(uint8 ident, uint16 scid);
			void		_HandleDisconnectionRsp(uint8 ident, uint16 dcid, uint16 scid);
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// An answer to our command that cannot be used (too short,
			// truncated, unknown options): close and wake the waiter.
			void		_HandleBadResponse(uint8 ident);
			// HCI link gone (L2capEndpointManager::Disconnected()).
			void		_HandleLinkLost();

			// Bluetooth Low Energy (docs/BLUETOOTH.md, steps 1c and 6;
			// L2capEndpointLE.cpp). A fixed channel (ATT, SMP) or an LE
			// credit-based channel; see <R5BluetoothLE.h>.
			bool		IsLECredit() const { return fLEMode != 0; }
			uint8		LEMode() const { return fLEMode; }
			uint16		FixedChannel() const { return fFixedChannel; }
			HciConnection* Connection() const { return fConnection; }
			status_t	_BindLE(const sockaddr* address);
			status_t	_ConnectLE(const sockaddr* address);
			status_t	_SendFixed(net_buffer* buffer);
			status_t	_ReceiveLECredit(net_buffer* buffer);
			void		_SendLECreditQueued();
			// On the listening endpoint: a pending child for an incoming
			// channel, open once the answer is sent (_MarkEstablished()).
			// Returns the result code of the answer (Part A, 4.23).
			uint16		_SpawnLECredit(HciConnection* connection, uint8 mode,
							uint16 scid, uint16 mtu, uint16 mps,
							uint16 credits, L2capEndpoint** _child);
			void		_FinishLECreditSpawn(bool accepted);
			void		_HandleLECreditConnectionRsp(uint8 ident, uint16 dcid,
							uint16 mtu, uint16 mps, uint16 credits,
							uint16 result);
			void		_HandleLECredits(uint16 credits);
			// Checks (apply false) or applies a reconfiguration; returns
			// the result code (4.28).
			uint16		_HandleLEReconfigure(uint16 mtu, uint16 mps,
							bool apply);
			// Closes a credit-based channel after a protocol error, and
			// gives the remote new credits while there is room.
			void		_LEDisconnect();
			void		_LEReturnCredits();
			// Our side of a credit-based channel, for answers.
			uint16		LEIncomingMTU() const
							{ return fChannelConfig.incoming_mtu; }
			uint16		LEIncomingMPS() const { return fLERx.mps; }
			uint16		LEIncomingCredits() const { return fLERxCredits; }
			// SPDX-SnippetEnd

private:
	inline bool
	_IsEstablishing()
	{
		return fState > LISTEN && fState < OPEN;
	}

			status_t	_WaitForStateChange(bigtime_t absoluteTimeout);

	static	void		_SendTimer(net_timer* timer, void* _endpoint);
			void		_SendQueued();

			void		_SendChannelConfig();
			status_t	_MarkEstablished();
			void		_MarkClosed();

private:
	friend class L2capEndpointManager;

	sem_id			fAcceptSemaphore;
	mutex			fLock;

	enum channel_status {
		CLOSED,
		BOUND,
		LISTEN,

		WAIT_FOR_CONNECTION_RSP,
		CONFIGURATION,
		OPEN,
		WAIT_FOR_DISCONNECTION_RSP,
		RECEIVED_DISCONNECTION_REQ,
	} fState;

	HciConnection*	fConnection;

	ConditionVariable fCommandWait;

	uint16			fChannelID, fDestinationChannelID;

	net_fifo		fReceiveQueue, fSendQueue;
	net_timer		fSendTimer;

	struct ConfigState {
		enum : uint8 {
			NONE,
			SENT,
			DONE,
		} in, out;
	} fConfigState;
	struct ChannelConfiguration {
		uint16		incoming_mtu;
		l2cap_qos	incoming_flow;
		uint16		outgoing_mtu;
		l2cap_qos	outgoing_flow;

		uint16		flush_timeout;
		uint16		link_timeout;
	} fChannelConfig;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// LE: fFixedChannel is the CID of a fixed-channel socket; fLEMode is
	// R5_L2CAP_MODE_* of a credit-based channel (0: neither, BR/EDR).
	uint16			fFixedChannel;
	uint8			fLEMode;
	bool			fLEBound;		// in the LE PSM tree
	uint16			fLETxCredits;
	uint16			fLETxMPS;
	net_buffer*		fLETxSDU;		// the rest of the SDU being sent
	bool			fLETxFirst;		// its next frame is its first
	uint16			fLETxSDULength;
	uint16			fLERxCredits;	// what the remote may still send
	l2cap_coc_rx	fLERx;
	net_buffer*		fLERxSDU;		// the SDU being received
	// SPDX-SnippetEnd
};


#endif	// L2CAP_ENDPOINT_H
