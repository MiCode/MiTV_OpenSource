
/*
 *  kernel/kdebugd/aop/kdbg_elf_addr2line.h
 *
 *  debug line section parsing module
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010-05-10  Created by gupta.namit@samsung.com
 *
 */



#ifndef __ADDR2LINE_H_
#define __ADDR2LINE_H_

#define COERCE16(x) (((long) (x) ^ 0x8000) - 0x8000)
#define COERCE32(x) (((long) (x) ^ 0x80000000) - 0x80000000)
#define EIGHT_GAZILLION ((int64_t) 1 << 63)
#define COERCE64(x) \
  (((int64_t) (x) ^ EIGHT_GAZILLION) - EIGHT_GAZILLION)

#define FAIL() \
  do { PRINT_KD("%s:%u\n", __FUNCTION__, __LINE__); /*assert(" problem occer");*/} while (0)

#define FILE_ALLOC_CHUNK 5
#define DIR_ALLOC_CHUNK 5


#define IS_DIR_SEPARATOR(c) ((c) == '/')
#define IS_ABSOLUTE_PATH(f) (IS_DIR_SEPARATOR((f)[0]) || (((f)[0]) && ((f)[1] == ':')))

#define FALSE 0
#define TRUE 1

typedef unsigned char byte;


/* Line number opcodes.  */
enum dwarf_line_number_ops {
	DW_LNS_extended_op = 0,
	DW_LNS_copy = 1,
	DW_LNS_advance_pc = 2,
	DW_LNS_advance_line = 3,
	DW_LNS_set_file = 4,
	DW_LNS_set_column = 5,
	DW_LNS_negate_stmt = 6,
	DW_LNS_set_basic_block = 7,
	DW_LNS_const_add_pc = 8,
	DW_LNS_fixed_advance_pc = 9,
	/* DWARF 3.  */
	DW_LNS_set_prologue_end = 10,
	DW_LNS_set_epilogue_begin = 11,
	DW_LNS_set_isa = 12
};

/* Line number extended opcodes.  */
enum dwarf_line_number_x_ops {
	DW_LNE_end_sequence = 1,
	DW_LNE_set_address = 2,
	DW_LNE_define_file = 3,
	DW_LNE_set_discriminator = 4,
	/* HP extensions.  */
	DW_LNE_HP_negate_is_UV_update      = 0x11,
	DW_LNE_HP_push_context             = 0x12,
	DW_LNE_HP_pop_context              = 0x13,
	DW_LNE_HP_set_file_line_column     = 0x14,
	DW_LNE_HP_set_routine_name         = 0x15,
	DW_LNE_HP_set_sequence             = 0x16,
	DW_LNE_HP_negate_post_semantics    = 0x17,
	DW_LNE_HP_negate_function_exit     = 0x18,
	DW_LNE_HP_negate_front_end_logical = 0x19,
	DW_LNE_HP_define_proc              = 0x20,

	DW_LNE_lo_user = 0x80,
	DW_LNE_hi_user = 0xff
};


/* Blocks are a bunch of untyped bytes.  */
struct dwarf_block {
  unsigned int size;
  byte *data;
};


/* line head */
struct line_head {
	unsigned long total_length;
	unsigned short version;
	unsigned long  prologue_length;
	unsigned char minimum_instruction_length;
	unsigned char default_is_stmt;
	int line_base;
	unsigned char line_range;
	unsigned char opcode_base;
	unsigned char *standard_opcode_lengths;
};

/* line information */
struct line_info {
  struct line_info *prev_line;
  unsigned long address;
  char *filename;
  unsigned int line;
  unsigned int column;
  int end_sequence;		/* End of (sequential) code sequence.  */
};

/* file information */
struct fileinfo {
  char *name;
  unsigned int dir;
  unsigned int time;
  unsigned int size;
};

/* adress link list for compilation unit*/
struct arange {
  unsigned long  low;
  unsigned long  high;
  struct arange *next;
};

/* struct contians important table information*/
struct line_table_info {
	struct line_table_info *Next;
	struct line_table_info *Prev;
	unsigned int offset;/* table offset in debug line section */
	unsigned int size;  /* Size of table data */
	struct arange *arange; /* VMA's of table*/
};

/* table containing inforamtion of compilation unit*/
struct line_info_table {
	/* No of files in compilation unit*/
	unsigned int num_files;
	unsigned int num_dirs;	/* No of directories*/
	struct arange *arange; /* Link list for the VMA*/
	char *comp_dir; /* Comp unit directory */
	char **dirs; /* directories pointer */
	struct fileinfo *files; /* File pointer */
	struct line_info *last_line;  /* largest VMA */
	struct line_info *lcl_head;   /* local head; used in 'add_line_info' */
};

/* decoding the table from the bitstream.*/
struct line_info_table*
decode_line_info(char *buff, int size, int *offset);
struct arange *
decode_addr_range_info(char *buff, int size, int *offset);

/* address look up in the given table*/
int lookup_address_in_line_info_table(struct line_info_table *table,
				   unsigned long addr,
				   char **filename_ptr,
				   unsigned int *linenumber_ptr);

/*free all allocated memory */
int free_line_info_table(struct line_info_table *table);

/* delete address link list of address*/
int table_delete_address(struct arange *parange);

/* Check whether table continig address or not. */
int table_contains_address(struct line_table_info *table, unsigned long addr);


#endif  /* __ADDR2LINE_H_ */
