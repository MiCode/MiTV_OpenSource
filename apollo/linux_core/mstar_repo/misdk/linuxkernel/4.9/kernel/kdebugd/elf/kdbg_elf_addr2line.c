
/*
 *  kernel/kdebugd/aop/kdbg_elf_addr2line.c
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
#include <linux/elf.h>
#include <kdebugd.h>



#include "kdbg_elf_def.h"
#include "kdbg_elf_sym_api.h"
#include "kdbg_elf_addr2line.h"


/* lalit: KDBG_MEM_DBG check should be put inside aop_mem.h (e.g. make
 * it empty in that case), not here */
#ifdef KDBG_MEM_DBG
#include "kdbg_util_mem.h"
#endif


#include "kdbg_elf_sym_debug.h"

/**
 * Architecture related functions
 */

/* Big Endian architecture */
#ifdef __BIG_ENDIAN
/* Big endian related function ..*/
	unsigned long
get16(const void *p)
{
	const byte *addr = (const byte *) p;
	return (addr[0] << 8) | addr[1];
}

	long
get_signed_16(const void *p)
{
	const byte *addr = (const byte *) p;
	return COERCE16((addr[0] << 8) | addr[1]);
}


	unsigned long
get32(const void *p)
{
	const byte *addr = (const byte *) p;
	unsigned long v;

	v = (unsigned long) addr[0] << 24;
	v |= (unsigned long) addr[1] << 16;
	v |= (unsigned long) addr[2] << 8;
	v |= (unsigned long) addr[3];
	return v;
}
	long
get_signed_32(const void *p)
{
	const byte *addr = (const byte *) p;
	unsigned long v;

	v = (unsigned long) addr[0] << 24;
	v |= (unsigned long) addr[1] << 16;
	v |= (unsigned long) addr[2] << 8;
	v |= (unsigned long) addr[3];
	return COERCE32(v);
}

	unsigned long long
get64(const void *p)
{
#ifdef HOST_64_BIT
	const byte *addr = (const byte *) p;
	unsigned long long v;

	v  = addr[0]; v <<= 8;
	v |= addr[1]; v <<= 8;
	v |= addr[2]; v <<= 8;
	v |= addr[3]; v <<= 8;
	v |= addr[4]; v <<= 8;
	v |= addr[5]; v <<= 8;
	v |= addr[6]; v <<= 8;
	v |= addr[7];

	return v;
#else
	FAIL();
	return 0;
#endif
}

	long long
get_signed_64(const void *p)
{
#ifdef HOST_64_BIT
	const byte *addr = (const byte *) p;
	unsigned long long v;

	v  = addr[0]; v <<= 8;
	v |= addr[1]; v <<= 8;
	v |= addr[2]; v <<= 8;
	v |= addr[3]; v <<= 8;
	v |= addr[4]; v <<= 8;
	v |= addr[5]; v <<= 8;
	v |= addr[6]; v <<= 8;
	v |= addr[7];

	return COERCE64(v);
#else
	FAIL();
	return 0;
#endif
}
/* Little Endian architecture*/
#else

/* little endian related function ..*/
	unsigned long
get16(const void *p)
{
	const byte *addr = (const byte *) p;
	return (addr[1] << 8) | addr[0];
}

	long
get_signed_16(const void *p)
{
	const byte *addr = (const byte *) p;
	return COERCE16((addr[1] << 8) | addr[0]);
}



	unsigned long
get32(const void *p)
{
	const byte *addr = (const byte *) p;
	unsigned long v;

	v = (unsigned long) addr[0];
	v |= (unsigned long) addr[1] << 8;
	v |= (unsigned long) addr[2] << 16;
	v |= (unsigned long) addr[3] << 24;
	return v;
}

	long
get_signed_32(const void *p)
{
	const byte *addr = (const byte *) p;
	unsigned long v;

	v = (unsigned long) addr[0];
	v |= (unsigned long) addr[1] << 8;
	v |= (unsigned long) addr[2] << 16;
	v |= (unsigned long) addr[3] << 24;
	return COERCE32(v);
}
	unsigned long long
get64(const void *p)
{
#ifdef HOST_64_BIT
	const byte *addr = (const byte *) p;
	unsigned long long v;

	v  = addr[7]; v <<= 8;
	v |= addr[6]; v <<= 8;
	v |= addr[5]; v <<= 8;
	v |= addr[4]; v <<= 8;
	v |= addr[3]; v <<= 8;
	v |= addr[2]; v <<= 8;
	v |= addr[1]; v <<= 8;
	v |= addr[0];

	return v;
#else
	FAIL();
	return 0;
#endif

}

	long long
get_signed_64(const void *p)
{
#ifdef HOST_64_BIT
	const byte *addr = (const byte *) p;
	unsigned long long v;

	v  = addr[7]; v <<= 8;
	v |= addr[6]; v <<= 8;
	v |= addr[5]; v <<= 8;
	v |= addr[4]; v <<= 8;
	v |= addr[3]; v <<= 8;
	v |= addr[2]; v <<= 8;
	v |= addr[1]; v <<= 8;
	v |= addr[0];

	return COERCE64(v);
#else
	FAIL();
	return 0;
#endif
}

#endif

	unsigned long
read_unsigned_leb128(byte *buf,
		unsigned int *bytes_read_ptr)
{
	unsigned long result;
	unsigned int num_read;
	unsigned int shift;
	unsigned char byte;

	result = 0;
	shift = 0;
	num_read = 0;
	do {
		byte = (*(unsigned char *) (buf) & 0xff)  /*get8 (buf)*/;
		buf++;
		num_read++;
		result |= (((unsigned long) byte & 0x7f) << shift);
		shift += 7;
	} while (byte & 0x80);
	*bytes_read_ptr = num_read;
	return result;
}


	long
read_signed_leb128(byte *buf,
		unsigned int *bytes_read_ptr)
{
	unsigned long result;
	unsigned int shift;
	unsigned int num_read;
	unsigned char byte;

	result = 0;
	shift = 0;
	num_read = 0;
	do {
		byte =  (*(unsigned char *) (buf) & 0xff) /* get8(buf)*/;
		buf++;
		num_read++;
		result |= (((unsigned long) byte & 0x7f) << shift);
		shift += 7;
	} while (byte & 0x80);
	if (shift < 8 * sizeof(result) && (byte & 0x40))
		result |= (((unsigned long) -1) << shift);
	*bytes_read_ptr = num_read;
	return result;
}


	static unsigned long long
read_address(byte *buf)
{
	return get32(buf);
}



/* VERBATIM
   The following function up to the END VERBATIM mark are
   copied directly from dwarf2read.c.  */

/* Read dwarf information from a buffer.  */

	static unsigned int
read_1_byte(byte *buf)
{
	unsigned int ret = (*(unsigned char *) (buf) & 0xff);
	return ret;
}

	static int
read_1_signed_byte(byte *buf)
{
	int ret = (((*(unsigned char *) (buf) & 0xff) ^ 0x80) - 0x80);
	return ret;
}

	static unsigned int
read_2_bytes(byte *buf)
{
	return get16(buf);
}

	static unsigned int
read_4_bytes(byte *buf)
{
	return get32(buf);
}

	static unsigned long long
read_8_bytes(byte *buf)
{
	return get64(buf);
}

#if 0 /* Function not in used, avoid compilation warning */
	static byte *
read_n_bytes(byte *buf, unsigned int size)
{
	return buf;
}
#endif
	static char *
read_string(byte *buf,
		unsigned int *bytes_read_ptr)
{
	/* Return a pointer to the embedded string.  */
	char *str = (char *) buf;

	if (*str == '\0') {
		*bytes_read_ptr = 1;
		return NULL;
	}

	*bytes_read_ptr = strlen(str) + 1;
	return str;
}

/* END VERBATIM */

/* struct alloc_size */
/* { */
/* 	 */
int total_allocated;
/*} */
/* int total_free =0; */

void *df_malloc(int size)
{
	char *ptr = NULL;
	ptr = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, size, GFP_KERNEL);
	if (ptr) {
		total_allocated += size;
		return ptr;
	} else {
		return NULL;
	}
}

void df_free(void *ptr)
{
	if (ptr) {
		KDBG_MEM_DBG_KFREE(ptr);
	}
}

/*
 * The function work as a realloc function of libc
 * the function used when more memory require for same  pointer
 *
 * logic used for this function is: allocate memory of new pointer size.
 * copy the content of old pointer to new pointer. delete the old pointer
 * and assign new pointer of old pointer .
 *
 */
static void *aop_elf_realloc(void *old_ptr, size_t old_size, size_t new_size)
{
	void *buf;

	buf = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, new_size, GFP_KERNEL);
	if (!buf)
		return NULL;

	if (old_ptr) {
		memcpy(buf, old_ptr, min(old_size, new_size));
		KDBG_MEM_DBG_KFREE(old_ptr);
	}
	return buf;
}


/* function is like strdup() function in libc
 * duplicate the string
 * the memory is allocated for the new string
 * user should free that memory */
static char *df_strdup(const char *s)
{
	char *p = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, (strlen(s) + 1), GFP_KERNEL);
	if (p) {
		strncpy(p, s, strlen(s));
		p[strlen(s)] = 0;
	}
	return p;
}


static char *
concat_filename(struct line_info_table *table, unsigned int file);

static inline int
new_line_sorts_after(struct line_info *new_line, struct line_info *line)
{
	return (new_line->address > line->address
			|| (new_line->address == line->address
				&& new_line->end_sequence < line->end_sequence));
}

/* Free the stored link list of VMA's*/
int table_delete_address(struct arange *parange)
{
	struct arange *arange;
	struct arange *tmp_arange;

	arange = parange;

	while (arange) {
		tmp_arange = arange->next;
		df_free(arange);
		arange = tmp_arange;
	}

	return TRUE;
}

/* search the table containing the address
 * if address given address falls between high low pc return true else false.
 * */
int table_contains_address(struct line_table_info *table, unsigned long addr)
{
	struct arange *arange;

	if (table->arange) {
		arange = table->arange;
		do {
			if (addr >= arange->low && addr < arange->high)
				return TRUE;
			arange = arange->next;
		} while (arange);
	}

	return FALSE;
}


/* function adding the line information to the table.*/
	static void
add_line_info(struct line_info_table *table,
		unsigned long address,
		char *filename,
		unsigned int line,
		unsigned int column,
		int end_sequence)
{
	unsigned long amt = sizeof(struct line_info);
	struct line_info *info = (struct line_info *) df_malloc(amt);

	if (!info) {
		sym_errk("error allocating: Insufficient memory\n");
		return;
	}

	/* Set member data of 'info'.  */
	info->address = address;
	info->line = line;
	info->column = column;
	info->end_sequence = end_sequence;

	if (filename && filename[0]) {
		int file_len = strlen(filename) ;
		info->filename = (char *) df_malloc(file_len + 1);
		if (info->filename) {
			/* SISC: Change strcpy to strncpy prevent warning fix*/
			strncpy(info->filename, filename, file_len);
			info->filename[file_len] = 0;
		}
	} else
		info->filename = NULL;

	/* Find the correct location for 'info'.  Normally we will receive
	   new line_info data 1) in order and 2) with increasing VMAs.
	   However some compilers break the rules (cf. decode_line_info) and
	   so we include some heuristics for quickly finding the correct
	   location for 'info'. In particular, these heuristics optimize for
	   the common case in which the VMA sequence that we receive is a
	   list of locally sorted VMAs such as
	   p...z a...j  (where a < j < p < z)

Note: table->lcl_head is used to head an *actual* or *possible*
sequence within the list (such as a...j) that is not directly
headed by table->last_line

Note: we may receive duplicate entries from 'decode_line_info'.  */

	if (table->last_line
			&& table->last_line->address == address
			&& table->last_line->end_sequence == end_sequence) {
		/* We only keep the last entry with the same address and end
		   sequence.  See PR ld/4986.  */
		if (table->lcl_head == table->last_line)
			table->lcl_head = info;
		info->prev_line = table->last_line->prev_line;

		/*remove dulicate entry*/
		if (table->last_line->filename)
			df_free(table->last_line->filename);
		df_free(table->last_line);
		table->last_line = info;
	} else if (!table->last_line
			|| new_line_sorts_after(info, table->last_line)) {
		/* Normal case: add 'info' to the beginning of the list */
		info->prev_line = table->last_line;
		table->last_line = info;

		/* lcl_head: initialize to head a *possible* sequence at the end.  */
		if (!table->lcl_head)
			table->lcl_head = info;
	} else if (!new_line_sorts_after(info, table->lcl_head)
			&& (!table->lcl_head->prev_line
				|| new_line_sorts_after(info, table->lcl_head->prev_line))) {
		/* Abnormal but easy: lcl_head is the head of 'info'.  */
		info->prev_line = table->lcl_head->prev_line;
		table->lcl_head->prev_line = info;
	} else {
		/* Abnormal and hard: Neither 'last_line' nor 'lcl_head' are valid
		   heads for 'info'.  Reset 'lcl_head'.  */
		struct line_info *li2 = table->last_line; /* always non-NULL */
		struct line_info *li1 = li2->prev_line;

		while (li1) {
			if (!new_line_sorts_after(info, li2)
					&& new_line_sorts_after(info, li1))
				break;

			li2 = li1; /* always non-NULL */
			li1 = li1->prev_line;
		}
		table->lcl_head = li2;
		info->prev_line = table->lcl_head->prev_line;
		table->lcl_head->prev_line = info;
	}
}

/*
 * The function arranges found VMA's in table (compilation unit)
 * actually this is a linklist of VMA's found while parsing the
 * compilation unit.
 */
	static void
arange_add(struct arange **first_arange, unsigned long  low_pc, unsigned long  high_pc)
{
	struct arange *arange;

	if (!*first_arange) {
		*first_arange = (struct arange *) df_malloc(sizeof(struct arange));
		if (!*first_arange) {
			sym_errk("ERR: Not Sufficient memory\n");
			return;
		}
		memset(*first_arange, 0, sizeof(struct arange));
	}


	/* If the first arrange is empty, use it. */
	if ((*first_arange)->high == 0) {
		(*first_arange)->low = low_pc;
		(*first_arange)->high = high_pc;
		return;
	}

	/* Next see if we can cheaply extend an existing range.  */
	arange = *first_arange;
	do {
		if (low_pc == arange->high) {
			arange->high = high_pc;
			return;
		}
		if (high_pc == arange->low) {
			arange->low = low_pc;
			return;
		}
		arange = arange->next;
	} while (arange);

	/* Need to allocate a new arrange and insert it into the arrange list.
	 * Order isn't significant, so just insert after the first arrange.
	 */
	arange = (struct arange *) df_malloc(sizeof(struct arange));
	arange->low = low_pc;
	arange->high = high_pc;
	arange->next = (*first_arange)->next;
	(*first_arange)->next = arange;
}


/*
 * The function deocde table from the bitstream of ELF.
 * the parsing of bitstream is a state machine.
 * with the DWARF 2 specs, the parsing logic can be identifiied.
 * though this is not a full fledged parsing of dwarf spec
 * only debug_line table parsing is included
 */
struct line_info_table*
decode_line_info(char *buff, int size, int *offset)
{
	/* bfd *abfd = unit->abfd; */
	struct line_info_table *table;
	byte *line_ptr;
	byte *line_end;
	struct line_head lh;
	unsigned int i, bytes_read, offset_size;
	char *cur_file, *cur_dir;
	unsigned char op_code, extended_op, adj_opcode;
	unsigned long amt;

	static int file_alloc_size;
	static int dir_alloc_size;

#ifdef KDBG_DWARF_64_BIT_SUPPORT
	/* Namit: Hardcoded value, value extracted from debug info section
	 * and we are not parsing that section applies for 32bit system
	 * not worked for 64 bit system
	 */
	int address_size = 4; /* TODO:provide provision for 32 as well 64 bit system */
#endif

	amt = sizeof(struct line_info_table);
	table = (struct line_info_table *) df_malloc(amt);
	if (!table) {
		sym_errk("error allocating: Insufficient memory\n");
		return NULL;
	}
	memset(table, 0, amt);
	table->comp_dir = NULL;

	table->num_files = 0;
	table->files = NULL;

	table->num_dirs = 0;
	table->dirs = NULL;

	table->files = NULL;
	table->last_line = NULL;
	table->lcl_head = NULL;

	line_ptr = buff;

	/* Read in the prologue.  */
	lh.total_length = read_4_bytes(line_ptr);
	*offset += lh.total_length;

	line_ptr += 4;
	offset_size = 4;

	/* The code is for 64 bit system, not handled yet */
	if (lh.total_length == 0xffffffff) {
		lh.total_length = read_8_bytes(line_ptr);
		line_ptr += 8;
		offset_size = 8;
	}
	/* To remove compiler warnig, as we support only 32 bit */
#ifdef KDBG_DWARF_64_BIT_SUPPORT
	else if (lh.total_length == 0 && address_size == 8) {
		/* Handle(non-standard) 64-bit DWARF2 formats.  */
		lh.total_length = read_4_bytes(line_ptr);
		line_ptr += 4;
		offset_size = 8;
	}
#endif
	line_end = line_ptr + lh.total_length;
	lh.version = read_2_bytes(line_ptr);
	line_ptr += 2;
	if (offset_size == 4)
		lh.prologue_length = read_4_bytes(line_ptr);
	else
		lh.prologue_length = read_8_bytes(line_ptr);
	line_ptr += offset_size;
	lh.minimum_instruction_length = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.default_is_stmt = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.line_base = read_1_signed_byte(line_ptr);
	line_ptr += 1;
	lh.line_range = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.opcode_base = read_1_byte(line_ptr);
	line_ptr += 1;
	amt = lh.opcode_base * sizeof(unsigned char);

	lh.standard_opcode_lengths = (unsigned char *) df_malloc(amt);
	if (!lh.standard_opcode_lengths) {
		sym_errk("error allocating: Insufficient memory\n");
		return table; /* Return imcomplete table, allocated memory will be free outside*/
	}

	lh.standard_opcode_lengths[0] = 1;

	for (i = 1; i < lh.opcode_base; ++i) {
		lh.standard_opcode_lengths[i] = read_1_byte(line_ptr);
		line_ptr += 1;
	}

	file_alloc_size = 0;
	dir_alloc_size = 0;
	/* Read directory table.  */
	while ((cur_dir = read_string(line_ptr, &bytes_read)) != NULL)	{
		line_ptr += bytes_read;

		if ((table->num_dirs % DIR_ALLOC_CHUNK) == 0) {
			char **tmp;

			amt = table->num_dirs + DIR_ALLOC_CHUNK;
			amt *= sizeof(char *);

			tmp = (char **) aop_elf_realloc(table->dirs, dir_alloc_size, amt);
			if (tmp == NULL) {
				df_free(table->dirs);
				if (lh.standard_opcode_lengths)
					df_free(lh.standard_opcode_lengths);
				return table;/* Return imcomplete table, allocated memory will be free outside*/
			}
			table->dirs = tmp;
			dir_alloc_size = amt;
		}

		table->dirs[table->num_dirs++] = cur_dir;
	}

	line_ptr += bytes_read;

	/* Read file name table.  */
	while ((cur_file = read_string(line_ptr, &bytes_read)) != NULL)	{
		line_ptr += bytes_read;

		if ((table->num_files % FILE_ALLOC_CHUNK) == 0)	{
			struct fileinfo *tmp;

			amt = table->num_files + FILE_ALLOC_CHUNK;
			amt *= sizeof(struct fileinfo);

			tmp = (struct fileinfo *) aop_elf_realloc(table->files, file_alloc_size, amt);
			if (tmp == NULL) {
				df_free(table->files);
				df_free(table->dirs);
				if (lh.standard_opcode_lengths)
					df_free(lh.standard_opcode_lengths);
				return table;/* Return imcomplete table, allocated memory will be free outside*/
			}
			file_alloc_size = amt;
			table->files = tmp;
		}

		table->files[table->num_files].name = cur_file;
		table->files[table->num_files].dir =
			read_unsigned_leb128(line_ptr, &bytes_read);
		line_ptr += bytes_read;
		table->files[table->num_files].time =
			read_unsigned_leb128(line_ptr, &bytes_read);
		line_ptr += bytes_read;
		table->files[table->num_files].size =
			read_unsigned_leb128(line_ptr, &bytes_read);
		line_ptr += bytes_read;
		table->num_files++;
	}

	line_ptr += bytes_read;

	/* Read the statement sequences until there's nothing left.  */
	while (line_ptr < line_end) {

		/* State machine registers.  */
		unsigned long address = 0;
		char *filename = table->num_files ? concat_filename(table, 1) : NULL;
		unsigned int line = 1;
		unsigned int column = 0;
		int is_stmt = lh.default_is_stmt;
		int end_sequence = 0;
		/* eraxxon@alumni.rice.edu: Against the DWARF2 specs, some
		   compilers generate address sequences that are wildly out of
		   order using DW_LNE_set_address (e.g. Intel C++ 6.0 compiler
		   for ia64-Linux).  Thus, to determine the low and high
		   address, we must compare on every DW_LNS_copy, etc.  */
		unsigned long low_pc  = (unsigned long) -1;
		unsigned long high_pc = 0;

		/* Decode the table.  */
		while (!end_sequence) {

			op_code = read_1_byte(line_ptr);
			line_ptr += 1;

			if (op_code >= lh.opcode_base) {

				/* Special operand.  */
				adj_opcode = op_code - lh.opcode_base;
				address += (adj_opcode / lh.line_range)
					* lh.minimum_instruction_length;
				line += lh.line_base + (adj_opcode % lh.line_range);
				/* Append row to matrix using current values.  */
				add_line_info(table, address, filename, line, column, 0);
				if (address < low_pc)
					low_pc = address;
				if (address > high_pc)
					high_pc = address;
			} else
				switch (op_code) {
				case DW_LNS_extended_op:
					/* Ignore length.  */
					line_ptr += 1;
					extended_op = read_1_byte(line_ptr);
					line_ptr += 1;

					switch (extended_op) {
					case DW_LNE_end_sequence:
						end_sequence = 1;
						add_line_info(table, address, filename, line, column,
								end_sequence);
						if (address < low_pc)
							low_pc = address;
						if (address > high_pc)
							high_pc = address;
						arange_add(&table->arange, low_pc, high_pc);
						break;
					case DW_LNE_set_address:
						address = read_address(line_ptr);
						line_ptr += 4; /*unit->addr_size; */
						break;
					case DW_LNE_define_file:
						cur_file = read_string(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						if ((table->num_files % FILE_ALLOC_CHUNK) == 0)	{
							struct fileinfo *tmp;

							amt = table->num_files + FILE_ALLOC_CHUNK;
							amt *= sizeof(struct fileinfo);
							tmp = (struct fileinfo *) aop_elf_realloc
								(table->files, file_alloc_size, amt);

							if (tmp == NULL) {
								df_free(table->files);
								df_free(table->dirs);
								df_free(filename);
								/* Return imcomplete table, allocated memory will be free outside */
								if (lh.standard_opcode_lengths)
									df_free(lh.standard_opcode_lengths);

								return table;
							}

							file_alloc_size = amt;
							table->files = tmp;
						}
						table->files[table->num_files].name = cur_file;
						table->files[table->num_files].dir =
							read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						table->files[table->num_files].time =
							read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						table->files[table->num_files].size =
							read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						table->num_files++;
						break;
					case DW_LNE_set_discriminator:
						(void) read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						break;
					default:
						df_free(filename);
						df_free(table->files);
						df_free(table->dirs);
						filename = NULL;
						table->files = NULL;
						table->dirs = NULL;
						if (lh.standard_opcode_lengths)
							df_free(lh.standard_opcode_lengths);
						return table ;
					}
					break;
				case DW_LNS_copy:
					add_line_info(table, address, filename, line, column, 0);
					if (address < low_pc)
						low_pc = address;
					if (address > high_pc)
						high_pc = address;
					break;
				case DW_LNS_advance_pc:
					address += lh.minimum_instruction_length
						* read_unsigned_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_advance_line:
					line += read_signed_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_set_file:
					{
						unsigned int file;

						/* The file and directory tables are 0
						   based, the references are 1 based.  */
						file = read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;

						if (filename)
							df_free(filename);
						filename = concat_filename(table, file);

						break;
					}
				case DW_LNS_set_column:
					column = read_unsigned_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_negate_stmt:
					is_stmt = (!is_stmt);
					break;
				case DW_LNS_set_basic_block:
					break;
				case DW_LNS_const_add_pc:
					address += lh.minimum_instruction_length
						* ((255 - lh.opcode_base) / lh.line_range);
					break;
				case DW_LNS_fixed_advance_pc:
					address += read_2_bytes(line_ptr);
					line_ptr += 2;
					break;
				default:
					{
						int i;

						/* Unknown standard opcode, ignore it.  */
						for (i = 0; i < lh.standard_opcode_lengths[op_code]; i++) {
							(void) read_unsigned_leb128(line_ptr, &bytes_read);
							line_ptr += bytes_read;
						}
					}
				}
		}

		if (filename)
			df_free(filename);
	}

	if (lh.standard_opcode_lengths)
		df_free(lh.standard_opcode_lengths);
	return table;
}



struct arange *
decode_addr_range_info(char *buff, int size, int *offset)
{
	byte *line_ptr;
	byte *line_end;
	struct line_head lh;
	unsigned int i, bytes_read, offset_size;
	char *cur_file, *cur_dir;
	unsigned char op_code, extended_op, adj_opcode;
	uint32_t amt;
	struct arange *tarange = NULL; /* Link list for the VMA*/

	static int file_alloc_size;
	static int dir_alloc_size;

#ifdef KDBG_DWARF_64_BIT_SUPPORT
	/* Namit: Hardcoded value, value extracted from debug info section
	 * and we are not parsing that section applies for 32bit system
	 * not worked for 64 bit system
	 */
	int address_size = 4; /* TODO:provide provision for 32 as well 64 bit system */
#endif

	line_ptr = (uint8_t *)buff;

	/* Read in the prologue.  */
	lh.total_length = read_4_bytes(line_ptr);
	*offset += lh.total_length;

	line_ptr += 4;
	offset_size = 4;

	/* The code is for 64 bit system, not handled yet */
	if (lh.total_length == 0xffffffff) {
		lh.total_length = read_8_bytes(line_ptr);
		line_ptr += 8;
		offset_size = 8;
	}
	/* To remove compiler warnig, as we support only 32 bit */
#ifdef KDBG_DWARF_64_BIT_SUPPORT
	else if (lh.total_length == 0 && address_size == 8) {
		/* Handle(non-standard) 64-bit DWARF2 formats.  */
		lh.total_length = read_4_bytes(line_ptr);
		line_ptr += 4;
		offset_size = 8;
	}
#endif
	line_end = line_ptr + lh.total_length;
	lh.version = read_2_bytes(line_ptr);
	line_ptr += 2;
	if (offset_size == 4)
		lh.prologue_length = read_4_bytes(line_ptr);
	else
		lh.prologue_length = read_8_bytes(line_ptr);
	line_ptr += offset_size;
	lh.minimum_instruction_length = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.default_is_stmt = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.line_base = read_1_signed_byte(line_ptr);
	line_ptr += 1;
	lh.line_range = read_1_byte(line_ptr);
	line_ptr += 1;
	lh.opcode_base = read_1_byte(line_ptr);
	line_ptr += 1;
	amt = lh.opcode_base * sizeof(unsigned char);

	lh.standard_opcode_lengths = (unsigned char *) df_malloc(amt);
	if (!lh.standard_opcode_lengths) {
		sym_errk("error allocating: Insufficient memory\n");
		return tarange; /* Return imcomplete table, allocated memory will be free outside*/
	}

	lh.standard_opcode_lengths[0] = 1;

	for (i = 1; i < lh.opcode_base; ++i) {
		lh.standard_opcode_lengths[i] = read_1_byte(line_ptr);
		line_ptr += 1;
	}

	file_alloc_size = 0;
	dir_alloc_size = 0;

	/* Read directory table.  */
	while ((cur_dir = read_string(line_ptr, &bytes_read)) != NULL)	{
		line_ptr += bytes_read;
	}

	line_ptr += bytes_read;

	/* Read file name table.  */
	while ((cur_file = read_string(line_ptr, &bytes_read)) != NULL)	{
		line_ptr += bytes_read;
		/* Read DIR */
		read_unsigned_leb128(line_ptr, &bytes_read);
		line_ptr += bytes_read;
		/* Read time */
		read_unsigned_leb128(line_ptr, &bytes_read);
		line_ptr += bytes_read;
		/* Read Size */
		read_unsigned_leb128(line_ptr, &bytes_read);

		line_ptr += bytes_read;
	}

	line_ptr += bytes_read;

	/* Read the statement sequences until there's nothing left.  */
	while (line_ptr < line_end) {

		/* State machine registers.  */
		uint32_t address = 0;
		unsigned int line = 1;
		int is_stmt = lh.default_is_stmt;
		int end_sequence = 0;
		/* eraxxon@alumni.rice.edu: Against the DWARF2 specs, some
		   compilers generate address sequences that are wildly out of
		   order using DW_LNE_set_address (e.g. Intel C++ 6.0 compiler
		   for ia64-Linux).  Thus, to determine the low and high
		   address, we must compare on every DW_LNS_copy, etc.  */
		uint32_t low_pc  = (uint32_t) -1;
		uint32_t high_pc = 0;

		/* Decode the table.  */
		while (!end_sequence) {

			op_code = read_1_byte(line_ptr);
			line_ptr += 1;

			if (op_code >= lh.opcode_base) {

				/* Special operand.  */
				adj_opcode = op_code - lh.opcode_base;
				address += (adj_opcode / lh.line_range)
					* lh.minimum_instruction_length;
				line += lh.line_base + (adj_opcode % lh.line_range);
				if (address < low_pc)
					low_pc = address;
				if (address > high_pc)
					high_pc = address;
			} else
				switch (op_code) {
				case DW_LNS_extended_op:
					/* Ignore length.  */
					line_ptr += 1;
					extended_op = read_1_byte(line_ptr);
					line_ptr += 1;

					switch (extended_op) {
					case DW_LNE_end_sequence:
						end_sequence = 1;
						if (address < low_pc)
							low_pc = address;
						if (address > high_pc)
							high_pc = address;
						arange_add(&tarange, low_pc, high_pc);
						break;
					case DW_LNE_set_address:
						address = read_address(line_ptr);
						line_ptr += 4; /*unit->addr_size; */
						break;
					case DW_LNE_define_file:
						cur_file = read_string(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						break;
					case DW_LNE_set_discriminator:
						(void) read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						break;
					default:
						if (lh.standard_opcode_lengths)
							df_free(lh.standard_opcode_lengths);
						return tarange ;
					}
					break;
				case DW_LNS_copy:
					if (address < low_pc)
						low_pc = address;
					if (address > high_pc)
						high_pc = address;
					break;
				case DW_LNS_advance_pc:
					address += lh.minimum_instruction_length
						* read_unsigned_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_advance_line:
					line += read_signed_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_set_file:
					{
						read_unsigned_leb128(line_ptr, &bytes_read);
						line_ptr += bytes_read;
						break;
					}
				case DW_LNS_set_column:
					read_unsigned_leb128(line_ptr, &bytes_read);
					line_ptr += bytes_read;
					break;
				case DW_LNS_negate_stmt:
					is_stmt = (!is_stmt);
					break;
				case DW_LNS_set_basic_block:
					break;
				case DW_LNS_const_add_pc:
					address += lh.minimum_instruction_length
						* ((255 - lh.opcode_base) / lh.line_range);
					break;
				case DW_LNS_fixed_advance_pc:
					address += read_2_bytes(line_ptr);
					line_ptr += 2;
					break;
				default:
					{
						int t;
						/* Unknown standard opcode, ignore it.  */
						for (t = 0; t < lh.standard_opcode_lengths[op_code]; t++) {
							(void) read_unsigned_leb128(line_ptr, &bytes_read);
							line_ptr += bytes_read;
						}
					}
				}
		}

	}

	if (lh.standard_opcode_lengths)
		df_free(lh.standard_opcode_lengths);
	return tarange;
}

	static char *
concat_filename(struct line_info_table *table, unsigned int file)
{
	char *filename;

	if (file - 1 >= table->num_files) {
		/* FILE == 0 means unknown.  */
		if (file) {
			sym_errk("Dwarf Error: mangled line number section (bad file number).\n");
			return df_strdup("<unknown>");
		}
	}

	filename = table->files[file - 1].name;

	if (!IS_ABSOLUTE_PATH(filename)) {
		char *dirname = NULL;
		char *subdirname = NULL;
		char *name;
		size_t len;
		if (table->files[file - 1].dir)
			subdirname = table->dirs[table->files[file - 1].dir - 1];

		if (!subdirname || !IS_ABSOLUTE_PATH(subdirname))
			dirname = table->comp_dir;

		if (!dirname) {
			dirname = subdirname;
			subdirname = NULL;
		}
		if (!dirname) {
			return df_strdup(filename);
		}
		len = strlen(dirname) + strlen(filename) + 2;

		if (subdirname) {
			len += strlen (subdirname) + 1;
			name = (char *)df_malloc(len);
			if (name) {
				/*SISC: Used snprintf to remove prevent warning*/
				snprintf(name, len, "%s/%s/%s", dirname, subdirname, filename);
				name[len-1] = 0;
			}
		} else {
			name = (char *)df_malloc (len);
			if (name) {
				/*SISC: Used snprintf to remove prevent warning*/
				snprintf(name, len, "%s/%s", dirname, filename);
				name[len-1] = 0;
			}
		}
		return name;
	}
	return df_strdup(filename);

}

	int
lookup_address_in_line_info_table(struct line_info_table *table,
		unsigned long addr,
		char **filename_ptr,
		unsigned int *linenumber_ptr)
{
	/* Note: table->last_line should be a descending sorted list. */
	struct line_info *each_line;

	for (each_line = table->last_line;
			each_line;
			each_line = each_line->prev_line)
		if (addr >= each_line->address)
			break;

	if (each_line
			&& !(each_line->end_sequence || each_line == table->last_line))	{
		*filename_ptr = each_line->filename;
		*linenumber_ptr = each_line->line;
		return TRUE;
	}

	*filename_ptr = NULL;
	return FALSE;
}

	int /*bool*/
free_line_info_table(struct line_info_table *table)
{
	struct line_info *each_line;
	struct line_info *prev_line;

	if (table->comp_dir)	{
		df_free(table->comp_dir);
		table->comp_dir = NULL;
	}
	if (table->dirs)	{
		df_free(table->dirs);
		table->dirs = NULL;
	}
	if (table->files) {
		df_free(table->files);
		table->files = NULL;
	}


	if (table->last_line) {
		for (each_line = table->last_line;
				each_line;) {
			prev_line = each_line;
			each_line = each_line->prev_line;
			if (prev_line->filename)
				df_free(prev_line->filename);
			df_free(prev_line);
		}
	}
	table->last_line = NULL;
	if (table->lcl_head) {
		/* 		free(table->lcl_head); */
	}
	return 1;
}
