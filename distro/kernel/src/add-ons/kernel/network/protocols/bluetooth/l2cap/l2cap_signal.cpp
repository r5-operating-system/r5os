/*
 * Copyright 2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <string.h>

#include <net/if_dl.h>
#include <net_datalink.h>
#include <NetBufferUtilities.h>

#include <l2cap.h>
#include "L2capEndpoint.h"
#include "L2capEndpointManager.h"
#include "l2cap_internal.h"
#include "l2cap_signal.h"
#include "l2cap_command.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "l2cap_parse.h"

#include <stdlib.h>

#include <R5BluetoothLE.h>
// SPDX-SnippetEnd

#include <btDebug.h>

#include <bluetooth/L2CAP/btL2CAP.h>
#include <bluetooth/HCI/btHCI_command.h>


struct l2cap_config_options {
	uint16 mtu;
	bool mtu_set;
	uint16 flush_timeout;
	bool flush_timeout_set;
	l2cap_qos qos;
	bool qos_set;
	net_buffer* rejected;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// An option ran past the command, or there was no memory to look.
	bool malformed;
	// SPDX-SnippetEnd
};


// #pragma mark - incoming signals


static status_t
l2cap_handle_connection_req(HciConnection* conn, uint8 ident, net_buffer* buffer)
{
	NetBufferHeaderReader<l2cap_connection_req> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 psm = le16toh(command->psm);
	const uint16 scid = le16toh(command->scid);

	L2capEndpoint* endpoint = gL2capEndpointManager.GetForPSM(psm);
	if (endpoint == NULL) {
		// Refuse connection.
		send_l2cap_connection_rsp(conn, ident, 0, scid,
			l2cap_connection_rsp::RESULT_PSM_NOT_SUPPORTED, 0);
		return B_OK;
	}

	endpoint->_HandleConnectionReq(conn, ident, psm, scid);
	gSocketModule->release_socket(endpoint->socket);
	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Responses carrying a Source CID (Connection, Configuration, Disconnection)
// identify our local channel by scid. If the ident lookup returned NULL or a
// different endpoint, deliver to the endpoint whose ChannelID() equals scid
// on this link and preserve the other endpoint's ident.
static L2capEndpoint*
resolve_response_endpoint(HciConnection* conn, L2capEndpoint* identEndpoint,
	uint16 scid, bool& releaseIdent, bool& releaseTarget)
{
	releaseTarget = false;
	if (identEndpoint != NULL && identEndpoint->ChannelID() == scid)
		return identEndpoint;

	if (identEndpoint != NULL)
		releaseIdent = false;

	L2capEndpoint* byChannel = gL2capEndpointManager.GetForChannel(scid);
	if (byChannel != NULL) {
		if (byChannel->Connection() != NULL && byChannel->Connection() != conn) {
			gSocketModule->release_socket(byChannel->socket);
			return NULL;
		}
		releaseTarget = true;
	}
	return byChannel;
}
// SPDX-SnippetEnd


static void
l2cap_handle_connection_rsp(HciConnection* conn, L2capEndpoint* endpoint,
	uint8 ident, net_buffer* buffer, bool& releaseIdent)
{
	NetBufferHeaderReader<l2cap_connection_rsp> command(buffer);
	if (command.Status() != B_OK)
		return;

	l2cap_connection_rsp response;
	response.dcid = le16toh(command->dcid);
	response.scid = le16toh(command->scid);
	response.result = le16toh(command->result);
	response.status = le16toh(command->status);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	bool releaseTarget = false;
	L2capEndpoint* target = resolve_response_endpoint(conn, endpoint,
		response.scid, releaseIdent, releaseTarget);
	if (target == NULL) {
		ERROR("l2cap: unexpected connection response: ident %d scid %d unknown\n",
			ident, response.scid);
		return;
	}
	// SPDX-SnippetEnd

	TRACE("%s: dcid=%d scid=%d result=%d status%d\n",
		__func__, response.dcid, response.scid, response.result, response.status);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A second Connection Response with the same identifier follows.
	if (response.result == l2cap_connection_rsp::RESULT_PENDING)
		releaseIdent = false;

	target->_HandleConnectionRsp(ident, response);
	if (releaseTarget)
		gSocketModule->release_socket(target->socket);
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The options are copied out of the buffer and walked with
// l2cap_parse_next_option(), which the host fuzzer runs too. Before, an
// option that ran past the command was read from the next command, a short
// value left the rest of "value" uninitialized, and a failed create() of
// the reject buffer was not checked.
static void
parse_configuration_options(net_buffer* buffer, size_t offset, uint16 length,
	l2cap_config_options& options)
{
	if (offset >= length)
		return;

	const size_t size = length - offset;
	uint8* data = (uint8*)malloc(size);
	if (data == NULL || gBufferModule->read(buffer, offset, data, size) != B_OK) {
		free(data);
		options.malformed = true;
		return;
	}

	size_t position = 0;
	l2cap_parse_option parsed;
	bool malformed = false;
	while (l2cap_parse_next_option(data, size, &position, &parsed, &malformed)) {
		l2cap_configuration_option_value value;
		memset(&value, 0, sizeof(value));
		memcpy(&value, parsed.value, min_c(sizeof(value), (size_t)parsed.length));

		const bool hint = parsed.hint;
		l2cap_configuration_option option;
		option.type = parsed.type;
		option.length = parsed.length;

		// A known option shorter than its value is treated as unknown.
		bool known = true;
		switch (option.type) {
			case l2cap_configuration_option::OPTION_MTU:
			case l2cap_configuration_option::OPTION_FLUSH_TIMEOUT:
				known = option.length >= sizeof(uint16);
				break;
			case l2cap_configuration_option::OPTION_QOS:
				known = option.length >= sizeof(l2cap_qos);
				break;
		}
		if (!known)
			option.type = 0xff;

		switch (option.type) {
// SPDX-SnippetEnd
			case l2cap_configuration_option::OPTION_MTU:
				options.mtu = le16toh(value.mtu);
				options.mtu_set = true;
				break;

			case l2cap_configuration_option::OPTION_FLUSH_TIMEOUT:
				options.flush_timeout = le16toh(value.flush_timeout);
				options.flush_timeout_set = true;
				break;

			case l2cap_configuration_option::OPTION_QOS:
				options.qos.flags = value.qos.flags;
				options.qos.service_type = value.qos.service_type;
				options.qos.token_rate = le32toh(value.qos.token_rate);
				options.qos.token_bucket_size = le32toh(value.qos.token_bucket_size);
				options.qos.peak_bandwidth = le32toh(value.qos.peak_bandwidth);
				options.qos.access_latency = le32toh(value.qos.access_latency);
				options.qos.delay_variation = le32toh(value.qos.delay_variation);
				options.qos_set = true;
				break;

			default: {
				if (hint)
					break;

				// SPDX-SnippetBegin
				// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
				// SPDX-License-Identifier: MIT
				// Unknown option: we need to reject it.
				if (options.rejected == NULL)
					options.rejected = gBufferModule->create(128);
				if (options.rejected == NULL) {
					options.malformed = true;
					break;
				}
				gBufferModule->append(options.rejected,
					parsed.value - L2CAP_PARSE_OPTION_HEADER,
					L2CAP_PARSE_OPTION_HEADER + parsed.length);
				// SPDX-SnippetEnd
			}
		}
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (malformed)
		options.malformed = true;
	free(data);
	// SPDX-SnippetEnd
}


static status_t
l2cap_handle_configuration_req(HciConnection* conn, uint8 ident, net_buffer* buffer, uint16 length)
{
	NetBufferHeaderReader<l2cap_configuration_req> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 dcid = le16toh(command->dcid);
	L2capEndpoint* endpoint = gL2capEndpointManager.GetForChannel(dcid);
	if (endpoint == NULL) {
		ERROR("l2cap: unexpected configuration req: channel does not exist (cid=%d)\n", dcid);
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_INVALID_CID, 0, 0, 0);
		return B_ERROR;
	}
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Only a channel of this link: a device on another link could close
	// or reconfigure it by guessing its CID.
	if (endpoint->Connection() != NULL && endpoint->Connection() != conn) {
		gSocketModule->release_socket(endpoint->socket);
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_INVALID_CID, 0, 0, 0);
		return B_ERROR;
	}
	// SPDX-SnippetEnd

	const uint16 flags = le16toh(command->flags);

	// Read options (if any).
	l2cap_config_options options = {};
	parse_configuration_options(buffer, sizeof(l2cap_configuration_req), length, options);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (options.malformed) {
		if (options.rejected != NULL)
			gBufferModule->free(options.rejected);
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
		gSocketModule->release_socket(endpoint->socket);
		return B_BAD_DATA;
	}
	// SPDX-SnippetEnd

	if (options.rejected != NULL) {
		// Reject without doing anything else.
		send_l2cap_configuration_rsp(conn, ident, endpoint->DestinationChannelID(), 0,
			l2cap_configuration_rsp::RESULT_UNKNOWN_OPTION, options.rejected);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// The response clones the options; the buffer was leaked here.
		gBufferModule->free(options.rejected);
		// SPDX-SnippetEnd
		gSocketModule->release_socket(endpoint->socket);
		return B_OK;
	}

	endpoint->_HandleConfigurationReq(ident, flags,
		options.mtu_set ? &options.mtu : NULL,
		options.flush_timeout_set ? &options.flush_timeout : NULL,
		options.qos_set ? &options.qos : NULL);
	gSocketModule->release_socket(endpoint->socket);
	return B_OK;
}


static status_t
l2cap_handle_configuration_rsp(HciConnection* conn, L2capEndpoint* endpoint,
	uint8 ident, net_buffer* buffer, uint16 length, bool& releaseIdent)
{
	NetBufferHeaderReader<l2cap_configuration_rsp> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 scid = le16toh(command->scid);
	const uint16 flags = le16toh(command->flags);
	const uint16 result = le16toh(command->result);
	releaseIdent = (flags & L2CAP_CFG_FLAG_CONTINUATION) == 0;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	bool releaseTarget = false;
	L2capEndpoint* target = resolve_response_endpoint(conn, endpoint, scid,
		releaseIdent, releaseTarget);
	if (target == NULL) {
		ERROR("l2cap: unexpected configuration rsp: ident %d scid %d unknown\n",
			ident, scid);
		return B_ERROR;
	}
	// SPDX-SnippetEnd

	// Read options (if any).
	l2cap_config_options options = {};
	parse_configuration_options(buffer, sizeof(l2cap_configuration_rsp), length, options);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The reject buffer was leaked here.
	if (options.rejected != NULL || options.malformed) {
		if (options.rejected != NULL)
			gBufferModule->free(options.rejected);
		// Reject without doing anything else.
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
		// No usable answer will come for this identifier: free it and
		// wake the waiting connect() (it kept waiting before).
		if (!releaseTarget)
			releaseIdent = true;
		target->_HandleBadResponse(ident);
		if (releaseTarget)
			gSocketModule->release_socket(target->socket);
		return B_OK;
	}

	target->_HandleConfigurationRsp(ident, scid, flags, result,
		options.mtu_set ? &options.mtu : NULL,
		options.flush_timeout_set ? &options.flush_timeout : NULL,
		options.qos_set ? &options.qos : NULL);
	if (releaseTarget)
		gSocketModule->release_socket(target->socket);
	// SPDX-SnippetEnd
	return B_OK;
}


static status_t
l2cap_handle_disconnection_req(HciConnection* conn, uint8 ident, net_buffer* buffer)
{
	NetBufferHeaderReader<l2cap_disconnection_req> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 dcid = le16toh(command->dcid);
	L2capEndpoint* endpoint = gL2capEndpointManager.GetForChannel(dcid);
	if (endpoint == NULL) {
		ERROR("l2cap: unexpected disconnection req: channel does not exist (cid=%d)\n", dcid);
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_INVALID_CID, 0, 0, 0);
		return B_ERROR;
	}
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Only a channel of this link: a device on another link could close
	// or reconfigure it by guessing its CID.
	if (endpoint->Connection() != NULL && endpoint->Connection() != conn) {
		gSocketModule->release_socket(endpoint->socket);
		send_l2cap_command_reject(conn, ident,
			l2cap_command_reject::REJECTED_INVALID_CID, 0, 0, 0);
		return B_ERROR;
	}
	// SPDX-SnippetEnd

	const uint16 scid = le16toh(command->scid);

	endpoint->_HandleDisconnectionReq(ident, scid);
	gSocketModule->release_socket(endpoint->socket);
	return B_OK;
}


static status_t
l2cap_handle_disconnection_rsp(HciConnection* conn, L2capEndpoint* endpoint,
	uint8 ident, net_buffer* buffer, bool& releaseIdent)
{
	NetBufferHeaderReader<l2cap_disconnection_rsp> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 dcid = le16toh(command->dcid);
	const uint16 scid = le16toh(command->scid);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	bool releaseTarget = false;
	L2capEndpoint* target = resolve_response_endpoint(conn, endpoint, scid,
		releaseIdent, releaseTarget);
	if (target == NULL) {
		ERROR("l2cap: unexpected disconnection response: ident %d scid %d unknown\n",
			ident, scid);
		return B_ERROR;
	}

	target->_HandleDisconnectionRsp(ident, dcid, scid);
	if (releaseTarget)
		gSocketModule->release_socket(target->socket);
	// SPDX-SnippetEnd
	return B_OK;
}


static status_t
l2cap_handle_echo_req(HciConnection *conn, uint8 ident, net_buffer* buffer, uint16 length)
{
	net_buffer* reply = gBufferModule->clone(buffer, false);
	if (reply == NULL)
		return ENOMEM;

	gBufferModule->trim(reply, length);
	return send_l2cap_command(conn, L2CAP_ECHO_RSP, ident, reply);
}


static status_t
l2cap_handle_echo_rsp(L2capEndpoint* endpoint, uint8 ident, net_buffer* buffer, uint16 length)
{
	// Not currently triggered by this module.
	return B_OK;
}


static status_t
l2cap_handle_info_req(HciConnection* conn, uint8 ident, net_buffer* buffer)
{
	NetBufferHeaderReader<l2cap_information_req> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 type = le16toh(command->type);

	net_buffer* reply = NULL;
	uint8 replyCode = 0;
	switch (type) {
		case l2cap_information_req::TYPE_CONNECTIONLESS_MTU:
			reply = make_l2cap_information_rsp(replyCode, type,
				l2cap_information_rsp::RESULT_SUCCESS, L2CAP_MTU_DEFAULT);
			break;

	    default:
			ERROR("l2cap: unhandled information request type %d\n", type);
			reply = make_l2cap_information_rsp(replyCode, type,
				l2cap_information_rsp::RESULT_NOT_SUPPORTED, 0);
			break;
	}

	return send_l2cap_command(conn, replyCode, ident, reply);
}


static status_t
l2cap_handle_info_rsp(L2capEndpoint* endpoint, uint8 ident, net_buffer* buffer)
{
	// Not currently triggered by this module.
	return B_OK;
}


static status_t
l2cap_handle_command_reject(L2capEndpoint* endpoint, uint8 ident, net_buffer* buffer,
	uint16 length)
{
	if (endpoint == NULL) {
		ERROR("l2cap: unexpected command rejected: ident %d unknown\n", ident);
		return B_ERROR;
	}

	NetBufferHeaderReader<l2cap_command_reject> command(buffer);
	if (command.Status() != B_OK)
		return ENOBUFS;

	const uint16 reason = le16toh(command->reason);
	TRACE("%s: reason=%d\n", __func__, command->reason);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Only what this command has after the reason: the buffer also holds
	// the commands after it in the same packet (review of #4291).
	l2cap_command_reject_data data = {};
	if (length > sizeof(l2cap_command_reject)
		&& buffer->size >= length) {
		gBufferModule->read(buffer, sizeof(l2cap_command_reject), &data,
			min_c(length - sizeof(l2cap_command_reject),
				sizeof(l2cap_command_reject_data)));
	}
	// SPDX-SnippetEnd

	endpoint->_HandleCommandRejected(ident, reason, data);

	return B_OK;
}


// #pragma mark - outgoing signals


status_t
send_l2cap_command(HciConnection* conn, uint8 code, uint8 ident, net_buffer* command)
{
	// TODO: Command timeouts (probably in L2capEndpoint.)
	{
		NetBufferPrepend<l2cap_command_header> header(command);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		if (header.Status() != B_OK) {
			gBufferModule->free(command);
			return ENOBUFS;
		}
		// SPDX-SnippetEnd
		header->code = code;
		header->ident = ident;
		header->length = htole16(command->size - sizeof(l2cap_command_header));
	} {
		NetBufferPrepend<l2cap_basic_header> basicHeader(command);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		if (basicHeader.Status() != B_OK) {
			gBufferModule->free(command);
			return ENOBUFS;
		}
		// SPDX-SnippetEnd
		basicHeader->length = htole16(command->size - sizeof(l2cap_basic_header));
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// An LE link signals on its own channel (Core 6.0, Vol 3, Part A,
		// 2.1).
		basicHeader->dcid = htole16(conn->type == R5_HCI_LINK_LE
			? R5_L2CAP_CID_LE_SIGNALING : L2CAP_SIGNALING_CID);
		// SPDX-SnippetEnd
	}
	command->type = conn->handle;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// PostACL() owns the buffer, also on failure (it freed part of it on
	// some errors, so freeing it here was a double free).
	return l2cap_post_acl(conn->Hid, command);
	// SPDX-SnippetEnd
}


status_t
send_l2cap_command_reject(HciConnection* conn, uint8 ident, uint16 reason,
	uint16 mtu, uint16 scid, uint16 dcid)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_command_reject(code, reason, mtu, scid, dcid);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_configuration_req(HciConnection* conn, uint8 ident, uint16 dcid, uint16 flags,
	uint16* mtu, uint16* flush_timeout, l2cap_qos* flow)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_configuration_req(code, dcid, flags, mtu, flush_timeout, flow);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_connection_req(HciConnection* conn, uint8 ident, uint16 psm, uint16 scid)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_connection_req(code, psm, scid);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_connection_rsp(HciConnection* conn, uint8 ident, uint16 dcid, uint16 scid,
	uint16 result, uint16 status)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_connection_rsp(code, dcid, scid, result, status);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_configuration_rsp(HciConnection* conn, uint8 ident, uint16 scid, uint16 flags,
	uint16 result, net_buffer* opt)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_configuration_rsp(code, scid, flags, result, opt);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_disconnection_req(HciConnection* conn, uint8 ident, uint16 dcid, uint16 scid)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_disconnection_req(code, dcid, scid);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


status_t
send_l2cap_disconnection_rsp(HciConnection* conn, uint8 ident, uint16 dcid, uint16 scid)
{
	uint8 code = 0;
	net_buffer* buffer = make_l2cap_disconnection_rsp(code, dcid, scid);
	if (buffer == NULL)
		return ENOMEM;

	return send_l2cap_command(conn, code, ident, buffer);
}


// #pragma mark - dispatcher


status_t
l2cap_handle_signaling_command(HciConnection* connection, net_buffer* buffer)
{
	TRACE("%s: signal size=%" B_PRIu32 "\n", __func__, buffer->size);

	// TODO: If there are multiple commands in a packet, we could accumulate the
	// responses into a single packet also...

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The length was checked before the 4 header bytes were removed, so a
	// command up to 4 bytes too long passed; remove_header() then failed,
	// the size stayed the same and this loop never ended. A command shorter
	// than its code needs was read from the next command. The early
	// returns leaked the buffer. Now l2cap_parse_check() decides, and the
	// buffer is always freed.
	status_t result = B_OK;
	// The LE signalling channel has its own commands (step 1c).
	const bool le = connection->type == R5_HCI_LINK_LE;
	while (buffer->size != 0) {
		// Every round must consume at least the 4 header bytes; stop
		// rather than spin when it does not (review of #4291).
		const size_t sizeBefore = buffer->size;
		NetBufferHeaderReader<l2cap_command_header> commandHeader(buffer);
		if (commandHeader.Status() != B_OK) {
			result = ENOBUFS;
			break;
		}

		const uint8 code = commandHeader->code;
		const uint8 ident = commandHeader->ident;
		const uint16 length = le16toh(commandHeader->length);
		commandHeader.Remove();
		if (buffer->size >= sizeBefore) {
			result = EMSGSIZE;
			break;
		}

		// An answer takes its identifier, and with it the socket reference,
		// before anything else, so a waiting connect() or shutdown() is also
		// woken by an answer that turns out to be broken (review of #4291).
		// A code this channel does not serve is not understood (and takes
		// no identifier, so a forged answer cannot wake a waiter).
		if (!l2cap_parse_code_allowed(code, le)) {
			if (length > buffer->size) {
				result = EMSGSIZE;
				break;
			}
			send_l2cap_command_reject(connection, ident,
				l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
			if (gBufferModule->remove_header(buffer, length) != B_OK) {
				result = EMSGSIZE;
				break;
			}
			continue;
		}

		// Not L2CAP_IS_SIGNAL_RSP(): 0x18 and 0x1a are answers too.
		L2capEndpoint* endpoint = NULL;
		if (l2cap_parse_is_response(code)) {
			endpoint = static_cast<L2capEndpoint*>(
				l2cap_take_command_ident(connection, ident));
		}

		const int check = l2cap_parse_check(code, length, buffer->size);
		if (check != L2CAP_PARSE_HANDLE && endpoint != NULL) {
			endpoint->_HandleBadResponse(ident);
			gSocketModule->release_socket(endpoint->socket);
			endpoint = NULL;
		}
		if (check == L2CAP_PARSE_TRUNCATED) {
			ERROR("%s: invalid L2CAP signaling command packet, code=%#x, "
				"ident=%d, length=%d, buffer size=%" B_PRIu32 "\n", __func__,
				code, ident, length, buffer->size);
			result = EMSGSIZE;
			break;
		}
		if (check == L2CAP_PARSE_REJECT) {
			ERROR("%s: L2CAP command %#x too short (%d bytes)\n", __func__,
				code, length);
			send_l2cap_command_reject(connection, ident,
				l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
			if (gBufferModule->remove_header(buffer, length) != B_OK) {
				result = EMSGSIZE;
				break;
			}
			continue;
		}

		// A handler clears this when more answers with this identifier
		// follow (a pending connection, a configuration continuation).
		bool releaseIdent = true;
		// SPDX-SnippetEnd

		switch (code) {
			case L2CAP_COMMAND_REJECT_RSP:
				l2cap_handle_command_reject(endpoint, ident, buffer, length);
				break;

			case L2CAP_CONNECTION_REQ:
				l2cap_handle_connection_req(connection, ident, buffer);
				break;

			case L2CAP_CONNECTION_RSP:
				l2cap_handle_connection_rsp(connection, endpoint, ident, buffer,
					releaseIdent);
				break;

			case L2CAP_CONFIGURATION_REQ:
				l2cap_handle_configuration_req(connection, ident,
					buffer, length);
				break;

			case L2CAP_CONFIGURATION_RSP:
				l2cap_handle_configuration_rsp(connection, endpoint, ident,
					buffer, length, releaseIdent);
				break;

			case L2CAP_DISCONNECTION_REQ:
				l2cap_handle_disconnection_req(connection, ident, buffer);
				break;

			case L2CAP_DISCONNECTION_RSP:
				l2cap_handle_disconnection_rsp(connection, endpoint, ident,
					buffer, releaseIdent);
				break;

			case L2CAP_ECHO_REQ:
				l2cap_handle_echo_req(connection, ident, buffer, length);
				break;

			case L2CAP_ECHO_RSP:
				l2cap_handle_echo_rsp(endpoint, ident, buffer, length);
				break;

			case L2CAP_INFORMATION_REQ:
				l2cap_handle_info_req(connection, ident, buffer);
				break;

			case L2CAP_INFORMATION_RSP:
				l2cap_handle_info_rsp(endpoint, ident, buffer);
				break;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
			case 0x17: case 0x18: case 0x19: case 0x1a:
				l2cap_handle_le_command(connection, code, ident, buffer,
					length, endpoint);
				break;
			// SPDX-SnippetEnd

			default:
				ERROR("l2cap: unknown L2CAP signaling command, "
					"code=%#x\n", code);
				send_l2cap_command_reject(connection, ident,
					l2cap_command_reject::REJECTED_NOT_UNDERSTOOD, 0, 0, 0);
				break;
		}

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// The identifier was taken above; put it back while more answers
		// with it are expected, else drop its socket reference.
		if (endpoint != NULL) {
			if (releaseIdent)
				gSocketModule->release_socket(endpoint->socket);
			else
				l2cap_keep_command_ident(connection, ident, endpoint);
		}
		// SPDX-SnippetEnd

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Advance to the next command (if any.)
		if (gBufferModule->remove_header(buffer, length) != B_OK) {
			result = EMSGSIZE;
			break;
		}
		// SPDX-SnippetEnd
	}

	gBufferModule->free(buffer);
	return result;
}
