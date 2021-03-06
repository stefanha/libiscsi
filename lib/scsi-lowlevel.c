/*
   Copyright (C) 2010 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

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
/*
 * would be nice if this could grow into a full blown library for scsi to
 * 1, build a CDB
 * 2, check how big a complete data-in structure needs to be
 * 3, unmarshall data-in into a real structure
 * 4, marshall a real structure into a data-out blob
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "scsi-lowlevel.h"
#include "slist.h"


void
scsi_free_scsi_task(struct scsi_task *task)
{
	struct scsi_allocated_memory *mem;

	while ((mem = task->mem)) {
		   SLIST_REMOVE(&task->mem, mem);
		   free(mem->ptr);
		   free(mem);
	}

	free(task->datain.data);
	free(task);
}

static void *
scsi_malloc(struct scsi_task *task, size_t size)
{
	struct scsi_allocated_memory *mem;

	mem = malloc(sizeof(struct scsi_allocated_memory));
	if (mem == NULL) {
		return NULL;
	}
	bzero(mem, sizeof(struct scsi_allocated_memory));
	mem->ptr = malloc(size);
	if (mem->ptr == NULL) {
		free(mem);
		return NULL;
	}
	bzero(mem->ptr, size);
	SLIST_ADD(&task->mem, mem);
	return mem->ptr;
}

struct value_string {
       int value;
       const char *string;
};

static const char *
value_string_find(struct value_string *values, int value)
{
	for (; values->value; values++) {
		if (value == values->value) {
			return values->string;
		}
	}
	return NULL;
}

const char *
scsi_sense_key_str(int key)
{
	struct value_string keys[] = {
		{SCSI_SENSE_ILLEGAL_REQUEST,
		 "ILLEGAL_REQUEST"},
		{SCSI_SENSE_UNIT_ATTENTION,
		 "UNIT_ATTENTION"},
	       {0, NULL}
	};

	return value_string_find(keys, key);
}

const char *
scsi_sense_ascq_str(int ascq)
{
	struct value_string ascqs[] = {
		{SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB,
		 "INVALID_FIELD_IN_CDB"},
		{SCSI_SENSE_ASCQ_LOGICAL_UNIT_NOT_SUPPORTED,
		 "LOGICAL_UNIT_NOT_SUPPORTED"},
		{SCSI_SENSE_ASCQ_BUS_RESET,
		 "BUS_RESET"},
	       {0, NULL}
	};

	return value_string_find(ascqs, ascq);
}

/*
 * TESTUNITREADY
 */
struct scsi_task *
scsi_cdb_testunitready(void)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_TESTUNITREADY;

	task->cdb_size   = 6;
	task->xfer_dir   = SCSI_XFER_NONE;
	task->expxferlen = 0;

	return task;
}


/*
 * REPORTLUNS
 */
struct scsi_task *
scsi_reportluns_cdb(int report_type, int alloc_len)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_REPORTLUNS;
	task->cdb[2]   = report_type;
	*(uint32_t *)&task->cdb[6] = htonl(alloc_len);

	task->cdb_size = 12;
	task->xfer_dir = SCSI_XFER_READ;
	task->expxferlen = alloc_len;

	task->params.reportluns.report_type = report_type;

	return task;
}

/*
 * parse the data in blob and calcualte the size of a full report luns
 * datain structure
 */
static int
scsi_reportluns_datain_getfullsize(struct scsi_task *task)
{
	uint32_t list_size;

	list_size = htonl(*(uint32_t *)&(task->datain.data[0])) + 8;

	return list_size;
}

/*
 * unmarshall the data in blob for reportluns into a structure
 */
static struct scsi_reportluns_list *
scsi_reportluns_datain_unmarshall(struct scsi_task *task)
{
	struct scsi_reportluns_list *list;
	int list_size;
	int i, num_luns;

	if (task->datain.size < 4) {
		return NULL;
	}

	list_size = htonl(*(uint32_t *)&(task->datain.data[0])) + 8;
	if (list_size < task->datain.size) {
		return NULL;
	}

	num_luns = list_size / 8 - 1;
	list = scsi_malloc(task, offsetof(struct scsi_reportluns_list, luns)
			   + sizeof(uint16_t) * num_luns);
	if (list == NULL) {
		return NULL;
	}

	list->num = num_luns;
	for (i = 0; i < num_luns; i++) {
		list->luns[i] = htons(*(uint16_t *)
				      &(task->datain.data[i*8+8]));
	}

	return list;
}


/*
 * READCAPACITY10
 */
struct scsi_task *
scsi_cdb_readcapacity10(int lba, int pmi)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_READCAPACITY10;

	*(uint32_t *)&task->cdb[2] = htonl(lba);

	if (pmi) {
		task->cdb[8] |= 0x01;
	}

	task->cdb_size = 10;
	task->xfer_dir = SCSI_XFER_READ;
	task->expxferlen = 8;

	task->params.readcapacity10.lba = lba;
	task->params.readcapacity10.pmi = pmi;

	return task;
}

/*
 * parse the data in blob and calcualte the size of a full
 * readcapacity10 datain structure
 */
static int
scsi_readcapacity10_datain_getfullsize(struct scsi_task *task _U_)
{
	return 8;
}

/*
 * unmarshall the data in blob for readcapacity10 into a structure
 */
static struct scsi_readcapacity10 *
scsi_readcapacity10_datain_unmarshall(struct scsi_task *task)
{
	struct scsi_readcapacity10 *rc10;

	if (task->datain.size < 8) {
		return NULL;
	}
	rc10 = scsi_malloc(task, sizeof(struct scsi_readcapacity10));
	if (rc10 == NULL) {
		return NULL;
	}

	rc10->lba        = htonl(*(uint32_t *)&(task->datain.data[0]));
	rc10->block_size = htonl(*(uint32_t *)&(task->datain.data[4]));

	return rc10;
}





/*
 * INQUIRY
 */
struct scsi_task *
scsi_cdb_inquiry(int evpd, int page_code, int alloc_len)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_INQUIRY;

	if (evpd) {
		task->cdb[1] |= 0x01;
	}

	task->cdb[2] = page_code;

	*(uint16_t *)&task->cdb[3] = htons(alloc_len);

	task->cdb_size = 6;
	task->xfer_dir = SCSI_XFER_READ;
	task->expxferlen = alloc_len;

	task->params.inquiry.evpd      = evpd;
	task->params.inquiry.page_code = page_code;

	return task;
}

/*
 * parse the data in blob and calcualte the size of a full
 * inquiry datain structure
 */
static int
scsi_inquiry_datain_getfullsize(struct scsi_task *task)
{
	if (task->params.inquiry.evpd == 0) {
		return task->datain.data[4] + 3;
	}

	switch (task->params.inquiry.page_code) {
	case SCSI_INQUIRY_PAGECODE_SUPPORTED_VPD_PAGES:
		return task->datain.data[3] + 4;
	case SCSI_INQUIRY_PAGECODE_UNIT_SERIAL_NUMBER:
		return task->datain.data[3] + 4;
	case SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION:
	     return ntohs(*(uint16_t *)&task->datain.data[2]) + 4;
	case SCSI_INQUIRY_PAGECODE_BLOCK_DEVICE_CHARACTERISTICS:
		return task->datain.data[3] + 4;
	default:
		return -1;
	}
}

/*
 * unmarshall the data in blob for inquiry into a structure
 */
static void *
scsi_inquiry_datain_unmarshall(struct scsi_task *task)
{
	if (task->params.inquiry.evpd == 0) {
		struct scsi_inquiry_standard *inq;

		/* standard inquiry */
		inq = scsi_malloc(task, sizeof(struct scsi_inquiry_standard));
		if (inq == NULL) {
			return NULL;
		}

		inq->periperal_qualifier    = (task->datain.data[0]>>5)&0x07;
		inq->periperal_device_type  = task->datain.data[0]&0x1f;
		inq->rmb                    = !!(task->datain.data[1]&0x80);
		inq->version                = task->datain.data[2];
		inq->normaca                = !!(task->datain.data[3]&0x20);
		inq->hisup                  = !!(task->datain.data[3]&0x10);
		inq->response_data_format   = task->datain.data[3]&0x0f;

		inq->sccs                   = !!(task->datain.data[5]&0x80);
		inq->acc                    = !!(task->datain.data[5]&0x40);
		inq->tpgs                   = (task->datain.data[5]>>4)&0x03;
		inq->threepc                = !!(task->datain.data[5]&0x08);
		inq->protect                = !!(task->datain.data[5]&0x01);

		inq->encserv                = !!(task->datain.data[6]&0x40);
		inq->multip                 = !!(task->datain.data[6]&0x10);
		inq->addr16                 = !!(task->datain.data[6]&0x01);
		inq->wbus16                 = !!(task->datain.data[7]&0x20);
		inq->sync                   = !!(task->datain.data[7]&0x10);
		inq->cmdque                 = !!(task->datain.data[7]&0x02);

		memcpy(&inq->vendor_identification[0],
		       &task->datain.data[8], 8);
		memcpy(&inq->product_identification[0],
		       &task->datain.data[16], 16);
		memcpy(&inq->product_revision_level[0],
		       &task->datain.data[32], 4);

		inq->clocking               = (task->datain.data[56]>>2)&0x03;
		inq->qas                    = !!(task->datain.data[56]&0x02);
		inq->ius                    = !!(task->datain.data[56]&0x01);

		return inq;
	}

	if (task->params.inquiry.page_code
	    == SCSI_INQUIRY_PAGECODE_SUPPORTED_VPD_PAGES) {
		struct scsi_inquiry_supported_pages *inq;

		inq = scsi_malloc(task,
			   sizeof(struct scsi_inquiry_supported_pages));
		if (inq == NULL) {
			return NULL;
		}
		inq->periperal_qualifier   = (task->datain.data[0]>>5)&0x07;
		inq->periperal_device_type = task->datain.data[0]&0x1f;
		inq->pagecode              = task->datain.data[1];

		inq->num_pages = task->datain.data[3];
		inq->pages = scsi_malloc(task, inq->num_pages);
		if (inq->pages == NULL) {
			return NULL;
		}
		memcpy(inq->pages, &task->datain.data[4], inq->num_pages);
		return inq;
	} else if (task->params.inquiry.page_code
		   == SCSI_INQUIRY_PAGECODE_UNIT_SERIAL_NUMBER) {
		struct scsi_inquiry_unit_serial_number *inq;

		inq = scsi_malloc(task,
			   sizeof(struct scsi_inquiry_unit_serial_number));
		if (inq == NULL) {
			return NULL;
		}
		inq->periperal_qualifier   = (task->datain.data[0]>>5)&0x07;
		inq->periperal_device_type = task->datain.data[0]&0x1f;
		inq->pagecode              = task->datain.data[1];

		inq->usn = scsi_malloc(task, task->datain.data[3]+1);
		if (inq->usn == NULL) {
			return NULL;
		}
		memcpy(inq->usn, &task->datain.data[4], task->datain.data[3]);
		inq->usn[task->datain.data[3]] = 0;
		return inq;
	} else if (task->params.inquiry.page_code
		   == SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION) {
		struct scsi_inquiry_device_identification *inq;
		int remaining = ntohs(*(uint16_t *)&task->datain.data[2]);
		unsigned char *dptr;

		inq = scsi_malloc(task,
			   sizeof(struct scsi_inquiry_device_identification));
		if (inq == NULL) {
			return NULL;
		}
		inq->periperal_qualifier   = (task->datain.data[0]>>5)&0x07;
		inq->periperal_device_type = task->datain.data[0]&0x1f;
		inq->pagecode              = task->datain.data[1];

		dptr = &task->datain.data[4];
		while (remaining > 0) {
			struct scsi_inquiry_device_designator *dev;

			dev = scsi_malloc(task,
			      sizeof(struct scsi_inquiry_device_designator));
			if (dev == NULL) {
				return NULL;
			}

			dev->next = inq->designators;
			inq->designators = dev;

			dev->protocol_identifier = (dptr[0]>>4) & 0x0f;
			dev->code_set            = dptr[0] & 0x0f;
			dev->piv                 = !!(dptr[1]&0x80);
			dev->association         = (dptr[1]>>4)&0x03;
			dev->designator_type     = dptr[1]&0x0f;

			dev->designator_length   = dptr[3];
			dev->designator          = scsi_malloc(task,
						   dev->designator_length+1);
			if (dev->designator == NULL) {
				return NULL;
			}
			dev->designator[dev->designator_length] = 0;
			memcpy(dev->designator, &dptr[4],
			       dev->designator_length);

			remaining -= 4;
			remaining -= dev->designator_length;

			dptr += dev->designator_length + 4;
		}
		return inq;
	} else if (task->params.inquiry.page_code
		   == SCSI_INQUIRY_PAGECODE_BLOCK_DEVICE_CHARACTERISTICS) {
		struct scsi_inquiry_block_device_characteristics *inq;

		inq = scsi_malloc(task,
		      sizeof(struct scsi_inquiry_block_device_characteristics));
		if (inq == NULL) {
			return NULL;
		}
		inq->periperal_qualifier   = (task->datain.data[0]>>5)&0x07;
		inq->periperal_device_type = task->datain.data[0]&0x1f;
		inq->pagecode              = task->datain.data[1];

		inq->medium_rotation_rate  = ntohs(*(uint16_t *)
						   &task->datain.data[4]);
		return inq;
	}

	return NULL;
}

/*
 * READ10
 */
struct scsi_task *
scsi_cdb_read10(int lba, int xferlen, int blocksize)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_READ10;

	*(uint32_t *)&task->cdb[2] = htonl(lba);
	*(uint16_t *)&task->cdb[7] = htons(xferlen/blocksize);

	task->cdb_size = 10;
	task->xfer_dir = SCSI_XFER_READ;
	task->expxferlen = xferlen;

	return task;
}

/*
 * WRITE10
 */
struct scsi_task *
scsi_cdb_write10(int lba, int xferlen, int fua, int fuanv, int blocksize)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_WRITE10;

	if (fua) {
		task->cdb[1] |= 0x08;
	}
	if (fuanv) {
		task->cdb[1] |= 0x02;
	}

	*(uint32_t *)&task->cdb[2] = htonl(lba);
	*(uint16_t *)&task->cdb[7] = htons(xferlen/blocksize);

	task->cdb_size = 10;
	task->xfer_dir = SCSI_XFER_WRITE;
	task->expxferlen = xferlen;

	return task;
}



/*
 * MODESENSE6
 */
struct scsi_task *
scsi_cdb_modesense6(int dbd, enum scsi_modesense_page_control pc,
		    enum scsi_modesense_page_code page_code,
		    int sub_page_code, unsigned char alloc_len)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_MODESENSE6;

	if (dbd) {
		task->cdb[1] |= 0x08;
	}
	task->cdb[2] = pc<<6 | page_code;
	task->cdb[3] = sub_page_code;
	task->cdb[4] = alloc_len;

	task->cdb_size = 6;
	task->xfer_dir = SCSI_XFER_READ;
	task->expxferlen = alloc_len;

	task->params.modesense6.dbd           = dbd;
	task->params.modesense6.pc            = pc;
	task->params.modesense6.page_code     = page_code;
	task->params.modesense6.sub_page_code = sub_page_code;

	return task;
}

/*
 * parse the data in blob and calcualte the size of a full
 * modesense6 datain structure
 */
static int
scsi_modesense6_datain_getfullsize(struct scsi_task *task)
{
	int len;

	len = task->datain.data[0] + 1;

	return len;
}


/*
 * SYNCHRONIZECACHE10
 */
struct scsi_task *
scsi_cdb_synchronizecache10(int lba, int num_blocks, int syncnv, int immed)
{
	struct scsi_task *task;

	task = malloc(sizeof(struct scsi_task));
	if (task == NULL) {
		return NULL;
	}

	bzero(task, sizeof(struct scsi_task));
	task->cdb[0]   = SCSI_OPCODE_SYNCHRONIZECACHE10;

	if (syncnv) {
		task->cdb[1] |= 0x04;
	}
	if (immed) {
		task->cdb[1] |= 0x02;
	}
	*(uint32_t *)&task->cdb[2] = htonl(lba);
	*(uint16_t *)&task->cdb[7] = htons(num_blocks);

	task->cdb_size   = 10;
	task->xfer_dir   = SCSI_XFER_NONE;
	task->expxferlen = 0;

	return task;
}



int
scsi_datain_getfullsize(struct scsi_task *task)
{
	switch (task->cdb[0]) {
	case SCSI_OPCODE_TESTUNITREADY:
		return 0;
	case SCSI_OPCODE_INQUIRY:
		return scsi_inquiry_datain_getfullsize(task);
	case SCSI_OPCODE_MODESENSE6:
		return scsi_modesense6_datain_getfullsize(task);
	case SCSI_OPCODE_READCAPACITY10:
		return scsi_readcapacity10_datain_getfullsize(task);
	case SCSI_OPCODE_SYNCHRONIZECACHE10:
		return 0;
	case SCSI_OPCODE_REPORTLUNS:
		return scsi_reportluns_datain_getfullsize(task);
	}
	return -1;
}

void *
scsi_datain_unmarshall(struct scsi_task *task)
{
	switch (task->cdb[0]) {
	case SCSI_OPCODE_TESTUNITREADY:
		return NULL;
	case SCSI_OPCODE_INQUIRY:
		return scsi_inquiry_datain_unmarshall(task);
	case SCSI_OPCODE_READCAPACITY10:
		return scsi_readcapacity10_datain_unmarshall(task);
	case SCSI_OPCODE_SYNCHRONIZECACHE10:
		return NULL;
	case SCSI_OPCODE_REPORTLUNS:
		return scsi_reportluns_datain_unmarshall(task);
	}
	return NULL;
}


const char *
scsi_devtype_to_str(enum scsi_inquiry_peripheral_device_type type)
{
	switch (type) {
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS:
		return "DIRECT_ACCESS";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SEQUENTIAL_ACCESS:
		return "SEQUENTIAL_ACCESS";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_PRINTER:
		return "PRINTER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_PROCESSOR:
		return "PROCESSOR";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_WRITE_ONCE:
		return "WRITE_ONCE";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_MMC:
		return "MMC";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SCANNER:
		return "SCANNER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OPTICAL_MEMORY:
		return "OPTICAL_MEMORY";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_MEDIA_CHANGER:
		return "MEDIA_CHANGER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_COMMUNICATIONS:
		return "COMMUNICATIONS";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_STORAGE_ARRAY_CONTROLLER:
		return "STORAGE_ARRAY_CONTROLLER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_ENCLOSURE_SERVICES:
		return "ENCLOSURE_SERVICES";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SIMPLIFIED_DIRECT_ACCESS:
		return "SIMPLIFIED_DIRECT_ACCESS";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OPTICAL_CARD_READER:
		return "OPTICAL_CARD_READER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_BRIDGE_CONTROLLER:
		return "BRIDGE_CONTROLLER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OSD:
		return "OSD";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_AUTOMATION:
		return "AUTOMATION";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SEQURITY_MANAGER:
		return "SEQURITY_MANAGER";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_WELL_KNOWN_LUN:
		return "WELL_KNOWN_LUN";
	case SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_UNKNOWN:
		return "UNKNOWN";
	}
	return "unknown";
}

const char *
scsi_devqualifier_to_str(enum scsi_inquiry_peripheral_qualifier qualifier)
{
	switch (qualifier) {
	case SCSI_INQUIRY_PERIPHERAL_QUALIFIER_CONNECTED:
		return "CONNECTED";
	case SCSI_INQUIRY_PERIPHERAL_QUALIFIER_DISCONNECTED:
		return "DISCONNECTED";
	case SCSI_INQUIRY_PERIPHERAL_QUALIFIER_NOT_SUPPORTED:
		return "NOT_SUPPORTED";
	}
	return "unknown";
}

const char *
scsi_version_to_str(enum scsi_version version)
{
	switch (version) {
	case SCSI_VERSION_SPC:
		return "ANSI INCITS 301-1997 (SPC)";
	case SCSI_VERSION_SPC2:
		return "ANSI INCITS 351-2001 (SPC-2)";
	case SCSI_VERSION_SPC3:
		return "ANSI INCITS 408-2005 (SPC-3)";
	}
	return "unknown";
}


const char *
scsi_inquiry_pagecode_to_str(int pagecode)
{
	switch (pagecode) {
	case SCSI_INQUIRY_PAGECODE_SUPPORTED_VPD_PAGES:
	     return "SUPPORTED_VPD_PAGES";
	case SCSI_INQUIRY_PAGECODE_UNIT_SERIAL_NUMBER:
		return "UNIT_SERIAL_NUMBER";
	case SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION:
		return "DEVICE_IDENTIFICATION";
	case SCSI_INQUIRY_PAGECODE_BLOCK_DEVICE_CHARACTERISTICS:
		return "BLOCK_DEVICE_CHARACTERISTICS";
	}
	return "unknown";
}


const char *
scsi_protocol_identifier_to_str(int identifier)
{
	switch (identifier) {
	case SCSI_PROTOCOL_IDENTIFIER_FIBRE_CHANNEL:
	     return "FIBRE_CHANNEL";
	case SCSI_PROTOCOL_IDENTIFIER_PARALLEL_SCSI:
	     return "PARALLEL_SCSI";
	case SCSI_PROTOCOL_IDENTIFIER_SSA:
		return "SSA";
	case SCSI_PROTOCOL_IDENTIFIER_IEEE_1394:
		return "IEEE_1394";
	case SCSI_PROTOCOL_IDENTIFIER_RDMA:
		return "RDMA";
	case SCSI_PROTOCOL_IDENTIFIER_ISCSI:
		return "ISCSI";
	case SCSI_PROTOCOL_IDENTIFIER_SAS:
		return "SAS";
	case SCSI_PROTOCOL_IDENTIFIER_ADT:
		return "ADT";
	case SCSI_PROTOCOL_IDENTIFIER_ATA:
		return "ATA";
	}
	return "unknown";
}

const char *
scsi_codeset_to_str(int codeset)
{
	switch (codeset) {
	case SCSI_CODESET_BINARY:
		return "BINARY";
	case SCSI_CODESET_ASCII:
		return "ASCII";
	case SCSI_CODESET_UTF8:
		return "UTF8";
	}
	return "unknown";
}

const char *
scsi_association_to_str(int association)
{
	switch (association) {
	case SCSI_ASSOCIATION_LOGICAL_UNIT:
		return "LOGICAL_UNIT";
	case SCSI_ASSOCIATION_TARGET_PORT:
		return "TARGET_PORT";
	case SCSI_ASSOCIATION_TARGET_DEVICE:
		return "TARGET_DEVICE";
	}
	return "unknown";
}

const char *
scsi_designator_type_to_str(int type)
{
	switch (type) {
	case SCSI_DESIGNATOR_TYPE_VENDOR_SPECIFIC:
		return "VENDOR_SPECIFIC";
	case SCSI_DESIGNATOR_TYPE_T10_VENDORT_ID:
		return "T10_VENDORT_ID";
	case SCSI_DESIGNATOR_TYPE_EUI_64:
		return "EUI_64";
	case SCSI_DESIGNATOR_TYPE_NAA:
		return "NAA";
	case SCSI_DESIGNATOR_TYPE_RELATIVE_TARGET_PORT:
		return "RELATIVE_TARGET_PORT";
	case SCSI_DESIGNATOR_TYPE_TARGET_PORT_GROUP:
		return "TARGET_PORT_GROUP";
	case SCSI_DESIGNATOR_TYPE_LOGICAL_UNIT_GROUP:
		return "LOGICAL_UNIT_GROUP";
	case SCSI_DESIGNATOR_TYPE_MD5_LOGICAL_UNIT_IDENTIFIER:
		return "MD5_LOGICAL_UNIT_IDENTIFIER";
	case SCSI_DESIGNATOR_TYPE_SCSI_NAME_STRING:
		return "SCSI_NAME_STRING";
	}
	return "unknown";
}

void
scsi_set_task_private_ptr(struct scsi_task *task, void *ptr)
{
	task->ptr = ptr;
}

void *
scsi_get_task_private_ptr(struct scsi_task *task)
{
	return task->ptr;
}
