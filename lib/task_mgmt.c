/*
   Copyright (C) 2011 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "iscsi.h"
#include "iscsi-private.h"

int
iscsi_task_mgmt_async(struct iscsi_context *iscsi,
		      int lun, uint8_t function, 
		      uint32_t ritt, uint32_t rcmdsn,
		      iscsi_command_cb cb, void *private_data)
{
	struct iscsi_pdu *pdu;

	if (iscsi->is_loggedin == 0) {
		iscsi_set_error(iscsi, "trying send nop-out while not logged "
				"in");
		return -1;
	}

	pdu = iscsi_allocate_pdu(iscsi, ISCSI_PDU_SCSI_TASK_MANAGEMENT_REQUEST,
				 ISCSI_PDU_SCSI_TASK_MANAGEMENT_RESPONSE);
	if (pdu == NULL) {
		iscsi_set_error(iscsi, "Failed to allocate task mgmt pdu");
		return -1;
	}

	/* immediate flag */
	iscsi_pdu_set_immediate(pdu);

	/* flags */
	iscsi_pdu_set_pduflags(pdu, 0x80 | function);

	/* lun */
	iscsi_pdu_set_lun(pdu, 2);

	/* ritt */
	iscsi_pdu_set_ritt(pdu, ritt);

	/* cmdsn is not increased if Immediate delivery*/
	iscsi_pdu_set_cmdsn(pdu, iscsi->cmdsn);
	pdu->cmdsn = iscsi->cmdsn;

	/* rcmdsn */
	iscsi_pdu_set_rcmdsn(pdu, rcmdsn);

	
	pdu->callback     = cb;
	pdu->private_data = private_data;

	if (iscsi_queue_pdu(iscsi, pdu) != 0) {
		iscsi_set_error(iscsi, "failed to queue iscsi taskmgmt pdu");
		iscsi_free_pdu(iscsi, pdu);
		return -1;
	}

	return 0;
}

int
iscsi_process_task_mgmt_reply(struct iscsi_context *iscsi, struct iscsi_pdu *pdu,
			    struct iscsi_in_pdu *in)
{
	struct iscsi_data data;

	data.data = NULL;
	data.size = 0;

	if (in->data_pos > ISCSI_HEADER_SIZE) {
		data.data = in->data;
		data.size = in->data_pos;
	}
	pdu->callback(iscsi, SCSI_STATUS_GOOD, &data, pdu->private_data);

	return 0;
}
