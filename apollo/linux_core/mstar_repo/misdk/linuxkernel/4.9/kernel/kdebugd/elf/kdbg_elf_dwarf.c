/*
 *  kernel/kdebugd/aop/kdbg_elf_dwarf.c
 *
 *  debug line section parsing module
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010-05-10  Created by gupta.namit@samsung.com
 *
 */

/********************************************************************
 INCLUDE FILES
********************************************************************/

#include <linux/types.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/errno.h>
#include <linux/fcntl.h>

#include "kdbg_elf_sym_debug.h"

#include "kdbg_elf_addr2line.h"
#include "kdbg_elf_dwarf.h"

/*
 * The function read the elf file and extract the line no and file name
 * from the given ELF.
 *
 * The function maintain a table list for each elf file,
 * each ELF file has more has one or more than one table.
 *
 * A link list is maintained to keep a all tables in the  kdbg_elf_usb_elf_list_item
 * structure.
 *
 * function return 1 on success and 0 on failure.
 */

int kdbg_elf_read_debug_line_table(kdbg_elf_usb_elf_list_item *plist)
{

	struct file *aop_filp = NULL;	/* File pointer to access file for ELF parsing */
	int bytesread = 0;

	/* struct to keep linklist of table inforamtion */
	struct line_table_info *table_info = NULL;

	/* struct to keep table info */
	struct arange *tarange; /* Link list for the VMA*/

	int prev_offset = 0;
	uint32_t aop_dbgline_offset = 0;
	uint32_t aop_dbgline_size = 0;
	unsigned long size = 0;
	char *dbg_line_buf = NULL;
	int offset = 0;
	int found = 0;
	/* keep the current fs */
	mm_segment_t oldfs = get_fs();

	if (!plist) {
		sym_errk("elf list item not found\n");
		return 0;
	}

	if (!plist->dbg_line_buf_size) {
		sym_printk("debug line section not found for this elf ....\n");
		return 0;
	}
	sym_printk("enter\n");

	sym_printk("[%s file loading....\n", plist->elf_name_actual_path);
	/*
	 * Kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	set_fs(KERNEL_DS);

	/* File Open */
	aop_filp =
	    filp_open(plist->elf_name_actual_path, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(aop_filp) || (aop_filp == NULL)) {
		aop_filp = NULL;
		sym_errk("file open error\n\n");
		goto ERR;
	}

	if (aop_filp->f_op->read == NULL) {
		sym_errk("read not allowed\n");
		goto ERR;
	}

	aop_filp->f_pos = 0;

	/* debug line section offset */
	aop_dbgline_offset = plist->dbg_line_buf_offset;

	/* debug line section size */
	aop_dbgline_size = plist->dbg_line_buf_size;

	sym_printk("debug line size = %d offset %d \n",
		   plist->dbg_line_buf_size, plist->dbg_line_buf_offset);

	/* Set the offset to read debug_line section. */
	aop_filp->f_pos = (loff_t) (aop_dbgline_offset);
	if (aop_filp->f_pos <= 0) {
		sym_errk("error in setting offset in file\n");
		goto ERR;
	}

	/* first 4 byte are the size of the line info table of the comp unit ..
	 * first read 4 byte and then read the size of return.
	 * in this approch we do  not read whole table, by this approch memory
	 * huge amount of memory can be saved*/

	while (offset < aop_dbgline_size) {

		/* Read the 4 byte, first 4 byte are the size of table... */
		bytesread = aop_filp->f_op->read(aop_filp,
					(void *)&size, (sizeof(char) * 4),
				      &aop_filp->f_pos);
		if (bytesread < (sizeof(char) * 4)) {
			sym_errk("Bytes Read: %d read bytes out of required 4"
				 "Exiting...\n", bytesread);
			goto ERR;
		}

		/* Allocate buffer of the size+4 because 4 bytes alerady read. */

		dbg_line_buf = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
							    sizeof(char) *
							    (size + 4),
							    GFP_KERNEL);
		if (!dbg_line_buf) {
			/* Scanrcity of memory dont go ahead */
			sym_errk("dbg_line_buf is NULL Exiting...\n");
			goto ERR;
		}

		/* copy size bytes in buffer */
		memcpy(dbg_line_buf, &size, 4);
		dbg_line_buf[3] = 0;
		/* After reading the size read the setion */
		bytesread = aop_filp->f_op->read(aop_filp, (dbg_line_buf + 4),
					(sizeof(char) * (size)),
					&aop_filp->f_pos);
		if (bytesread < (sizeof(char) * (size))) {
			/* May be the elf is currupt dont go ahead */
			sym_errk("Bytes Read: %d read bytes out of required %lu"
				 "Exiting...\n", bytesread, size);
			goto ERR;
		}
		 dbg_line_buf[3+(sizeof(char) * (size)) - 1] = 0;

		prev_offset = offset + aop_dbgline_offset;

		/*decode line info table */
		tarange  = decode_addr_range_info(dbg_line_buf, size, &offset);
		if (tarange) {	/* found the table info make a node in link list
					   and list out inportant info. */
			sym_printk("found line table line table\n");

			if (!table_info) {
				table_info =
				    (struct line_table_info *)
				    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
							 sizeof(struct
								line_table_info),
							 GFP_KERNEL);
				if (table_info) {
					table_info->Next = NULL;
					table_info->Prev = NULL;
					table_info->offset = prev_offset;
					table_info->size = size;
					table_info->arange = tarange;
					plist->dbg_line_tables = table_info;
					found = 1;
				}
			} else {

				table_info->Next =
				    (struct line_table_info *)
				    KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
							 sizeof(struct
								line_table_info),
							 GFP_KERNEL);
				if (table_info->Next) {
					table_info->Next->Prev = table_info;
					table_info = table_info->Next;
					table_info->offset = prev_offset;
					table_info->size = size;
					table_info->arange = tarange;
					table_info->Next = NULL;
				}
			}

		/*	free_line_info_table(pLine_table);
			KDBG_MEM_DBG_KFREE(pLine_table);
			pLine_table = NULL;*/
		}

		offset += 4;
		sym_printk("offset =%d\n", offset);

		/* free the buffer .. */
		if (dbg_line_buf) {
			KDBG_MEM_DBG_KFREE(dbg_line_buf);
			dbg_line_buf = NULL;
		}
		PRINT_KD("Dwarf loading...%d%%\r",
			 ((offset * 100) / plist->dbg_line_buf_size));
	}
	PRINT_KD("Dwarf loading...%d%%\r", 100);
	PRINT_KD("\n");
	PRINT_KD("done\n");

	/* set old FS */
	set_fs(oldfs);
	if (aop_filp) {
		filp_close(aop_filp, NULL);
	}

	/* table not found in debug line section */
	if (!found) {
		plist->dbg_line_tables = NULL;
	}
	return found;

ERR:
	/* partial tables are also OK ...
	 * */
	if (dbg_line_buf) {
		KDBG_MEM_DBG_KFREE(dbg_line_buf);
		dbg_line_buf = NULL;
	}

	set_fs(oldfs);
	if (aop_filp) {
		filp_close(aop_filp, NULL);
	}

	/* table not found in debug line section */
	if (!found) {
		plist->dbg_line_tables = NULL;
	}
	return found;
}

/*
 * The function search the table which are extracted in
 * kdbg_elf_read_debug_line_table() fucntion.
 *
 * A table takes hugh amount of memory so keeping tables in a
 * memory is not possible, rather than keep important information
 * like lower address and higher address.
 *
 * while searching just compare the lower and higher address
 * if the address falls between lower address and higher address
 * of the table, then parse the same table again and extract
 * all the inforamtion.
 * */

int kdbg_elf_dbg_line_search_table(kdbg_elf_usb_elf_list_item *
				   plist /*list of all ELF */ ,
				   unsigned long addr /*search address */ ,
				   struct aop_df_info *pdf_info
				   /* Dwarf info for that address */)
{

	struct file *aop_filp = NULL;	/* File pointer to access file for ELF parsing */

	struct line_table_info *each_table = NULL;
	int bytesread = 0;
	int found = 0;
	char *dbg_line_buf = NULL;
	struct line_info_table *pLine_table;
	int offset = 0;
	char *tfile_name = NULL;
	int line_no = 0;

	/* keep the current fs */
	mm_segment_t oldfs = get_fs();
	if (!plist) {
		sym_errk("elf list item not found\n");
		return 0;
	}

	sym_printk("address %d:%x\n", addr, addr);

	if (!pdf_info) {
		/* No memory for file name and line no return immidiately */
		sym_errk("structure for filename and line no not allocated\n");
		return 0;
	}

	/* Search all the table in the link list of corresponding ELF..if
	 * address matched,
	 * Go to the provided offset of ELF and read the table*/

	for (each_table = plist->dbg_line_tables; each_table;
	     each_table = each_table->Next) {

		/* check whethere table contain the address. called only one time
		 * when address matched */
		if (table_contains_address(each_table, addr)) {
			/*
			 * Kernel segment override to datasegment and write it
			 * to the accounting file.
			 */
			set_fs(KERNEL_DS);

			/* File Open */
			aop_filp = filp_open(plist->elf_name_actual_path,
					     O_RDONLY | O_LARGEFILE, 0);

			if (IS_ERR(aop_filp) || (aop_filp == NULL)) {
				aop_filp = NULL;
				sym_errk("file open error\n\n");
				goto ERR;
			}

			if (aop_filp->f_op->read == NULL) {
				sym_errk("read not allowed\n");
				goto ERR;
			}
			aop_filp->f_pos = 0;

			sym_printk("offset %d \n", each_table->offset);

			/* Goto offset of the file .. */
			aop_filp->f_pos = (loff_t) (each_table->offset);
			if (aop_filp->f_pos <= 0) {
				sym_errk(" ERR: offset %lld \n",
					 aop_filp->f_pos);
				goto ERR;
			}

			/* allocate the buffer of table section size */
			dbg_line_buf =
			    (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
							 sizeof(char) *
							 (each_table->size + 4),
							 GFP_KERNEL);
			if (!dbg_line_buf) {
				sym_errk
				    ("error allocating: Insufficient memory\n");
				goto ERR;
			}

			bytesread = aop_filp->f_op->read(aop_filp,
							      dbg_line_buf,
							      (sizeof(char) *
							       (each_table->
								size + 4)),
							      &aop_filp->f_pos);
			if (bytesread < (sizeof(char) * (each_table->size + 4))) {
				sym_errk("ERR: bytesread %d\n", bytesread);
				goto ERR;
			}

			dbg_line_buf[(sizeof(char) * (each_table->size + 4)) - 1] = 0;

			pLine_table =
			    decode_line_info(dbg_line_buf, each_table->size + 4,
					     &offset);
			if (pLine_table) {	/* found the table */

				/* look up the address in table */
				if (lookup_address_in_line_info_table
				    (pLine_table, addr, &tfile_name,
				     &line_no /* linenumber_ptr */)) {
					/* found the address in the table.got the filename and line no */
					found = 1;

					strncpy(pdf_info->df_file_name,
						tfile_name,
						AOP_DF_MAX_FILENAME - 1);
					pdf_info->
					    df_file_name[AOP_DF_MAX_FILENAME -
							 1] = 0;
					pdf_info->df_line_no = line_no;

					/* delete the table, free all allocated memory .. */
					table_delete_address(pLine_table->
							     arange);
					free_line_info_table(pLine_table);

					/* free table memory */
					KDBG_MEM_DBG_KFREE(pLine_table);
					pLine_table = NULL;

					/* free the buffer */
					KDBG_MEM_DBG_KFREE(dbg_line_buf);
					dbg_line_buf = NULL;

					if (aop_filp) {
						filp_close(aop_filp, NULL);
					}
					set_fs(oldfs);
					break;
				}

				/* delete the table, free all allocated memory .. */
				free_line_info_table(pLine_table);
				KDBG_MEM_DBG_KFREE(pLine_table);
				pLine_table = NULL;
			}
			KDBG_MEM_DBG_KFREE(dbg_line_buf);
			dbg_line_buf = NULL;
			if (aop_filp) {
				filp_close(aop_filp, NULL);
			}
			set_fs(oldfs);
		}
	}

	if (found) {
		sym_printk("SUCSESS\n");
		return 1;
	} else {
		strncpy(pdf_info->df_file_name, "??", 3);
		pdf_info->df_file_name[AOP_DF_MAX_FILENAME - 1] = 0;
		pdf_info->df_line_no = 0;
		return 0;
	}

ERR:
	strncpy(pdf_info->df_file_name, "??", 3);
	pdf_info->df_file_name[AOP_DF_MAX_FILENAME - 1] = 0;
	pdf_info->df_line_no = 0;

	sym_errk("error ...\n");
	return 0;
}

/*
 * The function delete all the memory alllocate by the linklist of
 * table of ELf binary */
void kdbg_elf_delete_line_info_table(struct line_table_info *table_info)
{
	struct line_table_info *each_table = NULL;
	struct line_table_info *temp_table = NULL;

	for (each_table = table_info; each_table;) {
		temp_table = each_table->Next;
		table_delete_address(each_table->arange);
		KDBG_MEM_DBG_KFREE(each_table);
		each_table = temp_table;
	}
}
