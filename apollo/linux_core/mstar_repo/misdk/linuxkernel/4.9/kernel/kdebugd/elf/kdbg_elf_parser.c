/*
 *  kernel/kdebugd/elf/kdbg_elf_elf_parser.c
 *
 *  ELF Symbol Table Parsing Module
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-07-21  Created by gaurav.j@samsung.com
 *
 */

/********************************************************************
  INCLUDE FILES
 ********************************************************************/
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/bootmem.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/elf.h>
#include <linux/sort.h>
#include <kdebugd.h>
#include <linux/syscalls.h>

#include "kdbg_elf_def.h"
#include "kdbg_elf_sym_api.h"

#define KDBG_ELF_DEBUG_ON 0
#define SYM_DEBUG_ON  0
#include "kdbg_elf_sym_debug.h"

#ifdef CONFIG_DWARF_MODULE
#include "kdbg_elf_addr2line.h"
#include "kdbg_elf_dwarf.h"
#endif /* CONFIG_DWARF_MODULE */

/* Pre defined directory path in USB where ELF should be placed */
#define KDBG_ELF_PATH "aop_bin"

/*  Symbol name parsing  */
#define KDBG_ELF_SYM_NAME(X, Y, Z)	\
	((X) == NULL ? "<none>" \
	 : (Y) == NULL ? "<no-name>" \
	 : ((X)->st_name >= (Z) ? "<corrupt>" \
		 : (Y) + (X)->st_name))

#ifdef CONFIG_DWARF_MODULE
#define KDBG_ELF_SECTION_NAME(X, Y, Z)	\
	((X) == NULL ? "<none>" \
	 : (Y) == NULL ? "<no-name>" \
	 : ((X)->sh_name >= (Z) ? "<corrupt>" \
		 :  (Y) + (X)->sh_name))
#endif /* CONFIG_DWARF_MODULE */

/* Enable/ Disable Symbol Parsing (Enable = 1, Disable = 0)
 * It is for disabling the reading of ELF files for function name in low-mem case
 * It can be used in case any side-effect is to be avoided in low-mem case,
 * for temporary diagnostic purposes.  In that case ELF reading for function name
 * will not happen while searching for function name in ELF database */
static int  kdbg_elf_config_low_mem_read_enable = 1;

/* string for adv update ELF database */
static const char *elf_config_elf_load_string[2] = {
	"Destructive",		/* Update ELF Database as Destructive*/
	"Additive", 	/* Update ELF Database as Additive*/
};

/* elf additive/ substractive option for download the ELF
   database from USB. By default it is Additive. */
int elf_config_additive = 1;

/* string for adv update ELF database */
static const char *kdbg_elf_module_disable_enable_string[2] = {
	"Disable",		/* Runtime Setting to Disable ELF Module*/
	"Enable ", 	/* Runtime Setting to Enable ELF Module.*/
};

/* Runtime Setting to Enable/ Disable ELF Module (Enable = 1, Disable = 0) */
int config_kdbg_elf_module_enable_disable = 1;

/* string for adv update ELF database */
static const char *kdbg_elf_low_mem_enable_disable_sym_parsing[2] = {
	"Enable",	/* Enable ELF reading in file (for low-memory) for symbol name.*/
	"Disable ", 	/* Disable ELF reading in file (for low-memory) for symbol name.*/
};

typedef enum {
	KDBG_ELF_SYM_GAP = 1,
	KDBG_ELF_SYM_OVERLAP
} kdbg_elf_sym_err_chk;

/* symbol Loading Scenarios */
typedef enum {
	KDBG_ELF_LOAD_DEFAULT = 1, /*KDBG_ELF_LOAD_DEFAULT */
	KDBG_ELF_LOAD_LOW_MEM,
	KDBG_ELF_LOAD_FULL_MEM,
	KDBG_ELF_LOAD_VIRT_ADDR, /* load virtual addr of ELF without Symbol  */
	KDBG_ELF_LOAD_NO_ELF /* No ELF Laoding */
} kdbg_elf_mem_sym_load;

static int kdbg_elf_elf_load_option = KDBG_ELF_LOAD_DEFAULT;

/* Print a VMA value.  */
/* How to print a vma value.  */
/* Represent a target address.  Also used as a generic unsigned type
   which is guaranteed to be big enough to hold any arithmetic types
   we need to deal with.  */
typedef unsigned long kdbg_elf_pvma;

typedef enum kdbg_elf_print_mode {
	HEX,
	DEC,
	DEC_5,
	UNSIGNED,
	PREFIX_HEX,
	FULL_HEX,
	LONG_HEX
} kdbg_elf_print_mode;



/* This member holds the head point of ELF name stored in database of ELF files */
LIST_HEAD(kdbg_elf_usb_head_item);

/* This member holds the head point of Symbol related info */
/* static struct list_head *kdbg_elf_head_item[KDBG_ELF_MAX_FILES];  */

kdbg_elf_symbol_load_notification kdbg_elf_sym_load_notification_func;

static char *kdbg_elf_get_file_type (unsigned e_type)
{
	static char buff[32];

	switch (e_type) {
	case ET_NONE:	return ("NONE (None)");
	case ET_REL:	return ("REL (Relocatable file)");
	case ET_EXEC:	return ("EXEC (Executable file)");
	case ET_DYN:	return ("DYN (Shared object file)");
	case ET_CORE:	return ("CORE (Core file)");

	default:
			if ((e_type >= ET_LOPROC) && (e_type <= ET_HIPROC))
				snprintf (buff, sizeof (buff), ("Processor Specific: (%x)"), e_type);
			else if ((e_type >= ET_LOOS) && (e_type <= ET_HIOS))
				snprintf (buff, sizeof (buff), ("OS Specific: (%x)"), e_type);
			else
				snprintf (buff, sizeof (buff), ("<unknown>: %x"), e_type);
			return buff;
	}
}

	static char *
kdbg_elf_get_machine_name (unsigned e_machine)
{
	static char buff[64];

	switch (e_machine) {
	case EM_NONE:		return "None";
	case EM_M32:		return "WE32100";
	case EM_SPARC:		return "Sparc";
	case EM_SPU:		return "SPU";
	case EM_386:		return "Intel 80386";
	case EM_68K:		return "MC68000";
	case EM_88K:		return "MC88000";
	case EM_486:		return "Intel 80486";
	case EM_860:		return "Intel 80860";
	case EM_MIPS:		return "MIPS R3000";
	case EM_S370:		return "IBM System/370";
	case EM_MIPS_RS3_LE:	return "MIPS R4000 big-endian";
	case EM_OLD_SPARCV9:	return "Sparc v9 (old)";
	case EM_PARISC:		return "HPPA";
	case EM_PPC_OLD:		return "Power PC (old)";
	case EM_SPARC32PLUS:	return "Sparc v8+" ;
	case EM_960:		return "Intel 90860";
	case EM_PPC:		return "PowerPC";
	case EM_PPC64:		return "PowerPC64";
	case EM_V800:		return "NEC V800";
	case EM_FR20:		return "Fujitsu FR20";
	case EM_RH32:		return "TRW RH32";
	case EM_MCORE:		return "MCORE";
	case EM_ARM:		return "ARM";
	case EM_OLD_ALPHA:		return "Digital Alpha (old)";
	case EM_SH:			return "Renesas / SuperH SH";
	case EM_SPARCV9:		return "Sparc v9";
	case EM_TRICORE:		return "Siemens Tricore";
	case EM_ARC:		return "ARC";
	case EM_H8_300:		return "Renesas H8/300";
	case EM_H8_300H:		return "Renesas H8/300H";
	case EM_H8S:		return "Renesas H8S";
	case EM_H8_500:		return "Renesas H8/500";
	case EM_IA_64:		return "Intel IA-64";
	case EM_MIPS_X:		return "Stanford MIPS-X";
	case EM_COLDFIRE:		return "Motorola Coldfire";
	case EM_68HC12:		return "Motorola M68HC12";
	case EM_ALPHA:		return "Alpha";
	case EM_CYGNUS_D10V:
	case EM_D10V:		return "d10v";
	case EM_CYGNUS_D30V:
	case EM_D30V:		return "d30v";
	case EM_CYGNUS_M32R:
	case EM_M32R:		return "Renesas M32R (formerly Mitsubishi M32r)";
	case EM_CYGNUS_V850:
	case EM_V850:		return "NEC v850";
	case EM_CYGNUS_MN10300:
	case EM_MN10300:		return "mn10300";
	case EM_CYGNUS_MN10200:
	case EM_MN10200:		return "mn10200";
	case EM_CYGNUS_FR30:
	case EM_FR30:		return "Fujitsu FR30";
	case EM_CYGNUS_FRV:		return "Fujitsu FR-V";
	case EM_PJ_OLD:
	case EM_PJ:			return "picoJava";
	case EM_MMA:		return "Fujitsu Multimedia Accelerator";
	case EM_PCP:		return "Siemens PCP";
	case EM_NCPU:		return "Sony nCPU embedded RISC processor";
	case EM_NDR1:		return "Denso NDR1 microprocesspr";
	case EM_STARCORE:		return "Motorola Star*Core processor";
	case EM_ME16:		return "Toyota ME16 processor";
	case EM_ST100:		return "STMicroelectronics ST100 processor";
	case EM_TINYJ:		return "Advanced Logic Corp. TinyJ embedded processor";
	case EM_FX66:		return "Siemens FX66 microcontroller";
	case EM_ST9PLUS:		return "STMicroelectronics ST9+ 8/16 bit microcontroller";
	case EM_ST7:		return "STMicroelectronics ST7 8-bit microcontroller";
	case EM_68HC16:		return "Motorola MC68HC16 Microcontroller";
	case EM_68HC11:		return "Motorola MC68HC11 Microcontroller";
	case EM_68HC08:		return "Motorola MC68HC08 Microcontroller";
	case EM_68HC05:		return "Motorola MC68HC05 Microcontroller";
	case EM_SVX:		return "Silicon Graphics SVx";
	case EM_ST19:		return "STMicroelectronics ST19 8-bit microcontroller";
	case EM_VAX:		return "Digital VAX";
	case EM_AVR_OLD:
	case EM_AVR:		return "Atmel AVR 8-bit microcontroller";
	case EM_CRIS:		return "Axis Communications 32-bit embedded processor";
	case EM_JAVELIN:		return "Infineon Technologies 32-bit embedded cpu";
	case EM_FIREPATH:		return "Element 14 64-bit DSP processor";
	case EM_ZSP:		return "LSI Logic's 16-bit DSP processor";
	case EM_MMIX:		return "Donald Knuth's educational 64-bit processor";
	case EM_HUANY:		return "Harvard Universitys's machine-independent object format";
	case EM_PRISM:		return "Vitesse Prism";
	case EM_X86_64:		return "Advanced Micro Devices X86-64";
	case EM_S390_OLD:
	case EM_S390:		return "IBM S/390";
	case EM_SCORE:		return "SUNPLUS S+Core";
	case EM_XSTORMY16:		return "Sanyo Xstormy16 CPU core";
	case EM_OPENRISC:
	case EM_OR32:		return "OpenRISC";
	case EM_CRX:		return "National Semiconductor CRX microprocessor";
	case EM_DLX:		return "OpenDLX";
	case EM_IP2K_OLD:
	case EM_IP2K:		return "Ubicom IP2xxx 8-bit microcontrollers";
	case EM_IQ2000:       	return "Vitesse IQ2000";
	case EM_XTENSA_OLD:
	case EM_XTENSA:		return "Tensilica Xtensa Processor";
	case EM_M32C:	        return "Renesas M32c";
	case EM_MT:                 return "Morpho Techologies MT processor";
	case EM_BLACKFIN:		return "Analog Devices Blackfin";
	case EM_NIOS32:		return "Altera Nios";
	case EM_ALTERA_NIOS2:	return "Altera Nios II";
	case EM_XC16X:		return "Infineon Technologies xc16x";
	case EM_CYGNUS_MEP:         return "Toshiba MeP Media Engine";
	case EM_CR16:		return "National Semiconductor's CR16";
	default:
				snprintf (buff, sizeof (buff), ("<unknown>: 0x%x"), e_machine);
				return buff;
	}
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
static const char *kdbg_elf_get_symbol_binding (unsigned int binding)
{
	static char buff[32];

	switch (binding) {
	case KDBG_ELF_STB_LOCAL:	return "LOCAL";
	case KDBG_ELF_STB_GLOBAL:	return "GLOBAL";
	case KDBG_ELF_STB_WEAK:	return "WEAK";
	default:
				if (binding >= KDBG_ELF_STB_LOPROC && binding <= KDBG_ELF_STB_HIPROC)
					snprintf (buff, sizeof (buff), ("<processor specific>: %d"),
							binding);
				else if (binding >= KDBG_ELF_STB_LOOS && binding <= KDBG_ELF_STB_HIOS)
					snprintf (buff, sizeof (buff), ("<OS specific>: %d"), binding);
				else
					snprintf (buff, sizeof (buff), ("<unknown>: %d"), binding);
				return buff;
	}
}

static const char *kdbg_elf_get_symbol_type (unsigned int type)
{
	static char buff[32];

	switch (type) {
	case KDBG_ELF_STT_NOTYPE:	return "NOTYPE";
	case KDBG_ELF_STT_OBJECT:	return "OBJECT";
	case KDBG_ELF_STT_FUNC:	return "FUNC";
	case KDBG_ELF_STT_SECTION:	return "SECTION";
	case KDBG_ELF_STT_FILE:	return "FILE";
	case KDBG_ELF_STT_COMMON:	return "COMMON";
	case KDBG_ELF_STT_TLS:	return "TLS";
				/*    case KDBG_ELF_STT_RELC:      return "RELC"; */
				/*    case KDBG_ELF_STT_SRELC:     return "SRELC"; */
	default:
				snprintf (buff, sizeof (buff), ("<unknown>: %d"), type);
				return buff;
	}
}

static int kdbg_elf_print_vma (kdbg_elf_pvma vma, kdbg_elf_print_mode mode)
{
	switch (mode) {
	case FULL_HEX:
		return PRINT_KD ("0x%8.8lx", (unsigned long) vma);

	case LONG_HEX:
		return PRINT_KD ("%8.8lx", (unsigned long) vma);

	case DEC_5:
		if (vma <= 99999)
			return PRINT_KD ("%5ld", (long) vma);
		/* Drop through.  */

	case PREFIX_HEX:
		return PRINT_KD ("0x%lx", (unsigned long) vma);

	case HEX:
		return PRINT_KD ("%lx", (unsigned long) vma);

	case DEC:
		return PRINT_KD ("%ld", (unsigned long) vma);

	case UNSIGNED:
		return PRINT_KD ("%lu", (unsigned long) vma);

	default:
		return PRINT_KD ("%ld", (unsigned long) vma);

	}

	return 0;
}
#endif /* #if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */

/*
 *   Add the Symbol info in Symbol database
 */
static int kdbg_elf_sym_add(Elf32_Sym *sym_info, kdbg_elf_kernel_symbol_item  *item_ptr)
{
	BUG_ON(!sym_info || !item_ptr);
	item_ptr->st_info = sym_info->st_info;
	item_ptr->st_name = sym_info->st_name;
	item_ptr->st_value = sym_info->st_value;
	item_ptr->st_size = sym_info->st_size;

	return 0;
}
static int kdbg_elf_chk_status(kdbg_elf_usb_elf_list_item *plist, char *filename)
{
	int idx;
	Elf32_Shdr	*lShdr;
	int bytesread;
	mm_segment_t oldfs = get_fs();
	int err = 0;
	Elf32_Shdr	*pShdr = NULL;		/* Section Header */
	Elf32_Ehdr	pEhdr;				/* ELF Header */
	int elf_status = KDBG_ELF_MATCH;
	struct file *elf_filp; /* File pointer to access file for ELF parsing*/

	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section
	 */
	uint32_t symtab_offset = 0;
	/*Total no of enteries in Section header*/
	uint32_t symtab_ent_no = 0;
	/*
	 * This member holds a section header table index link,
	 * whose interpretation depends on the section type.
	 */
	int symtab_str_link = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section*
	 */
	uint32_t dyn_symtab_offset = 0;
	/*Total no of enteries in dynamic symbol table*/
	uint32_t dyn_symtab_ent_no = 0;
	/*
	 * This member's value gives the size of the string section
	 */
	uint32_t dyn_str_size = 0;
	uint32_t sym_str_size = 0;
	/*
	 * This member holds a section header table index link,
	 * whose interpretation depends on the section type.
	 */
	int dyn_symtab_str_link = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the dynamic section string
	 */
	uint32_t dyn_str_offset = 0;
	uint32_t sym_str_offset = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section header
	 */
	uint32_t strtab_offset = 0;

	sym_printk("enter\n");

	PRINT_KD("%s file loading....\n", filename);

	/* File Open */
	elf_filp = filp_open(filename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(elf_filp) || (elf_filp == NULL)) {
		PRINT_KD("%s: error opening file\n", filename);
		return 1;
	}

	if (elf_filp->f_op->read == NULL) {
		PRINT_KD("%s: Read not allowed\n", filename);
		err = -EIO;
		goto ERR_2;
	}

	elf_filp->f_pos = 0;

	/*
	 * Kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	set_fs(KERNEL_DS);

	/*  ELF Header: the ELF header contains their actual sizes.*/
	/* e_ident: The initial bytes mark the file as an object file and provide machine-independent
	 * data with which to decode and interpret the file's contents. Complete descriptions appear
	 * below, in "ELF Identification."
	 */

	/*     EI_MAG0 to EI_MAG3
	 *A file's first 4 bytes hold a "magic number," identifying the file as an ELF object file.
	 *Name 			Value				Position
	 *ELFMAG0 			0x7f 				e_ident[EI_MAG0]
	 *ELFMAG1 			'E' 					e_ident[EI_MAG1]
	 *ELFMAG2 			'L' 					e_ident[EI_MAG2]
	 *ELFMAG3 			'F' 					e_ident[EI_MAG3]
	 */
	bytesread = elf_filp->f_op->read(elf_filp, (char *)&pEhdr, sizeof(Elf32_Ehdr),
					&elf_filp->f_pos);
	if (bytesread  < sizeof(Elf32_Ehdr)) {
		sym_errk("pEhdr %d read bytes out of required %lu\n", bytesread
				, sizeof(Elf32_Ehdr));
		err = -EIO;
		goto ERR;
	}

	/* First of all, some simple consistency checks */
	if (memcmp(pEhdr.e_ident, ELFMAG, SELFMAG) != 0) {
		PRINT_KD("Not an ELF File----!!!!\n");
		err = 1;
		goto ERR_2;
	}

	/* e_shnum:  This member holds the number of entries in the section header table.
	 * Thus the product of e_shentsize and e_shnum gives the section header table's size
	 * in bytes. If a file has no section header table, e_shnum holds the value zero.
	 */

	/* Section Header */
	pShdr = (Elf32_Shdr *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
					(sizeof(Elf32_Shdr)*pEhdr.e_shnum), GFP_KERNEL);
	if (!pShdr) {
		sym_errk("SectionHeader: Memory demand = %lu---out of memory\n",
				(sizeof(Elf32_Shdr)*pEhdr.e_shnum));
		err = -ENOMEM;
		goto ERR;
	}
	memset(pShdr, 0, sizeof(Elf32_Shdr)*pEhdr.e_shnum);

	elf_filp->f_pos = (loff_t)pEhdr.e_shoff;
	if (elf_filp->f_pos <= 0) {
		err = -EIO;
		goto ERR;
	}

	bytesread = elf_filp->f_op->read(elf_filp, (char *)pShdr,
					sizeof(Elf32_Shdr)*pEhdr.e_shnum, &elf_filp->f_pos);
	if (bytesread < sizeof(Elf32_Shdr)*pEhdr.e_shnum) {
		sym_errk("SectionHeader: %d read bytes out of required %lu\n", bytesread,
				sizeof(Elf32_Shdr)*pEhdr.e_shnum);
		err = -EIO;
		goto ERR;
	}

	lShdr = pShdr;

	for (idx = 0; idx < pEhdr.e_shnum; idx++, lShdr++) {
		if (idx == pEhdr.e_shstrndx) {
			strtab_offset = lShdr->sh_offset;
		} else if (lShdr->sh_type == SHT_DYNSYM) {
			dyn_symtab_offset = lShdr->sh_offset;
			BUG_ON(!lShdr->sh_entsize);
			if (!lShdr->sh_entsize)
				goto ERR;
			dyn_symtab_ent_no = lShdr->sh_size / lShdr->sh_entsize;
			dyn_symtab_str_link  = lShdr->sh_link;
		} else if ((idx == dyn_symtab_str_link) && dyn_symtab_str_link) {
			dyn_str_offset = lShdr->sh_offset;
			dyn_str_size = lShdr->sh_size;
		} else if (lShdr->sh_type == SHT_SYMTAB) {
			symtab_offset = lShdr->sh_offset;
			symtab_ent_no = lShdr->sh_size / lShdr->sh_entsize;
			symtab_str_link  = lShdr->sh_link;
		} else if ((idx == symtab_str_link) && symtab_str_link) {
			sym_str_offset  = lShdr->sh_offset;
			sym_str_size = lShdr->sh_size;
		}
	}

	if (sym_str_size) {
		if (plist->act_sym_str_size == sym_str_size) {
			sym_printk("---sym_str_size----MATCH----\n");
			elf_status = KDBG_ELF_MATCH;/* Match */
		} else{
			sym_printk("--sym_str_size----MISMATCH----\n");
			elf_status = KDBG_ELF_MISMATCH;/* Mismatch */
		}
	} else if (dyn_str_size) {
		if (plist->act_sym_str_size == dyn_str_size) {
			sym_printk("--dyn_str_size-----MATCH----\n");
			elf_status = KDBG_ELF_MATCH;/* Match */
		} else{
			sym_printk("--dyn_str_size-----MISMATCH----\n");
			elf_status = KDBG_ELF_MISMATCH;/* Mismatch */
		}
	} else{
		elf_status = KDBG_ELF_NO_SYMBOL; /* no symbol: binary is stripped */
		sym_printk("----NO SYMBOL----\n");
	}

	set_fs(oldfs);

	if (pShdr) {
		KDBG_MEM_DBG_KFREE(pShdr);
		pShdr = NULL;
	}
	filp_close(elf_filp, NULL);
	return elf_status;

ERR:
	sym_errk("%s: Error in  file loading \n", filename);
ERR_2:
	set_fs(oldfs);

	if (pShdr) {
		KDBG_MEM_DBG_KFREE(pShdr);
		pShdr = NULL;
	}

	filp_close(elf_filp, NULL);
	return -1;
}

/*  Parse the ELF file */
static int kdbg_elf_sym_read_elf(
		char *filename, kdbg_elf_usb_elf_list_item *plist, int load_from_usb)
{
	int idx;
	Elf32_Shdr	*lShdr;
	Elf32_Sym isyms;
	Elf32_Sym dyn_syms;
	int bytesread;
	mm_segment_t oldfs = get_fs();
	int symbol_count = 0;
	Elf32_Phdr	*temp_pPhdr = NULL;
	int err = 0;
	Elf32_Phdr	*pPhdr = NULL;		/* Program Header */
	Elf32_Shdr	*pShdr = NULL;		/* Section Header */
	Elf32_Ehdr	pEhdr;				/* ELF Header */

	struct file *elf_filp; /* File pointer to access file for ELF parsing*/

	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section
	 */
	uint32_t symtab_offset = 0;
	/*Total no of enteries in Section header*/
	uint32_t symtab_ent_no = 0;
	/*
	 * This member holds a section header table index link,
	 * whose interpretation depends on the section type.
	 */
	int symtab_str_link = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section*
	 */
	uint32_t dyn_symtab_offset = 0;
	/*Total no of enteries in dynamic symbol table*/
	uint32_t dyn_symtab_ent_no = 0;
	/*
	 * This member's value gives the size of the string section
	 */
	uint32_t dyn_str_size = 0;
	uint32_t sym_str_size = 0;
	/*
	 * This member holds a section header table index link,
	 * whose interpretation depends on the section type.
	 */
	int dyn_symtab_str_link = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the dynamic section string
	 */
	uint32_t dyn_str_offset = 0;
	uint32_t sym_str_offset = 0;
	/*
	 * This member's value gives the byte offset from the beginning of the
	 * file to the firstbyte in the section header
	 */
	uint32_t strtab_offset = 0;
#ifdef CONFIG_DWARF_MODULE
	/* for string buufer used for comparing debug_lin section */
	uint32_t strtab_size = 0;
	char *strtab_buf = NULL;
#endif /* CONFIG_DWARF_MODULE */
	/* Buffer for storing the symbol entries */
	kdbg_elf_kernel_symbol_item *kdbg_elf_sym_buff = NULL;

	kdbg_elf_kernel_symbol_item  *item_ptr = NULL;
	uint32_t item_count = 0;

	sym_printk("enter\n");

	PRINT_KD ("%s file loading....\n", filename);

	if (!plist) {
		PRINT_KD("plist not available\n");
		return 1;
	}

	/* File Open */
	elf_filp = filp_open(filename, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(elf_filp) || (elf_filp == NULL)) {
		PRINT_KD("%s: error opening file\n", filename);
		return 1;
	}

	if (elf_filp->f_op->read == NULL) {
		PRINT_KD("%s: Read not allowed\n", filename);
		err = -EIO;
		goto ERR_2;
	}

	strncpy(plist->elf_name_actual_path, filename, KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN);
	plist->elf_name_actual_path[KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN-1] = '\0';


	elf_filp->f_pos = 0;

	/*
	 * Kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	set_fs(KERNEL_DS);

	/*  ELF Header: the ELF header contains their actual sizes.*/
	/* e_ident: The initial bytes mark the file as an object file and provide machine-independent
	 * data with which to decode and interpret the file's contents. Complete descriptions appear
	 * below, in "ELF Identification."
	 */

	/*     EI_MAG0 to EI_MAG3
	 *A file's first 4 bytes hold a "magic number," identifying the file as an ELF object file.
	 *Name 			Value				Position
	 *ELFMAG0 			0x7f 				e_ident[EI_MAG0]
	 *ELFMAG1 			'E' 					e_ident[EI_MAG1]
	 *ELFMAG2 			'L' 					e_ident[EI_MAG2]
	 *ELFMAG3 			'F' 					e_ident[EI_MAG3]
	 */
	bytesread = elf_filp->f_op->read(elf_filp, (char *)&pEhdr, sizeof(Elf32_Ehdr),
					&elf_filp->f_pos);
	if (bytesread < sizeof(Elf32_Ehdr)) {
		sym_errk("pEhdr %d read bytes out of required %lu\n", bytesread
				, sizeof(Elf32_Ehdr));
		err = -EIO;
		goto ERR;
	}

	/* e_type: This member identifies the object file type.
	 * __ _N__a_m__e_ _______V_a_l_u_e_ ________M__e_a_n_in_g_____
	 *	     	ET_NONE   			0 				No file type
	 * 		ET_REL 				1 				Relocatable file
	 * 		ET_EXEC 			2 				Executable file
	 * 		ET_DYN 				3 				Shared object file
	 * 		ET_CORE 			4 				Core file
	 * 		ET_LOPROC 			0xff00 			Processor-specific
	 * 		ET_HIPROC 			0xffff 			Processor-specific
	 * Although the core file contents are unspecified, type ET_CORE is reserved to mark the
	 * file. Values from ET_LOPROC through ET_HIPROC (inclusive) are reserved for
	 * processor-specific semantics. Other values are reserved and will be assigned to new
	 * object file types as necessary.
	 */
	plist->file_type = pEhdr.e_type;

	/* First of all, some simple consistency checks */
	if (memcmp(pEhdr.e_ident, ELFMAG, SELFMAG) != 0) {
		PRINT_KD("Not an ELF File----!!!!\n");
		err = 1;
		goto ERR_2;
	}

	if (kdbg_elf_chk_machine_type(pEhdr)) {
		PRINT_KD("Machine: %s is not Valid Machine\n ", kdbg_elf_get_machine_name (pEhdr.e_machine));
		err = 1;
		goto ERR_2;
	} else {
		sym_printk("Machine: %s is Valid Machine\n ", kdbg_elf_get_machine_name (pEhdr.e_machine));
	}

	if (pEhdr.e_type == ET_EXEC || pEhdr.e_type == ET_DYN) {
		sym_printk("Type: %s is Valid ELF File\n ", kdbg_elf_get_file_type (pEhdr.e_type));
	} else{
		PRINT_KD("Type: %s is not Valid ELF File \n ", kdbg_elf_get_file_type (pEhdr.e_type));
		err = 1;
		goto ERR_2;
	}

	/* e_phnum: This member holds the number of entries in the program header table.
	 * Thus the product of e_phentsize and e_phnum gives the table's size in bytes.
	 * If a file has no program header table, e_phnum holds the value zero.
	 */

	/* Program Header: An executable or shared object file's program header table is an array
	 * of structures, each describing a segment or other information the system needs to
	 * prepare the program for execution.
	 */
	pPhdr = (Elf32_Phdr *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
					sizeof(Elf32_Phdr)*pEhdr.e_phnum, GFP_KERNEL);
	if (!pPhdr) {
		sym_errk("pEhdr.e_phnum out of memory\n");
		err = -ENOMEM;
		goto ERR;
	}
	memset(pPhdr, 0, sizeof(Elf32_Phdr)*pEhdr.e_phnum);

	elf_filp->f_pos = (loff_t)sizeof(Elf32_Ehdr);

	bytesread = elf_filp->f_op->read(elf_filp, (char *)pPhdr,
					sizeof(Elf32_Phdr)*pEhdr.e_phnum, &elf_filp->f_pos);
	if (bytesread < sizeof(Elf32_Phdr)*pEhdr.e_phnum) {
		sym_errk("pEhdr.e_phnum %d read bytes out of required %lu\n",
				bytesread, sizeof(Elf32_Phdr)*pEhdr.e_phnum);
		err = -EIO;
		goto ERR;
	}

	/* Extracting Virtual address helps to find the symbol address In case of prelinked library */
	temp_pPhdr = pPhdr;
	for (idx = 0; idx < pEhdr.e_phnum; idx++, temp_pPhdr++) {
		if ((temp_pPhdr->p_type == PT_LOAD) &&
				((temp_pPhdr->p_flags & PF_X) == PF_X)) {
			sym_printk("Type: %s\n", kdbg_elf_get_file_type (pEhdr.e_type));
			sym_printk("Virtual Address: 0x%08lx\n",
					(unsigned long) temp_pPhdr->p_vaddr);
			plist->virtual_addr = temp_pPhdr->p_vaddr;
			break;
		}
	}
	if (kdbg_elf_elf_load_option == KDBG_ELF_LOAD_VIRT_ADDR) {
		PRINT_KD ("deliberately skipping symbol loading for ELF File : %s\n", filename);
		goto DONE;
	}

	/* e_shnum:  This member holds the number of entries in the section header table.
	 * Thus the product of e_shentsize and e_shnum gives the section header table's size
	 * in bytes. If a file has no section header table, e_shnum holds the value zero.
	 */

	/* Section Header */
	pShdr = (Elf32_Shdr *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
					(sizeof(Elf32_Shdr)*pEhdr.e_shnum), GFP_KERNEL);
	if (!pShdr) {
		sym_errk("SectionHeader: Memory demand = %lu---out of memory\n",
				(sizeof(Elf32_Shdr)*pEhdr.e_shnum));
		err = -ENOMEM;
		goto ERR;
	}
	memset(pShdr, 0, sizeof(Elf32_Shdr)*pEhdr.e_shnum);

	elf_filp->f_pos = (loff_t)pEhdr.e_shoff;
	if (elf_filp->f_pos  <= 0) {
		err = -EIO;
		goto ERR;
	}

	bytesread = elf_filp->f_op->read(elf_filp, (char *)pShdr,
					sizeof(Elf32_Shdr)*pEhdr.e_shnum, &elf_filp->f_pos);
	if (bytesread < sizeof(Elf32_Shdr)*pEhdr.e_shnum) {
		sym_errk("SectionHeader: %d read bytes out of required %lu\n", bytesread,
				sizeof(Elf32_Shdr)*pEhdr.e_shnum);
		err = -EIO;
		goto ERR;
	}

	/* sh_offset: This member's value gives the byte offset from the beginning of the file to
	 * the first byte in the section. One section type, SHT_NOBITS described below, occupies no
	 * space in the file, and its sh_offset member locates the conceptual placement in the file.
	 */

	/* sh_entsize: Some sections hold a table of fixed-size entries, such as a symbol table.
	 * For such a section,this member gives the size in bytes of each entry.
	 * The member contains 0 if the section does not hold a table of fixed-size entries.
	 */

	/* sh_size: This member gives the section's size in bytes. Unless the section type is
	 * SHT_NOBITS, the section occupies sh_size bytes in the file. A section of type
	 * SHT_NOBITS may have a non-zero size, but it occupies no space in the file.
	 */

	/* sh_link: This member holds a section header table index link, whose interpretation
	 * depends on the section type. A table below describes the values.
	 */

	lShdr = pShdr;
	for (idx = 0; idx < pEhdr.e_shnum; idx++, lShdr++) {
#ifdef CONFIG_DWARF_MODULE
		sym_printk("%s %d\n", KDBG_ELF_SECTION_NAME(lShdr, strtab_buf, strtab_size), lShdr->sh_type);
#endif /* CONFIG_DWARF_MODULE */
		if (idx == pEhdr.e_shstrndx) {
			strtab_offset = lShdr->sh_offset;
#ifdef CONFIG_DWARF_MODULE
			strtab_size =  lShdr->sh_size;

			strtab_buf = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, strtab_size);
			if (strtab_buf == NULL) {
				sym_errk("[kdbg_elf_head_item] out of memory\n");
				err = -ENOMEM;
				goto ERR;
			}
			elf_filp->f_pos = (loff_t)strtab_offset;
			if (elf_filp->f_pos <= 0) {
				err = -EIO;
				goto ERR;
			}

			bytesread = elf_filp->f_op->read(elf_filp, (char *)strtab_buf,
							strtab_size, &elf_filp->f_pos);
			if (bytesread < strtab_size) {
				sym_errk("byteread is not accurate %d %u\n", bytesread,
						strtab_size);
				err = -EIO;
				goto ERR;
			}
#endif
		} else if (lShdr->sh_type == SHT_DYNSYM) {
			dyn_symtab_offset = lShdr->sh_offset;
			BUG_ON(!lShdr->sh_entsize);
			if (!lShdr->sh_entsize)
				goto ERR;
			dyn_symtab_ent_no = lShdr->sh_size / lShdr->sh_entsize;
			dyn_symtab_str_link  = lShdr->sh_link;
		} else if ((idx == dyn_symtab_str_link) && dyn_symtab_str_link) {
			dyn_str_offset = lShdr->sh_offset;
			dyn_str_size = lShdr->sh_size;
		} else if (lShdr->sh_type == SHT_SYMTAB) {
			symtab_offset = lShdr->sh_offset;
			symtab_ent_no = lShdr->sh_size / lShdr->sh_entsize;
			symtab_str_link  = lShdr->sh_link;
		} else if ((idx == symtab_str_link) && symtab_str_link) {
			sym_str_offset  = lShdr->sh_offset;
			sym_str_size = lShdr->sh_size;
		}
	}
#ifdef CONFIG_DWARF_MODULE

	lShdr = pShdr;
	for (idx = 0; idx < pEhdr.e_shnum; idx++, lShdr++) {
		sym_printk(" %s %d\n", KDBG_ELF_SECTION_NAME(lShdr, strtab_buf, strtab_size), lShdr->sh_type);
		if (/*lShdr->sh_type == SHT_PROGBITS &&*/   /* NAMIT for filename and line debug */
				!strcmp(KDBG_ELF_SECTION_NAME(lShdr, strtab_buf, strtab_size), ".debug_line")) {
			plist->dbg_line_buf_offset = lShdr->sh_offset;
			plist->dbg_line_buf_size = lShdr->sh_size;
		}
	}
#endif /* CONFIG_DWARF_MODULE */

	sym_printk("sym_str_size = %d :: dyn_str_size = %d\n", sym_str_size, dyn_str_size);

	if (sym_str_size) {
		sym_printk("sym_str_size = %d :: sym_str_offset = %d\n", sym_str_size, sym_str_offset);
		symbol_count = 0;

		if ((kdbg_elf_elf_load_option == KDBG_ELF_LOAD_DEFAULT && load_from_usb) ||
				kdbg_elf_elf_load_option == KDBG_ELF_LOAD_FULL_MEM) {
			plist->sym_buff = (char *)KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE,
							(sizeof(char)*sym_str_size));
			if (!plist->sym_buff) {
				sym_errk("Symstrbuf out of memory\n");
				err = -ENOMEM;
				goto ERR;
			}
			memset(plist->sym_buff, 0, sizeof(char)*sym_str_size);

			/*For Symbol String  Table */
			elf_filp->f_pos = (loff_t)sym_str_offset;
			if (elf_filp->f_pos <= 0) {
				err = -EIO;
				goto ERR;
			}

			bytesread = elf_filp->f_op->read(elf_filp, plist->sym_buff,
							sizeof(char)*sym_str_size, &elf_filp->f_pos);
			if (bytesread < sizeof(char)*sym_str_size) {
				sym_errk("SymStr: %d read bytes out of required %lu\n", bytesread,
						sizeof(char)*sym_str_size);
				PRINT_KD ("symstr is corrupted. Try to load dynsym data\n");
				goto ERR;
			}
		}

		/* For Symbol Table loading */
		/* 1. Calculate the number of symbol entries that are functions,
		   to be used in 2nd step for allocating the dynamic memory
		   for the symbol entries */

		/* Count Loop (Estimate the required memory) */
		elf_filp->f_pos = (loff_t)symtab_offset;
		if (elf_filp->f_pos <= 0) {
			err = -EIO;
			goto ERR;
		}

		for (idx = 0; idx < symtab_ent_no; idx++) {
			bytesread = elf_filp->f_op->read(elf_filp, (char *)&isyms, sizeof (Elf32_Sym),
							&elf_filp->f_pos);
			if (bytesread < sizeof (Elf32_Sym)) {
				sym_errk("SymTab: %d read bytes out of required %lu\n", bytesread
						, sizeof (Elf32_Sym));
				err = -EIO;
				goto ERR;
			}

			if ((ELF_ST_TYPE (isyms.st_info) == KDBG_ELF_STT_FUNC)
					&& (isyms.st_shndx != SHN_UNDEF)
					&& (isyms.st_shndx < SHN_LORESERVE)) {
				symbol_count++;
			}
		}
		sym_printk("number of functions = %d\n", symbol_count);

		if (!symbol_count) {
			PRINT_KD("No Functions in symbol table\n");
			goto DONE;
		}

		/* 2. Allocate memory based on symbol count */
		kdbg_elf_sym_buff = (kdbg_elf_kernel_symbol_item *)KDBG_MEM_DBG_VMALLOC
					(KDBG_MEM_ELF_MODULE, symbol_count *
					 sizeof(kdbg_elf_kernel_symbol_item));
		if (!kdbg_elf_sym_buff) {
			sym_errk("[kdbg_elf_sym_buff] out of memory\n");
			err = -ENOMEM;
			plist->kdbg_elf_sym_head = NULL;
			goto ERR;
		}

		/* Reset the offset again.*/
		elf_filp->f_pos = (loff_t)symtab_offset;
		/* it is already checked above, so it should not happen */
		BUG_ON(elf_filp->f_pos <= 0);

		/* Now load the symbols into the allocated array */
		item_ptr = kdbg_elf_sym_buff;
		for (idx = 0; idx < symtab_ent_no; idx++) {
			bytesread = elf_filp->f_op->read(elf_filp, (char *)&isyms, sizeof (Elf32_Sym),
							&elf_filp->f_pos);
			if (bytesread < sizeof (Elf32_Sym)) {
				sym_errk("SymTab: %d read bytes out of required %lu\n", bytesread
						, sizeof (Elf32_Sym));
				err = -EIO;
				goto ERR;
			}

			if ((ELF_ST_TYPE (isyms.st_info) == KDBG_ELF_STT_FUNC)
					&& (isyms.st_shndx != SHN_UNDEF)
					&& (isyms.st_shndx < SHN_LORESERVE)) {

				kdbg_elf_sym_add(&isyms, item_ptr);
				item_ptr++;
				item_count++;

				/* Allocated memory can not contains item more that symbol
				 * count */
				if (item_count >= symbol_count)
					break;
			}
		}

		plist->type_sym_dym = 0;
		plist->sym_str_size = sym_str_size;
		plist->sym_str_offset = sym_str_offset;
		dyn_str_size = 0;
		dyn_str_offset = 0;
	} else if (dyn_str_size) {
		sym_printk("dyn_str_size = %d :: dyn_str_offset = %d\n", dyn_str_size, dyn_str_offset);
		symbol_count = 0;
		if ((kdbg_elf_elf_load_option == KDBG_ELF_LOAD_DEFAULT && strstr(plist->elf_name_actual_path, "usb")) ||
				kdbg_elf_elf_load_option == KDBG_ELF_LOAD_FULL_MEM) {
			plist->sym_buff = (char *)KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE,
							(sizeof(char)  * dyn_str_size));
			if (!plist->sym_buff) {
				sym_printk("size = %d : dyn_sym_buff --out of memory\n",
						(sizeof(char)*dyn_str_size));
				err = -ENOMEM;
				goto ERR;
			}
			memset(plist->sym_buff, 0, sizeof(char)*dyn_str_size);

			/*For dynamic string table*/
			elf_filp->f_pos = (loff_t)dyn_str_offset;
			if (elf_filp->f_pos <= 0) {
				err = -EIO;
				goto ERR;
			}

			bytesread = elf_filp->f_op->read(elf_filp, plist->sym_buff,
							sizeof(char)*dyn_str_size, &elf_filp->f_pos);
			if (bytesread < sizeof(char)*dyn_str_size) {
				sym_errk("SectionHeader: %d read bytes out of required %lu\n",
						bytesread,
						sizeof(char)*dyn_str_size);
				err = -EIO;
				goto ERR;
			}
		}
		/* For Dynamic Symbol Table */
		/* 1. Calculate the number of symbol entries that are functions,
		   to be used in 2nd step for allocating the dynamic memory
		   for the symbol entries */

		/* Count Loop (Estimate the required memory) */
		elf_filp->f_pos = (loff_t)dyn_symtab_offset;
		if (elf_filp->f_pos <= 0) {
			err = -EIO;
			goto ERR;
		}

		for (idx = 0; idx < dyn_symtab_ent_no; idx++) {
			bytesread = elf_filp->f_op->read(elf_filp, (char *)&dyn_syms,
							sizeof (Elf32_Sym), &elf_filp->f_pos);
			if (bytesread < sizeof (Elf32_Sym)) {
				sym_errk("DynSymTab: %d read bytes out of required %lu\n",
						bytesread, sizeof (Elf32_Sym));

				err = -EIO;
				goto ERR;
			}
			if ((ELF_ST_TYPE (dyn_syms.st_info) == KDBG_ELF_STT_FUNC)
					&& (dyn_syms.st_shndx != SHN_UNDEF)
					&& (dyn_syms.st_shndx < SHN_LORESERVE)) {
				symbol_count++;
			}
		}

		sym_printk("dyn: number of functions = %d\n", symbol_count);

		if (!symbol_count) {
			PRINT_KD("No Functions in dyn-symbol table\n");
			goto DONE;
		}

		/* 2. Allocate memory based on symbol count */
		kdbg_elf_sym_buff = (kdbg_elf_kernel_symbol_item *) KDBG_MEM_DBG_VMALLOC
					(KDBG_MEM_ELF_MODULE, symbol_count *
					 sizeof(kdbg_elf_kernel_symbol_item));
		if (!kdbg_elf_sym_buff) {
			sym_errk("[kdbg_elf_sym_buff] out of memory\n");
			err = -ENOMEM;
			plist->kdbg_elf_sym_head = NULL;
			goto ERR;
		}

		/* Reset the offset again.*/
		elf_filp->f_pos = (loff_t)dyn_symtab_offset;
		/* it is already checked above, so it should not happen */
		BUG_ON(elf_filp->f_pos <= 0);

		/* 3. Load the symbols into the allocated array */
		item_ptr = kdbg_elf_sym_buff;
		for (idx = 0; (idx < dyn_symtab_ent_no) ; idx++) {
			bytesread = elf_filp->f_op->read(elf_filp, (char *)&dyn_syms,
							sizeof (Elf32_Sym), &elf_filp->f_pos);
			if (bytesread < sizeof (Elf32_Sym)) {
				sym_errk("DynSymTab: %d read bytes out of required %lu\n",
						bytesread, sizeof (Elf32_Sym));

				err = -EIO;
				goto ERR;
			}
			if ((ELF_ST_TYPE (dyn_syms.st_info) == KDBG_ELF_STT_FUNC)
					&& (dyn_syms.st_shndx != SHN_UNDEF)
					&& (dyn_syms.st_shndx < SHN_LORESERVE)) {

				kdbg_elf_sym_add(&dyn_syms, item_ptr);
				item_ptr++;
				item_count++;

				/* Allocated memory can not contains item more that symbol
				 * count */
				if (item_count >= symbol_count)
					break;
			}
		}

		plist->type_sym_dym = 1;
		plist->sym_str_size = dyn_str_size;
		plist->sym_str_offset = dyn_str_offset;
		sym_str_size = 0;
		sym_str_offset = 0;
	}

	if (!plist->sym_str_size)
		plist->elf_status = KDBG_ELF_NO_SYMBOL;

	plist->act_sym_str_size = plist->sym_str_size;
	plist->kdbg_elf_sym_head = kdbg_elf_sym_buff;
	plist->elf_symbol_count = item_count; /* actual items read */

DONE:

	sym_printk ("file loading is completed %s (%d functions)\n", filename, item_count);
#ifdef CONFIG_DWARF_MODULE
	if (strtab_buf) {
		KDBG_MEM_DBG_VFREE(strtab_buf);
		strtab_buf = NULL;
	}
#endif /* CONFIG_DWARF_MODULE */
	set_fs(oldfs);
	if (pPhdr) {
		KDBG_MEM_DBG_KFREE(pPhdr);
		pPhdr = NULL;
	}

	if (pShdr) {
		KDBG_MEM_DBG_KFREE(pShdr);
		pShdr = NULL;
	}
	filp_close(elf_filp, NULL);
	return 0;

ERR:
	sym_errk("%s: Error in  file loading \n", filename);
ERR_2:
	set_fs(oldfs);

#ifdef CONFIG_DWARF_MODULE
	if (strtab_buf) {
		KDBG_MEM_DBG_VFREE(strtab_buf);
		strtab_buf = NULL;
	}
#endif /* CONFIG_DWARF_MODULE */

	if (pPhdr) {
		KDBG_MEM_DBG_KFREE(pPhdr);
		pPhdr = NULL;
	}

	if (pShdr) {
		KDBG_MEM_DBG_KFREE(pShdr);
		pShdr = NULL;
	}

	/* error occur while loading ELF, discard symbol table buffer */
	if (kdbg_elf_sym_buff) {
		KDBG_MEM_DBG_VFREE(kdbg_elf_sym_buff);
	}
	plist->elf_symbol_count = 0;

	plist->sym_str_size = 0;
	plist->sym_str_offset = 0;

	/* FULL_MEM case */
	if (plist->sym_buff) {
		KDBG_MEM_DBG_VFREE(plist->sym_buff);
		plist->sym_buff = NULL;
	}

	/* it is never set after initialization since error occured */
	BUG_ON(plist->kdbg_elf_sym_head);
	/* It is never set because of error, so it never enters below condition */
	if (plist->kdbg_elf_sym_head) {
		KDBG_MEM_DBG_VFREE(plist->kdbg_elf_sym_head);
		plist->kdbg_elf_sym_head = NULL;
	}

	filp_close(elf_filp, NULL);
	return err;
}

/*
 *   Find the Symbol name form ELF
 */
	static void kdbg_elf_find_func_name
(kdbg_elf_usb_elf_list_item *plist, unsigned int idx, char *elf_sym_buff, unsigned int sym_len)
{
	struct file *elf_filp = NULL; /* File pointer to access file for ELF parsing*/
	int bytesread = 0;
	unsigned int total_bytes = 0;
	uint32_t symstr_offset = 0;
	uint32_t symstr_size = 0;
	mm_segment_t oldfs = get_fs();

	sym_printk("enter\n");
	BUG_ON(sym_len > AOP_MAX_SYM_NAME_LENGTH);

	sym_printk("Index = %d\n", idx);
	sym_printk("[%s file loading....\n",  plist->elf_name_actual_path);

	/*
	 * Kernel segment override to datasegment and write it
	 * to the accounting file.
	 */
	set_fs(KERNEL_DS);

	/* File Open */
	elf_filp = filp_open(plist->elf_name_actual_path, O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(elf_filp) || (elf_filp == NULL)) {
		elf_filp = NULL;
		sym_errk("file open error\n\n");
		strncpy(elf_sym_buff, "<none>", sizeof("<none>"));
		sym_errk("<none>\n");
		bytesread = sizeof("<none>");
		goto DONE;
	}

	if (elf_filp->f_op->read == NULL) {
		sym_errk("read not allowed\n");
		strncpy(elf_sym_buff, "<none>", sizeof("<none>"));
		sym_errk("----<none>\n");
		bytesread = sizeof("<none>");
		goto DONE;
	}

	elf_filp->f_pos = 0;

	sym_printk("sym_str_size = %d \n", plist->sym_str_size);

	symstr_offset = plist->sym_str_offset;
	symstr_size =  plist->sym_str_size;

	elf_filp->f_pos = (loff_t)(symstr_offset + idx);
	if (elf_filp->f_pos <= 0) {
		strncpy(elf_sym_buff, "<no-name>", sizeof("<no-name>"));
		sym_errk("----<no-name>");
		bytesread = sizeof("<no-name>");
		goto DONE;
	}


	sym_printk("symstr_offset = %d :: symstr_size = %d\n",
			symstr_offset, symstr_size);
	if (idx >=  symstr_size) {
		strncpy(elf_sym_buff, "<corrupt>", sizeof("<corrupt>"));
		sym_errk("----<corrupt>\n");
		bytesread = sizeof("<corrupt>");
		goto DONE;
	}

	total_bytes = (symstr_size - idx);
	sym_printk("total_bytes = %d\n", total_bytes);

	if (total_bytes > sym_len) {
		total_bytes = sym_len;
	}

	bytesread = elf_filp->f_op->read
				(elf_filp, elf_sym_buff, total_bytes, &elf_filp->f_pos);
	if (bytesread < total_bytes) {
		sym_errk("Bytes Read: %d read bytes out of required %u\n", bytesread, sym_len);
		strncpy (elf_sym_buff, "<none>", sizeof("<none>"));
		bytesread = sizeof("<none>");
	}

DONE:
	BUG_ON(bytesread <= 0 || bytesread > sym_len);
	elf_sym_buff[bytesread-1] = '\0';
	sym_printk("\nSym_len = %d :: Bytes read = %d :: [%s]\n", sym_len, bytesread,
			elf_sym_buff);
	if (elf_filp) {
		filp_close(elf_filp, NULL);
	}
	set_fs(oldfs);
}

/*
 *   Find the Symbol info from Symbol database
 */
static int kdbg_elf_sym_find(unsigned int address,
		kdbg_elf_usb_elf_list_item *plist, char *pfunc_name,
		unsigned int symbol_len, unsigned int *start_addr)
{
	kdbg_elf_kernel_symbol_item *beg = NULL, *end = NULL, *mid = NULL;
	kdbg_elf_kernel_symbol_item *temp_item = NULL;
	int ret = 1;
	int found = 0;

	if (!plist->sym_str_size) {
		sym_printk("No DynSym and Sym Info present\n");
		return -EINVAL;
	}

	if (!pfunc_name) {
		PRINT_KD("[%s] : pfunc_name is NULL\n", __FUNCTION__);
		return -EINVAL ;
	}


	/* Array is sorted, Implement Binary Search alogorithm */
	/* Search for the address, if address not match exactly take the previouse
	 * value */

	/* Sanity check for existence*/
	beg = plist->kdbg_elf_sym_head;

	if (plist->elf_symbol_count > 0 && beg && beg->st_value <= address) {

		/* every thing store in array no need to verify pointer for existence*/
		end = plist->kdbg_elf_sym_head + (plist->elf_symbol_count - 1);

		while (beg <= end) {
			mid = beg + (end - beg)/2;
			/* is the address in lower or upper half? */
			if (address < mid->st_value)
				end = mid - 1;     /* new end */
			else if (address == mid->st_value) {
				found = 1;
				temp_item = mid;
				break;
			} else
				beg = mid + 1;     /* new beginning */
		}

		if (!found) {
			/* the position less than the address */
			temp_item = end;
		}

		/* checks */
		BUG_ON(temp_item < plist->kdbg_elf_sym_head);
		if ((temp_item >= plist->kdbg_elf_sym_head + plist->elf_symbol_count)) {
			sym_errk("---------------- BUG BUG -------------\n");
			sym_errk("temp_item [%p]  plist->kdbg_elf_sym_head [%p] last= [%p] count [%d]\n",
					temp_item, plist->kdbg_elf_sym_head, plist->kdbg_elf_sym_head + plist->elf_symbol_count,
					plist->elf_symbol_count);
		}

		BUG_ON(temp_item >= plist->kdbg_elf_sym_head + plist->elf_symbol_count);
		BUG_ON(temp_item > plist->kdbg_elf_sym_head
				&& temp_item->st_value < temp_item[-1].st_value);
		BUG_ON((temp_item < plist->kdbg_elf_sym_head + plist->elf_symbol_count - 1)
				&& temp_item->st_value > temp_item[1].st_value);

		/* more checks */
		if (temp_item < plist->kdbg_elf_sym_head + plist->elf_symbol_count - 1) {
			if (temp_item->st_value != temp_item[1].st_value) {
				BUG_ON(address < temp_item->st_value);
				BUG_ON(address >= temp_item[1].st_value);
			} else {
				BUG_ON(address < temp_item->st_value);
			}
		} else {
			BUG_ON(temp_item != plist->kdbg_elf_sym_head + plist->elf_symbol_count - 1);
			BUG_ON(address < temp_item->st_value);
		}
	}

	/* item found, now check if it lies within function size. */
	if (temp_item && (address < (temp_item->st_size + temp_item->st_value))) {
#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
		kdbg_elf_print_vma (temp_item->st_value, LONG_HEX);
#endif
		sym_printk (" %-7s", kdbg_elf_get_symbol_type
				(ELF_ST_TYPE (temp_item->st_info)));
		sym_printk("File Loading : %s :: Index : %d\n", plist->elf_name_actual_path,
				temp_item->st_name);

		if (plist->sym_buff) {
			snprintf(pfunc_name, symbol_len, "%s",
					KDBG_ELF_SYM_NAME(temp_item, plist->sym_buff, plist->sym_str_size));
		} else {
			if (kdbg_elf_config_low_mem_read_enable) {
				kdbg_elf_find_func_name(plist, temp_item->st_name, pfunc_name, symbol_len);
				pfunc_name[symbol_len - 1] = '\0';
			} else {
				strncpy(pfunc_name, "###", sizeof("###"));
				pfunc_name[symbol_len - 1] = '\0';
			}
		}

		*start_addr = temp_item->st_value;
		sym_printk("[pfunc_name]  :: %s\n", pfunc_name);
		ret = 0;
	} else{
		ret = -ENOMEM;
	}

	return ret;
}

/*
 *   Listing all ELF USB files
 */
static int kdbg_elf_sym_list_db_stats(void)
{
	kdbg_elf_usb_elf_list_item *plist = NULL;
	struct list_head *pos;
	int idx = 0;

	sym_printk("enter\n");
	list_for_each(pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			return -1;
		}

		if (!idx) {
			PRINT_KD ("Index\tUSB_PATH\t\t\tELF_Name\t\tSymbol_Count\tLoding Option\tstatus\n");
			PRINT_KD ("================================="
					"=============================="
					"===========================================\n");
		}
		PRINT_KD("%2d\t", ++idx);
		PRINT_KD("%-24s\t", plist->path_name);
		PRINT_KD("%-16s\t", plist->elf_name);
		PRINT_KD("%-8d\t", plist->elf_symbol_count);
		if (plist->sym_buff)
			PRINT_KD("FULL MEM\t");
		else
			PRINT_KD("LOW MEM\t\t");

		switch (plist->elf_status) {
		case KDBG_ELF_NO_SYMBOL:
			PRINT_KD("NO_SYMBOL\n");
			break;
		case KDBG_ELF_MISMATCH:
			PRINT_KD("MISMATCH\n");
			break;
		case KDBG_ELF_MATCH:
			PRINT_KD("MATCH\n");
			break;
		case KDBG_ELF_SYMBOLS:
			PRINT_KD("SYMBOLS\n");
			break;
		default:
			PRINT_KD ("\n");
		}
	}

	if (!plist) {
		PRINT_KD ("NO ELF Files Found in Database\n");
		return -1;
	}

	return 0;
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
/*
 *   List all the Symbol info from Symbol database
 */
static int kdbg_elf_sym_list_elf_db(void)
{
	kdbg_elf_kernel_symbol_item *t_item;
	kdbg_elf_usb_elf_list_item *plist;
	struct list_head *usb_pos;
	int idx = 0;
	int index = 0;
	int choice;
	char *sym_buff = NULL;
	int sym_idx = 0;

	sym_printk("enter\n");

	sym_buff = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, AOP_MAX_SYM_NAME_LENGTH, GFP_KERNEL);
	if (!sym_buff) {
		sym_errk("elf_sym_buff: memory alloc failure\n");
		return -EINVAL ;
	}

	if (kdbg_elf_sym_list_db_stats()) {
		KDBG_MEM_DBG_KFREE(sym_buff);
		return -ENOENT;
	}

	PRINT_KD ("Select the File ==>");
	choice = debugd_get_event_as_numeric(NULL, NULL);
	if (choice == 0) {
		KDBG_MEM_DBG_KFREE(sym_buff);
		return 0;
	}

	sym_printk("Symbol list using list_for_each()\n");
	PRINT_KD ("   Num:    Value  Size Type    Bind   Name\n");

	list_for_each(usb_pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			KDBG_MEM_DBG_KFREE(sym_buff);
			return -ENOMEM;
		}

		if (++index == choice) {
			sym_printk("%s", plist->elf_name);
			if (!plist->sym_str_size) {
				PRINT_KD ("No DynSym and Sym Info present in ELF File : %s\n",
						plist->elf_name);
				KDBG_MEM_DBG_KFREE(sym_buff);
				return 0;
			}

			t_item = plist->kdbg_elf_sym_head;
			for (sym_idx = 0; sym_idx < plist->elf_symbol_count; sym_idx++, t_item++) {
				if (t_item)	{
					PRINT_KD ("%6d: ", idx++);
					kdbg_elf_print_vma (t_item->st_value, LONG_HEX);
					PRINT_KD(" %5d", t_item->st_size);
					PRINT_KD (" %-7s", kdbg_elf_get_symbol_type
							(ELF_ST_TYPE (t_item->st_info)));
					PRINT_KD (" %-6s", kdbg_elf_get_symbol_binding
							(ELF_ST_BIND (t_item->st_info)));
					sym_printk("File Loading : %s :: Index : %d\n",
							plist->elf_name_actual_path, t_item->st_name);

					if (plist->sym_buff) {
						PRINT_KD (" %s \n", KDBG_ELF_SYM_NAME(t_item, plist->sym_buff,
									plist->sym_str_size));
					} else {
						kdbg_elf_find_func_name(plist, t_item->st_name, sym_buff,
								AOP_MAX_SYM_NAME_LENGTH);
						PRINT_KD (" %s \n", sym_buff);
					}
				} else {
					sym_errk("t_item is NULL\n");
					KDBG_MEM_DBG_KFREE(sym_buff);
					return -ENOMEM ;
				}
			}
		}
	}

	BUG_ON(!sym_buff);
	KDBG_MEM_DBG_KFREE(sym_buff);

	return 0;
}
#endif /*(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */

/*
 *   Delete all ELF file from sym database
 */
static void kdbg_elf_sym_db_delete(void)
{
	kdbg_elf_usb_elf_list_item *plist;
	struct list_head *usb_pos, *q;

	sym_printk("enter\n");

	list_for_each_safe(usb_pos, q, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			return ;
		}

		list_del(usb_pos);

		if (plist->sym_buff) {
			KDBG_MEM_DBG_VFREE(plist->sym_buff);
			plist->sym_buff = NULL;
		}
#ifdef CONFIG_DWARF_MODULE
		if (plist->dbg_line_tables)	{
			kdbg_elf_delete_line_info_table(plist->dbg_line_tables);
			plist->dbg_line_tables = NULL;
		}
#endif /* CONFIG_DWARF_MODULE */

		KDBG_MEM_DBG_VFREE(plist);
		plist = NULL;
	}

	return;
}

/*
 *   Symbol info related datbase deletion  from the link list
 */
void kdbg_elf_sym_delete(void)
{
	kdbg_elf_usb_elf_list_item *plist = NULL;
	struct list_head *usb_pos;

	sym_printk("enter\n");
	PRINT_KD("Unloading  the ELF database......\n");

	/* now let's be good and free the kdbg_elf_kernel_symbol_item items. since we will be removing
	 * items off the list using list_del() we need to use a safer version of the list_for_each()
	 * macro aptly named list_for_each_safe(). Note that you MUST use this macro if the loop
	 * involves deletions of items (or moving items from one list to another).
	 */
	list_for_each(usb_pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			return ;
		}

		if (plist->kdbg_elf_sym_head) {
			KDBG_MEM_DBG_VFREE(plist->kdbg_elf_sym_head);
			plist->kdbg_elf_sym_head = NULL;
		}
		plist->elf_symbol_count = 0;
	}

	if (!plist) {
		PRINT_KD ("NO ELF Files Found in Database\n");
		return;
	}

	kdbg_elf_sym_db_delete();

	PRINT_KD("Unloaded the ELF database\n");
	return;
}

/*
 *   Delete ELF database for partcular ELF name matches
 */
static int  kdbg_elf_sym_delete_given_elf_db(const char *elf_name, char *elf_file, int *elf_mismatch)
{
	kdbg_elf_usb_elf_list_item *plist;
	struct list_head *usb_pos, *q;

	sym_printk("enter\n");

	list_for_each_safe(usb_pos, q, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);

		if (!plist) {
			sym_errk("plist is NULL\n");
			return -1;
		}

		if (!strcmp(plist->elf_name, elf_name)) {
			*elf_mismatch = kdbg_elf_chk_status(plist, elf_file);
			if (*elf_mismatch < 0)
				return -1;

			if (plist->kdbg_elf_sym_head) {
				KDBG_MEM_DBG_VFREE(plist->kdbg_elf_sym_head);
				plist->kdbg_elf_sym_head = NULL;
			}

			if (plist->sym_buff) {
				KDBG_MEM_DBG_VFREE(plist->sym_buff);
				plist->sym_buff = NULL;
			}

#ifdef CONFIG_DWARF_MODULE
			if (plist->dbg_line_tables)	{
				kdbg_elf_delete_line_info_table(plist->dbg_line_tables);
				plist->dbg_line_tables = NULL;
			}
#endif /* CONFIG_DWARF_MODULE */
			list_del(usb_pos);
			KDBG_MEM_DBG_VFREE(plist);
			plist = NULL;
			return 0;
		}
	}
	return -1;
}

/*
 *  Get the func name  after Search the symbol by
 *  recieving filename and addres(program counter)
 */
int kdbg_elf_get_symbol(char *pfilename, unsigned int symbol_addr,
		unsigned int symbol_len,  struct aop_symbol_info *symbol_info)
{
	kdbg_elf_usb_elf_list_item *plist;
	struct list_head *pos;
	int retval;

	sym_printk("enter\n");

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return -1;
	}

	list_for_each(pos, &kdbg_elf_usb_head_item)
	{
		plist = list_entry(pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("pList is NULL\n");
			goto ERR;
		}

		if (!strcmp(plist->elf_name, pfilename)) {
			sym_printk("[%s] : ELF Name Match!!!!\n", pfilename);
			sym_printk("  Type:                              %s\n",
					kdbg_elf_get_file_type (plist->file_type));
			sym_printk("Virtual Address : 0x%08lx\n ",
					(unsigned long) plist->virtual_addr);
			symbol_info->virt_addr = plist->virtual_addr;
			symbol_addr += plist->virtual_addr;
			retval = kdbg_elf_sym_find(symbol_addr, plist, symbol_info->pfunc_name,
					symbol_len, &symbol_info->start_addr);

			if (retval == 0) {
				symbol_info->start_addr =
					symbol_info->start_addr - symbol_info->virt_addr;
			} else {
				symbol_info->start_addr = symbol_addr - symbol_info->virt_addr;
			}

#ifdef CONFIG_DWARF_MODULE
			if (symbol_info->df_info_flag && !kdbg_elf_dbg_line_search_table(plist, (unsigned long)symbol_addr,
						symbol_info->pdf_info)) { /* Namit: for searching vma addr ..*/
				sym_printk("filename and line no not found ..\n");
			}
#endif /* CONFIG_DWARF_MODULE */
			return retval;
		}
	}
	/* 	else  */ /*No symbol found .... */

#ifdef CONFIG_DWARF_MODULE
	if (symbol_info->df_info_flag && symbol_info->pdf_info) {
		strncpy(symbol_info->pdf_info->df_file_name, "??", 3);
		symbol_info->pdf_info->df_file_name[3] = 0;
		symbol_info->pdf_info->df_line_no = 0;
	}
#endif

	sym_printk("[%s] : File NOT FOUND In DATABASE : NO SYMBOL\n", pfilename);

ERR:
	symbol_info->virt_addr = 0;
	symbol_info->start_addr = symbol_addr - symbol_info->virt_addr;
	return -1;
}
/*Give the ELF name without path name out of the full name*/
char *kdbg_elf_base_elf_name(const char *file)
{
	const char *pStr = 0;
	int  sepPos = -1;
	int ii = 0;
	int bSpace = 1;

	if (!file) {
		return NULL;
	}

	pStr = file;

	for (ii = 0; *pStr; ++ii, ++pStr) {
		if (*pStr == '/') {
			sepPos = ii;
		}

		if (bSpace && *pStr != ' ' && *pStr != '\t') {
			bSpace = 0;
		}
	}

	if (ii == 0) {
		BUG_ON(!bSpace);
		return NULL;
	}

	if (!bSpace) {
		BUG_ON(!(file && sepPos + 1 <= (int) strlen(file))); /* check right boundary */
		return (char *) (file + (sepPos + 1));
	} else{
		return NULL;
	}
}

/* swap function for topthread_info */
static void swap_symbol_info(void *va, void *vb, int size)
{
	kdbg_elf_kernel_symbol_item *a = va, *b = vb;
	kdbg_elf_kernel_symbol_item  tItem;

	BUG_ON(sizeof(kdbg_elf_kernel_symbol_item) != size);
	memcpy(&tItem, a, size);
	memcpy(a, b, size);
	memcpy(b, &tItem, size);
}

/* compare function for topthread_info using cpu_time */
static int cmp_symbol_info(const void *va, const void *vb)
{
	kdbg_elf_kernel_symbol_item *a = (kdbg_elf_kernel_symbol_item *)va, *b = (kdbg_elf_kernel_symbol_item  *)vb;
	return  a->st_value - b->st_value;
}

static void kdbg_sym_list_sort(kdbg_elf_kernel_symbol_item *kdbg_elf_sym_buff,  int symbol_count)
{
	sym_printk("enter\n");
	sort(kdbg_elf_sym_buff, symbol_count, sizeof(kdbg_elf_kernel_symbol_item), cmp_symbol_info, swap_symbol_info);
}

/* swap function for topthread_info */
static void kdbg_elf_swap_symbol_index(void *va, void *vb, int size)
{
	int *a = va, *b = vb;
	int  tItem;

	BUG_ON(sizeof(int) != size);
	memcpy(&tItem, a, size);
	memcpy(a, b, size);
	memcpy(b, &tItem, size);
}

/* compare function for topthread_info using cpu_time */
static int kdbg_elf_cmp_name_index_info(const void *va, const void *vb)
{
	int *a = (int *)va, *b = (int *)vb;
	/*return  (b - a); (for descending order) */
	return *a - *b; /* (for ascending order) */
}

void kdbg_elf_sym_index_heapSort(int *isyms, int sym_count)
{
	sort(isyms, sym_count, sizeof(isyms[0]), kdbg_elf_cmp_name_index_info, kdbg_elf_swap_symbol_index);
}

int kdbg_elf_bin_search(int *buff, int len, int value, int *pindex)
{
	int beg = 0, end = 0 , mid = 0 ;
	int index_found = 0;

	/* Sanity check for existence*/
	beg = 0;
	end = len;
	BUG_ON(!pindex);
	/* every thing store in array no need to verify pointer for existence*/
	while (beg <= end) {
		mid = beg + (end - beg)/2;
		/* is the address in lower or upper half? */
		if (value  < buff[mid])
			end = mid - 1;     /* new end */
		else if (value  == buff[mid]) {
			index_found = 1;
			break;
		} else
			beg = mid + 1;     /* new beginning */
	}

	if (!index_found) {
		/* the position less than the address */
		*pindex = end;
	} else{
		*pindex = mid;
	}

	return index_found;

}


int kdbg_elf_prune_symstr_for_func(kdbg_elf_usb_elf_list_item *plist)
{
	char *symstrbuf = NULL;
	int si = 0;
	uint32_t newSymBuffSize = 0;
	int ret = 0;
	int *g_arr = NULL;
	int *p_arr = NULL;
	kdbg_elf_kernel_symbol_item *psyms = NULL;
	const int corruptindex = 0;
	int prev = -1;
	int last_index = 0;
	int buf_len = 0;
	int new_index = 0;
	int duplicateSym = 0;

	PRINT_KD("Optimizing size...\n");
	sym_printk("Symbol Count = %d\n", plist->elf_symbol_count);

	prev = -1;
	psyms = plist->kdbg_elf_sym_head;

	for (si = 0; si < plist->elf_symbol_count; si++, psyms++)
		if (psyms->st_value != prev) {
			sym_printk("[Before] [%d <-- %d]:  prev = %08x,  (%08x <-- %08x)\n",
					last_index, si, prev, plist->kdbg_elf_sym_head[last_index].st_value,  psyms->st_value);
			prev = plist->kdbg_elf_sym_head[last_index].st_value  = psyms->st_value;
			plist->kdbg_elf_sym_head[last_index] = *psyms;
			last_index++;
		} else
			duplicateSym++;

	PRINT_KD("===> Duplicate Addresses = %d:  [%d/%d]\n", duplicateSym, last_index, plist->elf_symbol_count);
	plist->elf_symbol_count = last_index;

	duplicateSym = 0;
	prev = -1;
	last_index = 0;

	g_arr = (int *)KDBG_MEM_DBG_KMALLOC
				(KDBG_MEM_ELF_MODULE, sizeof(int)*plist->elf_symbol_count, GFP_KERNEL);
	if (!g_arr) {
		sym_errk("[g_arr] out of memory\n");
		return -ENOMEM;
	}

	psyms = plist->kdbg_elf_sym_head;

	for (si = 0; si < plist->elf_symbol_count; si++, psyms++)
		g_arr[si] = psyms->st_name;

	kdbg_elf_sym_index_heapSort(g_arr, plist->elf_symbol_count);

	for (si = 0; si < plist->elf_symbol_count; si++) {
		if (g_arr[si] != prev) {
			prev = g_arr[last_index++]  = g_arr[si];
		} else
			duplicateSym++;
	}

	p_arr = (int *)KDBG_MEM_DBG_KMALLOC
				(KDBG_MEM_ELF_MODULE, sizeof(int)*last_index, GFP_KERNEL);
	if (!p_arr) {
		sym_errk("[p_arr] out of memory\n");
		ret  = -ENOMEM;
		goto ERR;
	}

	PRINT_KD("Duplicate/Symbols: %d / %d\n", duplicateSym, plist->elf_symbol_count);

	newSymBuffSize = sizeof("<CORRUPT>");

	for (si = 0; si < last_index; si++) {
		if (g_arr[si] >= plist->sym_str_size) {
			p_arr[si] = corruptindex;
		} else{
			p_arr[si] = newSymBuffSize;
			buf_len = strlen(&plist->sym_buff[g_arr[si]]);
			newSymBuffSize += (buf_len + 1);
		}
	}

	symstrbuf = (char *)KDBG_MEM_DBG_VMALLOC
				(KDBG_MEM_ELF_MODULE, sizeof(char)*(newSymBuffSize + 16));
	if (!symstrbuf) {
		sym_errk("[symstrbuf] out of memory\n");
		ret =  -ENOMEM;
		goto ERR1;
	}

	strncpy(symstrbuf, "<CORRUPT>", newSymBuffSize);

	psyms = plist->kdbg_elf_sym_head;

	for (si = 0; si < plist->elf_symbol_count; si++, psyms++) {
		ret = kdbg_elf_bin_search(g_arr, last_index, psyms->st_name, &new_index);
		BUG_ON(!ret);

		if (p_arr[new_index] < newSymBuffSize) {
			int len = strlen(plist->sym_buff + psyms->st_name) + 1;
			if (len >= (newSymBuffSize - p_arr[new_index]))
				len = newSymBuffSize - p_arr[new_index]-1;
			strncpy(symstrbuf + p_arr[new_index], plist->sym_buff + psyms->st_name, len);
		}
		psyms->st_name = p_arr[new_index];
	}

	symstrbuf[newSymBuffSize]  = 0;

	psyms = plist->kdbg_elf_sym_head;

	PRINT_KD("Size Improvement = %d / %d,  No. of Symbols: %d /%d)\n",
			newSymBuffSize, plist->sym_str_size, last_index, plist->elf_symbol_count);

	if (plist->sym_buff) {
		KDBG_MEM_DBG_VFREE(plist->sym_buff);
		plist->sym_buff = NULL;
	}

	plist->sym_buff = symstrbuf;
	plist->sym_str_size = newSymBuffSize;
	ret = 0;

ERR1:

	if (p_arr) {
		KDBG_MEM_DBG_KFREE(p_arr);
		p_arr = NULL;
	}

ERR:
	if (g_arr) {
		KDBG_MEM_DBG_KFREE(g_arr);
		g_arr = NULL;
	}

	return ret;
}

/* load elf database by elf file  */
int load_elf_db_by_elf_file(char *elf_file, int elf_load_from_usb, int elf_match)
{
	kdbg_elf_usb_elf_list_item *plist;
	char *elf_name = NULL;
	struct list_head *usb_pos;

	if (!config_kdbg_elf_module_enable_disable) {
		sym_printk("ELF Module Disable!!!\n");
		return -1;
	}

	if (kdbg_elf_elf_load_option == KDBG_ELF_LOAD_NO_ELF) {
		PRINT_KD ("!!! NO ELF LOADING\n");
		return -1;
	}

	elf_name = kdbg_elf_base_elf_name(elf_file);
	if (!elf_name || elf_name < elf_file) {
		sym_errk("Error in spliting filename(%s)...\n", elf_file);
		return -1;
	}

	/* check file is exists or not */
	list_for_each(usb_pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);

		if (!plist) {
			sym_errk("plist is NULL\n");
			return -1;
		}

		if (!strcmp(plist->elf_name, elf_name)) {
			/* elf file already exists so don't load it again */
			sym_printk("Database Name :: %s\n", plist->elf_name);
			sym_printk("file CMP :: %s\n", plist->elf_name);
			sym_printk ("%s file already loaded\n", elf_file);
			return 0;
		}
	}
	plist = NULL;

	plist = (kdbg_elf_usb_elf_list_item *)KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE,
					sizeof(kdbg_elf_usb_elf_list_item));
	if (!plist) {
		sym_errk("[plist] out of memory\n");
		return -ENOMEM;
	}
	memset(plist, 0, sizeof(kdbg_elf_usb_elf_list_item));
	plist->elf_status = elf_match;

	if (kdbg_elf_sym_read_elf(elf_file, plist, elf_load_from_usb)) {
		PRINT_KD("%s: Failed to read Symbol\n", elf_file);
		KDBG_MEM_DBG_VFREE(plist);
		return -1;
	}

#ifdef CONFIG_DWARF_MODULE
	/* Namit: for reading debug line section ....*/
	if (!kdbg_elf_read_debug_line_table(plist)) {
		/*using prink for brief msg, sym_errk give log string line*/
		PRINT_KD("Debug line section not found\n");
		sym_printk("Failed to read debugline(Dwarf info) section for %s\n", plist->elf_name_actual_path); /* Namit: */
	}
#endif /* CONFIG_DWARF_MODULE */

	if (elf_name == elf_file) {
		plist->path_name[0] = '\0';
	} else{
		BUG_ON(elf_name <= elf_file);
		elf_name[-1] = '\0'; /*We have guarenteed that elf_name is always
				       greater than starting of elf_path ptr*/
		snprintf(plist->path_name, KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN, "%s",
				elf_file);
	}
	snprintf(plist->elf_name,  KDBG_ELF_MAX_ELF_FILE_NAME_LEN, "%s", elf_name);

	sym_printk("Type:%s\n", kdbg_elf_get_file_type (plist->file_type));
	sym_printk("Virtual Address : 0x%08lx \n", (unsigned long) plist->virtual_addr);
	list_add_tail(&plist->usb_elf_list, &kdbg_elf_usb_head_item);

	if (plist->elf_symbol_count && plist->sym_str_size)
		kdbg_sym_list_sort(plist->kdbg_elf_sym_head, plist->elf_symbol_count);

	if (plist->sym_buff) {
		const unsigned long old_jiffies = jiffies;
		if (kdbg_elf_prune_symstr_for_func(plist) != 0)
			PRINT_KD("Optimiztion failed !!!\n");
		else
			PRINT_KD("Time for ELF memory reduction: %lu ms\n", (jiffies - old_jiffies)*1000/HZ);
	}

	if (kdbg_elf_sym_load_notification_func)
		kdbg_elf_sym_load_notification_func(1);

	return 0;
}

/* load elf database from binary of user specified Pid */
static void kdbg_elf_sym_load_elf_by_pid(void)
{
	struct task_struct *p;
	int do_unlock = 1;
	pid_t pid;

	/* load elf database from bianry of user specified Pid */
	PRINT_KD ("Load Symbols From Pid:\n");

#ifdef CONFIG_PREEMPT_RT
	if (!read_trylock(&tasklist_lock)) {
		PRINT_KD ("hm, tasklist_lock write-locked.\n");
		PRINT_KD ("ignoring ...\n");
		do_unlock = 0;
	}
#else
	read_lock(&tasklist_lock);
#endif

	PRINT_KD ("---------------------------\n");
	PRINT_KD (" PID  Command Name\n");
	PRINT_KD ("---------------------------\n");

	for_each_process(p) {
		if (!p->mm)
			PRINT_KD ("%5d [%s]\n", p->pid, p->comm);
		else
			PRINT_KD ("%5d %s\n", p->pid, p->comm);
	}
	PRINT_KD ("---------------------------\n");

	if (do_unlock)
		read_unlock(&tasklist_lock);

	PRINT_KD ("\n");
	PRINT_KD ("Enter the pid to load elf db ==>");
	pid = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD("\n");

	kdbg_elf_load_elf_db_by_pids(&pid, 1);
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
/* load elf database from USB */
static int kdbg_elf_load_from_usb(void)
{
	static struct kdbg_elf_dir_list dir_list;
	static struct kdbg_elf_usb_path usb_path_list;
	int i = 0;
	char *pfull_elf_name = NULL;
	int index = 0;
	int elf_match = KDBG_ELF_SYMBOLS;

	memset(&usb_path_list, 0, sizeof(usb_path_list));

	if (kdbg_elf_usb_detect(&usb_path_list)) {
		return -1;
	}

	if (!elf_config_additive) {
		kdbg_elf_sym_delete();
	}
	/* temp. for storing the ELF full path name to be loaded*/
	pfull_elf_name = (char *) KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, sizeof(char) * KDBG_ELF_MAX_PATH_LEN);
	BUG_ON(!pfull_elf_name);
	pfull_elf_name[0] = '\0';

	for (index = 0; (index < usb_path_list.num_usb) && (index < KDBG_ELF_MAX_USB);
			index++) {
		memset(&dir_list, 0, sizeof(dir_list));
		snprintf(dir_list.path_dir, KDBG_ELF_MAX_PATH_LEN, "%s/%s/",
				usb_path_list.name[index], KDBG_ELF_PATH);
		if (kdbg_elf_dir_read(&dir_list)) {
			sym_errk("ERROR Reading the USB\n");
			continue;
		}

		PRINT_KD("Scanning dir: %s\n", dir_list.path_dir);
		for (i = 0; i < dir_list.num_files; i++) {
			sym_printk(" Type : %d :: %s \n", dir_list.dirent[i].d_type, dir_list.dirent[i].d_name);
			if ((dir_list.dirent[i].d_type == DT_DIR)) {
				continue;
			}

			snprintf(pfull_elf_name, KDBG_ELF_MAX_ELF_FILE_NAME_LEN, "%s%s",
					dir_list.path_dir, dir_list.dirent[i].d_name);

			if (elf_config_additive) {
				/* delete the file if exists */
				kdbg_elf_sym_delete_given_elf_db(dir_list.dirent[i].d_name, pfull_elf_name, &elf_match);
			}

			load_elf_db_by_elf_file(pfull_elf_name, 0, elf_match);
		}
	}
	KDBG_MEM_DBG_VFREE(pfull_elf_name);
	pfull_elf_name = NULL;
	return 0;
}
#endif /* (KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */

/* Scan the current directory for ELF*/
void kdbg_elf_recscandir(const char *root_path)
{
	char **ppath_list = NULL;
	int top = -1; /* stack top */
	char *pfull_elf_name = NULL;
	int ii = 0;
	static struct kdbg_elf_dir_list dir_list;
	const int PATHLIST_ARRAY_SIZE = sizeof(char *) * KDBG_ELF_MAX_PATHLIST_ENTRIES;
	int fd = -1;
	unsigned long cur_jiffies = 0;
	int load_from_usb = 0;
	int elf_match = KDBG_ELF_SYMBOLS;

	ppath_list = (char **)KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, PATHLIST_ARRAY_SIZE);
	BUG_ON(!ppath_list);
	memset(ppath_list, 0, PATHLIST_ARRAY_SIZE);

	/* push root_path to stack */
	ppath_list[++top] = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE,
			sizeof(char) * KDBG_ELF_MAX_PATH_LEN);
	BUG_ON(top);

	BUG_ON(!ppath_list[top]);

	strncpy(ppath_list[top], root_path, KDBG_ELF_MAX_PATH_LEN);
	ppath_list[top][KDBG_ELF_MAX_PATH_LEN - 1] = '\0';

	/* temp. for storing the ELF full path name to be loaded*/
	pfull_elf_name = (char *) KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, sizeof(char) * KDBG_ELF_MAX_PATH_LEN);
	BUG_ON(!pfull_elf_name);
	pfull_elf_name[0] = '\0';

	while (top != -1) {
		sym_printk("top= %d\n", top);
		BUG_ON(top < 0);

		strncpy(dir_list.path_dir, ppath_list[top], KDBG_ELF_MAX_PATH_LEN);
		dir_list.path_dir[KDBG_ELF_MAX_PATH_LEN - 1] = '\0';
		--top;

		PRINT_KD ("Scanning path: [%s]\n", dir_list.path_dir);
		dir_list.num_files = 0;
		if (kdbg_elf_dir_read(&dir_list)) {
			sym_errk("ERROR Reading the USB\n");
		}
		sym_printk("Number of files in dir (%s): %d\n",  dir_list.path_dir, dir_list.num_files);

		/* DFS stack pop loop */
		for (ii = 0; ii < dir_list.num_files; ++ii) {

			if (dir_list.dirent[ii].d_type == DT_LNK) {
				snprintf(pfull_elf_name, KDBG_ELF_MAX_PATH_LEN, "%s/%s",
						dir_list.path_dir, dir_list.dirent[ii].d_name);
				fd = sys_open(pfull_elf_name, O_DIRECTORY, 0);
				if (fd < 0) {
					sym_printk(" Unable to open file\n");
				} else{
					/* close file here */
					sys_close(fd);
					dir_list.dirent[ii].d_type = DT_DIR;
					sym_printk("pfull_elf_name = %s is directory\n", pfull_elf_name);
				}
			}

			if (dir_list.dirent[ii].d_type == DT_DIR) {
				/* skip . and .. */
				if ((dir_list.dirent[ii].d_name[0] == '.'
							&& dir_list.dirent[ii].d_name[1] == '\0')
						|| (dir_list.dirent[ii].d_name[0] == '.'
							&& dir_list.dirent[ii].d_name[1] == '.'
							&& dir_list.dirent[ii].d_name[2] == '\0')) {
					sym_printk("Ignoring %s\n", dir_list.dirent[ii].d_name);
					continue;
				}

				if (top != KDBG_ELF_MAX_PATHLIST_ENTRIES - 1) {
					BUG_ON(top < -1);
					BUG_ON(top >= KDBG_ELF_MAX_PATHLIST_ENTRIES - 1);
					++top;

					/* lazy initialization */
					if (!ppath_list[top]) {
						ppath_list[top] = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, sizeof(char) * KDBG_ELF_MAX_PATH_LEN);
						BUG_ON(!ppath_list[top]);
					}

					snprintf(ppath_list[top], KDBG_ELF_MAX_PATH_LEN,
							"%s/%s", dir_list.path_dir,
							dir_list.dirent[ii].d_name);
					ppath_list[top][KDBG_ELF_MAX_PATH_LEN - 1] = '\0';
					sym_printk("pushed: top= %d, (%s)\n", top, ppath_list[top]);
				} else{
					PRINT_KD ("Stack is full\n");
				}
			} else {
				sym_printk("[F] [%s]\n", dir_list.dirent[ii].d_name);

				snprintf(pfull_elf_name, KDBG_ELF_MAX_PATH_LEN, "%s/%s",
						dir_list.path_dir, dir_list.dirent[ii].d_name);

				if (elf_config_additive) {
					/* delete the file if exists */
					kdbg_elf_sym_delete_given_elf_db(dir_list.dirent[ii].d_name, pfull_elf_name, &elf_match);
				}

				cur_jiffies = jiffies;
				load_from_usb = 1;
				load_elf_db_by_elf_file(pfull_elf_name, load_from_usb, elf_match);
				PRINT_KD("ELF load time= %ld ms\n", (jiffies - cur_jiffies)*1000/HZ);

			}
		}
	}

	for (ii = 0; ii < KDBG_ELF_MAX_PATHLIST_ENTRIES; ++ii) {
		if (ppath_list[ii]) {
			KDBG_MEM_DBG_VFREE(ppath_list[ii]);
			ppath_list[ii] = NULL;
		}
	}
	KDBG_MEM_DBG_VFREE(ppath_list);
	KDBG_MEM_DBG_VFREE(pfull_elf_name);
}
/* load recursive elf database from USB */
static int kdbg_elf_load_recuresive_from_usb(void)
{
	static struct kdbg_elf_usb_path usb_path_list;
	int index = 0;
	char *path_dir = NULL;
	memset(&usb_path_list, 0, sizeof(usb_path_list));

	if (kdbg_elf_usb_detect(&usb_path_list)) {
		return -1;
	}

	path_dir = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, sizeof(char) * KDBG_ELF_MAX_PATH_LEN);
	if (!path_dir) {
		sym_errk("pPath_dir: mem alloc failure\n");
		return -1;
	}
	path_dir[0] = '\0';

	if (!elf_config_additive) {
		kdbg_elf_sym_delete();
	}

	for (index = 0; (index < usb_path_list.num_usb) && (index < KDBG_ELF_MAX_USB);
			index++) {
		snprintf(path_dir, KDBG_ELF_MAX_PATH_LEN, "%s/%s/",
				usb_path_list.name[index], KDBG_ELF_PATH);
		kdbg_elf_recscandir(path_dir);
	}

	KDBG_MEM_DBG_VFREE(path_dir);
	path_dir = NULL;

	return 0;
}

static int 	kdbg_elf_sym_list_usb_files (void)
{
	static struct kdbg_elf_dir_list dir_list;
	static struct kdbg_elf_usb_path usb_path_list;
	int index = 0;
	int i = 0;

	memset(&usb_path_list, 0, sizeof(usb_path_list));
	if (kdbg_elf_usb_detect(&usb_path_list)) {
		return -1;
	}

	for (index = 0; (index < usb_path_list.num_usb) && (index < KDBG_ELF_MAX_USB);
			index++) {
		memset(&dir_list, 0, sizeof(dir_list));
		snprintf(dir_list.path_dir, KDBG_ELF_MAX_PATH_LEN, "%s/%s/",
				usb_path_list.name[index], KDBG_ELF_PATH);
		if (kdbg_elf_dir_read(&dir_list) == -1) {
			PRINT_KD ("ERROR Reading the USB\n");
			continue;
		}

		if (dir_list.num_files) {
			PRINT_KD ("There are %d files(s) found in %s\n",
					dir_list.num_files, dir_list.path_dir);
			for (i = 0; i < dir_list.num_files; i++) {
				PRINT_KD (" [%d] : type = %d - %s \n", (i+1), dir_list.dirent[i].d_type, dir_list.dirent[i].d_name);
			}
		} else
			PRINT_KD ("Files not found in aop_bin/\n");

	}

	return 0;
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
static int kdbg_elf_sym_search_symbol_test(void)
{
	int idx = 0;
	kdbg_elf_usb_elf_list_item *plist;
	struct list_head *pos;
	char buff[KDBG_ELF_MAX_ELF_FILE_NAME_LEN] = {0,};
	int choice = 0;
	unsigned int address = 0;

	idx = 0;
	if (kdbg_elf_sym_list_db_stats())
		return -1;

	PRINT_KD ("Select the File ==>");
	choice = debugd_get_event_as_numeric(NULL, NULL);

	memset(buff, 0, sizeof(buff));

	list_for_each(pos, &kdbg_elf_usb_head_item)
	{
		plist = list_entry(pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			PRINT_KD ("[%s] : pList is NULL\n", __FUNCTION__);
			return -1;
		}

		if (idx++ == choice) {
			unsigned int start_addr;
			PRINT_KD ("\nFile Selected = %s\n", plist->elf_name);
			PRINT_KD ("\nEnter the Addr (In Decimal) ==>");
			address = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD ("\n");

			kdbg_elf_sym_find(address, plist, buff,
					KDBG_ELF_MAX_ELF_FILE_NAME_LEN, &start_addr);
			PRINT_KD ("Func Name :: [%s]\n", buff);
			break;
		}
	}

	return 0;
}
#endif /* #if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */

/* to register elf file load/unload notification function
   this notification is used to reset the kdbg_elf_sym_report database */
void kdbg_elf_sym_register_oprofile_elf_load_notification_func (
		kdbg_elf_symbol_load_notification func)
{
	if (func)
		kdbg_elf_sym_load_notification_func = func;
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
static void kdbg_elf_chk_symbol_size_zero(void)
{
	kdbg_elf_kernel_symbol_item *t_item = NULL;
	kdbg_elf_usb_elf_list_item *plist = NULL;
	struct list_head *usb_pos = NULL;
	int total_sym_count = 0;
	char *sym_buff = NULL;
	int sym_idx = 0;

	sym_buff = (char *)KDBG_MEM_DBG_VMALLOC
		(KDBG_MEM_ELF_MODULE, AOP_MAX_SYM_NAME_LENGTH);
	if (!sym_buff) {
		sym_errk("sym_buff is NULL\n");
		return;
	}

	sym_printk("enter\n");


	list_for_each(usb_pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			KDBG_MEM_DBG_VFREE(sym_buff);
			return ;
		}
		PRINT_KD ("\n");
		PRINT_KD ("Elf_name = %s\n", plist->elf_name);
		total_sym_count = 0;
		PRINT_KD ("   Num:    Value  Size Type    Name\n");

		t_item = plist->kdbg_elf_sym_head;
		for (sym_idx = 0; sym_idx < plist->elf_symbol_count; sym_idx++, t_item++) {

			if (!t_item) {
				sym_errk("t_item is NULL\n");
				KDBG_MEM_DBG_VFREE(sym_buff);
				return ;
			}
			if (t_item->st_size == 0) {
				PRINT_KD ("%6d: ", ++total_sym_count);
				kdbg_elf_print_vma (t_item->st_value, LONG_HEX);
				kdbg_elf_print_vma (t_item->st_size, DEC_5);
				PRINT_KD (" %-7s", kdbg_elf_get_symbol_type
						(ELF_ST_TYPE (t_item->st_info)));
				sym_printk("File Loading : %s :: Index : %d\n",
						plist->elf_name_actual_path, t_item->st_name);

				if (plist->sym_buff) {
					PRINT_KD (" %s \n", KDBG_ELF_SYM_NAME(t_item, plist->sym_buff,
								plist->sym_str_size));
				} else {
					kdbg_elf_find_func_name(plist, t_item->st_name, sym_buff,
							AOP_MAX_SYM_NAME_LENGTH);
					PRINT_KD (" %s \n", sym_buff);
				}
			}
		}
		PRINT_KD ("Total No of Symbol having size Zero = %d\n", total_sym_count);
	}

	BUG_ON(!sym_buff);
	KDBG_MEM_DBG_VFREE(sym_buff);

	return;
}

static void kdbg_elf_symbol_integrity_chk(kdbg_elf_sym_err_chk SYM_ERR_CHK)
{
	kdbg_elf_kernel_symbol_item *t_item = NULL;
	kdbg_elf_kernel_symbol_item *prev_item = NULL;
	kdbg_elf_usb_elf_list_item *plist = NULL;
	struct list_head *usb_pos = NULL;
	int sym_idx = 0;

	sym_printk("enter\n");

	list_for_each(usb_pos, &kdbg_elf_usb_head_item) {
		plist = list_entry(usb_pos, kdbg_elf_usb_elf_list_item, usb_elf_list);
		if (!plist) {
			sym_errk("plist is NULL\n");
			return ;
		}
		PRINT_KD("\n");
		PRINT_KD("Elf_name = %s\n", plist->elf_name);
		PRINT_KD("   Value  Size\n");

		t_item = plist->kdbg_elf_sym_head;
		for (sym_idx = 0; sym_idx < plist->elf_symbol_count; sym_idx++, t_item++) {

			if (!t_item) {
				sym_errk("t_item is NULL\n");
				return ;
			}

			if (prev_item) {

				if (SYM_ERR_CHK == KDBG_ELF_SYM_GAP) {
					if (prev_item->st_value + prev_item->st_size < t_item->st_value) {
						kdbg_elf_print_vma (prev_item->st_value, LONG_HEX);
						kdbg_elf_print_vma (prev_item->st_size, DEC_5);
						PRINT_KD ("\n");
						kdbg_elf_print_vma (t_item->st_value, LONG_HEX);
						kdbg_elf_print_vma (t_item->st_size, DEC_5);
						PRINT_KD ("\n*** AOP warning :: Gap Found b/w two address");
						break;
					}
				} else if (SYM_ERR_CHK == KDBG_ELF_SYM_OVERLAP) {
					if (prev_item->st_value + prev_item->st_size > t_item->st_value) {
						kdbg_elf_print_vma (prev_item->st_value, LONG_HEX);
						kdbg_elf_print_vma (prev_item->st_size, DEC_5);
						PRINT_KD ("\n");
						kdbg_elf_print_vma (t_item->st_value, LONG_HEX);
						kdbg_elf_print_vma (t_item->st_size, DEC_5);
						PRINT_KD ("\n*** AOP warning :: Overlap  Found b/w two address");
						break;
					}
				}
			}

			prev_item = t_item;
		}

		prev_item = NULL;
	}
}

static struct kmem_cache *my_cachep_16;
static struct kmem_cache *my_cachep_20;
static struct kmem_cache *my_cachep_32;

static int  init_my_cache(int blk)
{
	switch (blk) {
	case 16:
		if (!my_cachep_16) {
			PRINT_KD("my_cachep_16\n");
			my_cachep_16 = kmem_cache_create(
					"my_cache_16",   /* Name */
					blk,                    /* Object Size */
					0,                       /* Alignment */
					0,    			  /* Flags */
					NULL);          	  /* Constructor/Deconstructor */
		}
		break;
	case 20:
		if (!my_cachep_20) {
			PRINT_KD("my_cachep_20\n");
			my_cachep_20 = kmem_cache_create(
					"my_cache_20",            /* Name */
					blk,                    /* Object Size */
					0,                     /* Alignment */
					0,    /* Flags */
					NULL);          /* Constructor/Deconstructor */
		}
		break;
	case 32:
		if (!my_cachep_32) {
			PRINT_KD("my_cachep_32\n");
			my_cachep_32 = kmem_cache_create(
					"my_cache_32",           /* Name */
					blk,                    /* Object Size */
					0,                     /* Alignment */
					0,    /* Flags */
					NULL);          /* Constructor/Deconstructor */
		}
		break;
	default:
		PRINT_KD ("UNKNOWN malloc  option\n");
		return -1;
	}

	return 0;
}

void slab_test(int n, long blk)
{
	void *object;
	int i = 0;
	struct kmem_cache *my_cachep = NULL;

	switch (blk) {
	case 16:
		PRINT_KD("my_cachep_16\n");
		my_cachep = my_cachep_16;
		break;
	case 20:
		PRINT_KD("my_cachep_20\n");
		my_cachep = my_cachep_20;
		break;
	case 32:
		PRINT_KD("my_cachep_32\n");
		my_cachep = my_cachep_32;
		break;
	default:
		PRINT_KD ("UNKNOWN blk option\n");
		return;
	}

	PRINT_KD("Cache name is %s\n", kmem_cache_name(my_cachep));
	PRINT_KD("Cache object size is %d\n", kmem_cache_size(my_cachep));

	for (i = 0; i < n; i++) {
		object = kmem_cache_alloc(my_cachep, GFP_KERNEL);

		if (!object) {
			sym_errk("Not Sufficient M/m for Index = %d\n", i);
			break;
		}
	}
	sym_printk("Memory Allocated = %ld\n", blk*n);
	return;
}



static void kdbg_elf_chunk_malloc(int mode)
{
	void *ptr = NULL;
	long bytes = 0;
	int n = 0;
	int i = 0;
	int blk = 0;

	switch (mode) {
	case 0:
		PRINT_KD ("Allocate Memory VAMLLOC(Bytes) = ");
		bytes = debugd_get_event_as_numeric(NULL, NULL);
		if (bytes) {
			PRINT_KD ("\n");
			PRINT_KD ("Enter No of enteries ==>\n");
			n = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			if (n) {
				for (i = 0; i < n; i++) {
					ptr = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, bytes);
					if (!ptr) {
						sym_errk("Not Sufficient M/m for Index = %d\n", i);
					}
				}
			}
			PRINT_KD("Memory Allocated = %ld\n", bytes*n);
		}
		break;
	case 1:
		PRINT_KD ("Allocate Memory KAMLLOC (Bytes) = ");
		bytes = debugd_get_event_as_numeric(NULL, NULL);
		if (bytes) {
			PRINT_KD ("\n");
			PRINT_KD ("Enter No of enteries ==>\n");
			n = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			if (n) {
				for (i = 0; i < n; i++) {
					ptr = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, bytes, GFP_KERNEL);
					if (!ptr) {
						sym_errk("Not Sufficient M/m for Index = %d\n", i);
					}
				}
			}
			PRINT_KD("Memory Allocated = %ld\n", bytes*n);
		}
		break;
	case 2:
		{
			PRINT_KD ("Allocate Memory kmem_cache_alloc......\n");
			PRINT_KD ("Enter size of block : 16(16Bytes)\\ 20(20Bytes)\\ 32(32Bytes)==>\n");
			blk = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD("\n");
			if (blk) {
				if (init_my_cache(blk))
					break;
				PRINT_KD ("\nEnter No of enteries ==>\n");
				n = debugd_get_event_as_numeric(NULL, NULL);
				if (n) {
					slab_test(n, blk);
				}
			}
		}
		break;
	default:
		PRINT_KD ("UNKNOWN malloc option\n");
	}
}

static void kdbg_elf_create_low_memory_chk(void)
{
	static void **ptr;
	static int len;
	int mega_bytes = 0;
	int i = 0;

	PRINT_KD ("Allocate Memory (MB) = ");
	mega_bytes = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD ("\n");

	if (mega_bytes < 0 || mega_bytes > 200) { /* limit check */
		PRINT_KD ("error invalid value [0~200]: %d\n", mega_bytes);
		return;
	}

	if (len > 0) {
		for (i = 0; i < len; i++) {
			KDBG_MEM_DBG_VFREE(ptr[i]);
			sym_printk("Free Index = %x\n", (unsigned int)ptr[i]);
		}
		sym_printk("Free ptr = %x\n", (unsigned int)ptr);
		KDBG_MEM_DBG_VFREE(ptr);
		ptr = NULL;
	}
	len = 0;

	if (mega_bytes) {
		ptr = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, mega_bytes * sizeof (void *));
		if (!ptr) {
			sym_errk("Not Sufficient m/m for ptr\n");
			return;
		}
		sym_printk("Alloc ptr = %x\n", (unsigned int)ptr);

		memset(ptr, 0, mega_bytes * sizeof (void *));
		for (i = 0; i < mega_bytes; i++) {
			ptr[i] = KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, 1024 * 1024);
			if (!ptr[i]) {
				sym_errk("Not Sufficient M/m for Index = %d\n", i);
				break;
			} else {
				memset(ptr[i], 0xA5, 1024 * 1024);
				memset(ptr[i], 0, 1024 * 1024);
			}
			sym_printk("Alloc Index = %x\n", (unsigned int)ptr[i]);
		}
		PRINT_KD("Memory Allocated = %d\n", mega_bytes);
	}
	len = i-1; /* mega_bytes; */
}
#endif  /* #if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)	*/

void kdbg_elf_elf_loading_scenario(void)
{
	PRINT_KD ("------------------------------------------------"
			"--------------------\n");
	PRINT_KD ("ELF Load Option Currently Set :: ");

	switch (kdbg_elf_elf_load_option) {
	case KDBG_ELF_LOAD_DEFAULT:
		PRINT_KD("Default Symbol Load\n");
		break;
	case KDBG_ELF_LOAD_LOW_MEM:
		PRINT_KD("Low Mem Symbol Load\n");
		break;
	case KDBG_ELF_LOAD_FULL_MEM:
		PRINT_KD("FULL MEM Symbol Load\n");
		break;
	case KDBG_ELF_LOAD_VIRT_ADDR:
		PRINT_KD(" Load ELF Virtual Address without Symbol Name\n");
		break;
	case KDBG_ELF_LOAD_NO_ELF:
		PRINT_KD("No ELF Loading\n");
		break;
	default:
		PRINT_KD ("UNKNOWN Symbol Load option\n");
	}

	PRINT_KD ("------------------------------------------------"
			"--------------------\n");

	PRINT_KD (" 1.Default Option : rootfs = LOW MEM, USB = FULL MEM\n"
			" 2. LOW MEM Option\n"
			" 3. FULL MEM Option\n");
	PRINT_KD (" 4. Load ELF Virtual Address without Symbol\n");
	PRINT_KD(" 5. No ELF Loading\n");

	PRINT_KD ("Select the ELF Loading Option ==>");
	kdbg_elf_elf_load_option  = debugd_get_event_as_numeric(NULL, NULL);
	PRINT_KD ("\n");

	if (kdbg_elf_elf_load_option < KDBG_ELF_LOAD_DEFAULT ||
			kdbg_elf_elf_load_option > KDBG_ELF_LOAD_NO_ELF) {
		kdbg_elf_elf_load_option = KDBG_ELF_LOAD_DEFAULT;
	}
}
#ifdef CONFIG_DWARF_MODULE
int kdbg_elf_get_word(struct file *fp, char *buf)
{
	char ch = 0;
	int i = 0;
	int ret = 0;
	/* int ret = fread(&ch,1,  1, fp); */
	ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
	if (ret != 1) {
		*buf = 0;
		return 0;
	}


	while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
		/* ret = fread(&ch,1,  1, fp);	 */
		ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
		if (ret != 1) {
			*buf = 0;
			return 0;
		}
	}


	while (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
		buf[i++] = ch;
		/* ret = fread(&ch,1,  1, fp);	 */
		ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
		if (ret != 1)
			break;
	}

	buf[i] = '\0';
	return i;
}

unsigned int kdbg_elf_hex2dec(char *buf)
{
	char ch = 0;
	int digit = 0;
	unsigned value = 0;

	while (*buf) {
		ch = *buf;
		if (ch >= '0' && ch <= '9')
			digit = ch - '0';
		else if (ch >= 'a' && ch <= 'f')
			digit = 10+ch - 'a';
		else if (ch >= 'A' && ch <= 'F')
			digit = 10+ch - 'A';
		else	{
			PRINT_KD ("Invalid Hex Char[%c]\n", ch);
			digit = 0;
		}
		value = (value  << 4) + digit;
		++buf;
	}

	return value;
}

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
int  kdbg_elf_dbg_dwarf(void)
{
	struct file *elf_filp = NULL; /* File pointer to access file for ELF parsing*/
	unsigned int value = 0;
	static char filename[128];
	static char buff[128];
	mm_segment_t oldfs = get_fs();
	struct aop_symbol_info symbol_info;
	char *func_name = NULL;

	func_name = (char *)KDBG_MEM_DBG_VMALLOC(KDBG_MEM_ELF_MODULE, AOP_MAX_SYM_NAME_LENGTH);
	if (!func_name) {
		sym_errk("func_name: no memory\n");
		return -1;
	}

	symbol_info.pfunc_name = func_name;
	set_fs(KERNEL_DS);

	/* File Open */
	elf_filp = filp_open("/dtv/usb/sda1/dwarf/dwarf.txt", O_RDONLY | O_LARGEFILE, 0);

	if (IS_ERR(elf_filp) || (elf_filp == NULL)) {
		sym_errk("error opening file\n");
		goto ERR_OUT;
	}

	if (elf_filp->f_op->read == NULL) {
		sym_errk("read not allowed\n");
		goto ERR_OUT;
	}

	filename[0] = '\0';
	buff[0] = '\0';

	kdbg_elf_get_word(elf_filp, filename);
	PRINT_KD ("FileName  : [%s]\n", filename);

	while (kdbg_elf_get_word(elf_filp, buff) > 0) {
		PRINT_KD ("Buff : %s\n", buff);
		value = kdbg_elf_hex2dec(buff);
		PRINT_KD ("Value : %d -> %x\n", value, value);
		symbol_info.df_info_flag = 1;
		symbol_info.pdf_info = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE, sizeof(struct aop_df_info), GFP_KERNEL);
		if (symbol_info.pdf_info) {
			kdbg_elf_get_symbol(filename, value, KDBG_ELF_MAX_ELF_FILE_NAME_LEN,  &symbol_info);
			PRINT_KD (" func %s, %s:%d \n", symbol_info.pfunc_name, symbol_info.pdf_info->df_file_name, symbol_info.pdf_info->df_line_no);
			KDBG_MEM_DBG_KFREE(symbol_info.pdf_info);
		}

	}

ERR_OUT:

	if (func_name) {
		KDBG_MEM_DBG_KFREE(func_name);
		func_name = NULL;
	}


	if (!IS_ERR(elf_filp))
		filp_close(elf_filp, NULL);

	set_fs(oldfs);

	return 0;

}
#endif /* defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)*/
#endif /* CONFIG_DWARF_MODULE */
/*
   FUNCTION NAME	 	:	kdbg_elf_sym_kdmenu
DESCRIPTION			:	main entry routine for the ELF Parseing utility
ARGUMENTS			:	option , File Name
RETURN VALUE	 	:	0 for success
AUTHOR			 	:	Gaurav Jindal
 **********************************************/
int kdbg_elf_sym_kdmenu(void)
{
	int operation = 0;
	static struct kdbg_elf_usb_path usb_path_list;
	int ret = 1;

	sym_printk("Update ELF Database as = %d\n", elf_config_additive);

	do {
		if (ret) {
			PRINT_KD ("\n");
			PRINT_KD ("Options are:\n");
			PRINT_KD ("------------------------------------------------"
					"--------------------\n");
			PRINT_KD (" 1 Load All ELF from USB - Recursive (aop_bin/)\n");
			PRINT_KD (" 2 Unload ELF Database\n");
			PRINT_KD (" 3 List Database Stats\n");
			PRINT_KD (" 4 List USB (aop_bin/) files\n");
			PRINT_KD (" 5 Search USB Mount paths\n");
			PRINT_KD (" 6 Load symbols with pid\n");
			PRINT_KD (" 7 Enable/ Disable ELF reading in file (for low-memory) for symbol name\n");
			PRINT_KD (" 8 ELF Loading options\n");
			PRINT_KD (" 9 Runtime Setting to Enable/ Disable ELF Module\n");
			PRINT_KD (" 10 Set ELF Update as Additive or Destructive(Default is Additive)\n");
#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
			PRINT_KD (" 11 Load All ELF from USB (aop_bin/)\n");
			PRINT_KD (" 12 Print symbol info (Internal Test)\n");
			PRINT_KD (" 13 Get Function name(Internal Test)\n");
			PRINT_KD (" 14 Search Symbol (Internal Test)\n");
			PRINT_KD (" 15 Select USB Mount path\n");
			PRINT_KD (" 16 Check For Symbol Size Zero(Internal Test)\n");
			PRINT_KD (" 17 Check For Ovelap Symbols(Internal Test)\n");
			PRINT_KD (" 18 Check For Gap b/w Symbols(Internal Test)\n");
			PRINT_KD (" 19 Low memory test\n");
			PRINT_KD (" 20 Malloc test\n");
#ifdef CONFIG_DWARF_MODULE
			PRINT_KD (" 21 Dwarf Testing (internal Test)\n");
#endif /* CONFIG_DWARF_MODULE */
#endif /* #if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */
			PRINT_KD ("------------------------------------------------"
					"--------------------\n");
			PRINT_KD (" 99 Adv ELF: Exit Menu\n");
			PRINT_KD ("------------------------------------------------"
					"--------------------\n");
			PRINT_KD ("[Adv Oprofile:USB] Option ==>  ");
		}

		operation = debugd_get_event_as_numeric(NULL, NULL);
		/* In the continuation of above PRINT_KD no need to pu */
		PRINT_KD("\n");

		switch (operation) {
		case 1:
			/*  Load All ELF from USB - Recurssive (aop_bin/)\ */
			kdbg_elf_load_recuresive_from_usb();
			ret = 1;
			break;

		case 2:
			/* Unload ELF Database */
			kdbg_elf_sym_delete();

			/* notify elf database is cleared */
			if (kdbg_elf_sym_load_notification_func) {
				kdbg_elf_sym_load_notification_func(0);
			}
			ret = 1;
			break;

		case 3:
			/*  List Database Stats */
			kdbg_elf_sym_list_db_stats();
			ret = 1;
			break;

		case 4:
			/* List USB (aop_bin/) files */
			kdbg_elf_sym_list_usb_files();
			ret = 1;
			break;

		case 5:
			/* Search USB Mount paths */
			memset(&usb_path_list, 0, sizeof(usb_path_list));
			kdbg_elf_usb_detect(&usb_path_list);
			if (!usb_path_list.num_usb)
				PRINT_KD ("USB NOT Connected\n");
			break;

		case 6:
			/* Load symbols with pid */
			kdbg_elf_sym_load_elf_by_pid ();
			ret = 1;
			break;

		case 7:
			/*Enable/ Disable ELF reading in file (for low-memory) for symbol name */
			PRINT_KD ("ELF reading in file (for low-memory) for symbol name : 1[%s] / 0[%s] ==> ",
					kdbg_elf_low_mem_enable_disable_sym_parsing[0],
					kdbg_elf_low_mem_enable_disable_sym_parsing[1]);
			operation = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD ("\n");

			if (operation < 0 || operation > 1)
				PRINT_KD ("Invalid option\n");
			else
				kdbg_elf_config_low_mem_read_enable = operation;
			break;

		case 8:
			/* ELF Loading Options */
			kdbg_elf_elf_loading_scenario();
			break;

		case 9:
			/* Runtime Setting to Enable/ Disable ELF Module */
			PRINT_KD ("Runtime Setting to ELF Module : 0[%s] / 1[%s] ==> ",
					kdbg_elf_module_disable_enable_string[0],
					kdbg_elf_module_disable_enable_string[1]);
			operation = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD ("\n");

			if (operation < 0 || operation > 1) {
				PRINT_KD ("Invalid option\n");
			} else{
				config_kdbg_elf_module_enable_disable = operation;
				if (!config_kdbg_elf_module_enable_disable) {
					/* Unload ELF Database */
					kdbg_elf_sym_delete();

					/* notify elf database is cleared */
					if (kdbg_elf_sym_load_notification_func) {
						kdbg_elf_sym_load_notification_func(0);
					}
				}

				PRINT_KD ("ELF Module %s!!!\n", kdbg_elf_module_disable_enable_string[config_kdbg_elf_module_enable_disable]);
			}
			break;
		case 10:
			/* Set ELF Update as Additive or Destructive(Default is Additive)\ */
			PRINT_KD("------------------------------------------------"
					"--------------------\n");
			/* to update symbol demangle option */
			PRINT_KD("Current Download Status ELF Database as = %s\n",
					elf_config_elf_load_string[elf_config_additive]);
			PRINT_KD("------------------------------------------------"
					"--------------------\n");

			PRINT_KD ("Download ELF Database as : 0[%s] / 1[%s] ==> ",
					elf_config_elf_load_string[0],
					elf_config_elf_load_string[1]);
			operation = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD ("\n");

			if (operation < 0 || operation > 1)
				PRINT_KD("Invalid option\n");
			else
				elf_config_additive = operation;
			break;

#if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0)
		case 11:
			/* Load All ELF from USB (aop_bin/) */
			kdbg_elf_load_from_usb();
			ret = 1;
			break;

		case 12:
			/* Print symbol info (Internal Test) */
			ret = kdbg_elf_sym_list_elf_db();
			ret = 1;
			break;

		case 13:
			/* Get Function name(Internal Test) */
			{
				unsigned int address = 0;
				char buff[KDBG_ELF_MAX_ELF_FILE_NAME_LEN] = {0,};
				struct aop_symbol_info  symbol_info;

				memset(buff, 0, sizeof(buff));
				PRINT_KD ("\n");
				PRINT_KD ("Enter the Addr (In Decimal) ==>");
				address = debugd_get_event_as_numeric(NULL, NULL);
				PRINT_KD ("\n");
				symbol_info.pfunc_name = buff;
#ifdef CONFIG_DWARF_MODULE
				symbol_info.df_info_flag = 0;
				symbol_info.pdf_info = NULL;
#endif /* CONFIG_DWARF_MODULE */
				kdbg_elf_get_symbol("exeDSP", address,
						KDBG_ELF_SYM_NAME_LENGTH_MAX, &symbol_info);
				PRINT_KD ("Func Name :: [%s]\n", buff);
			}
			break;

		case 14:
			/* Search Symbol (Internal Test) */
			ret = kdbg_elf_sym_search_symbol_test();
			break;

		case 15:
			/* Select USB Mount path */
			{
				int index = 0;
				int choice = 0;

				PRINT_KD (" USB Mount Path(s):\n");
				memset(&usb_path_list, 0, sizeof(usb_path_list));
				kdbg_elf_usb_detect(&usb_path_list);
				if (usb_path_list.num_usb) {
					for (index = 0; index < usb_path_list.num_usb; index++) {
						PRINT_KD ("[%d] : %s\n", index, usb_path_list.name[index]);
					}
					PRINT_KD ("Select the USB Path ==>");
					choice = debugd_get_event_as_numeric(NULL, NULL);
					PRINT_KD("\n");
				} else
					PRINT_KD ("USB NOT Connected\n");
			}
			break;

		case 16:
			/* Check For Symbol Size Zero(Internal Test) */
			kdbg_elf_chk_symbol_size_zero();
			break;

		case 17:
			/* Check For Ovelap Symbols(Internal Test) */
			kdbg_elf_symbol_integrity_chk(KDBG_ELF_SYM_OVERLAP);
			break;

		case 18:
			/* Check For Gap b/w Symbols(Internal Test) */
			kdbg_elf_symbol_integrity_chk(KDBG_ELF_SYM_GAP);
			break;

		case 19:
			/* Low memory test */
			kdbg_elf_create_low_memory_chk();
			break;

		case 20:
			PRINT_KD ("Runtime mem alloc test : 0[vmalloc] / 1[kmalloc] / 2[kmem_cache_alloc]==>");
			operation = debugd_get_event_as_numeric(NULL, NULL);
			PRINT_KD ("\n");
			if (operation < 0 || operation > 2) {
				PRINT_KD ("Invalid option\n");
			} else{
				kdbg_elf_chunk_malloc(operation);
			}
			break;

#ifdef CONFIG_DWARF_MODULE
		case 21:
			/* Dwarf Testing (internal Test) */
			kdbg_elf_dbg_dwarf();
			break;
#endif /* CONFIG_DWARF_MODULE */
#endif /* #if defined(KDBG_ELF_DEBUG_ON) && (KDBG_ELF_DEBUG_ON != 0) */
		case 99:
			/* ELF Menu Exit */
			break;
		default:
			PRINT_KD ("ELF invalid option....\n");
			ret = 1; /* to show menu */
			break;
		}
	} while (operation != 99);

	PRINT_KD ("ELF menu exit....\n");

	/* as this return value is mean to show or not show the kdebugd menu options */
	return ret;

}

/*
 * ELF Module init function, which initialize ELF Module and start functions
 * and allocate kdbg mem module (Hash table)
 */
int kdbg_elf_init(void)
{
	/* adv oprofile menu options */
	kdbg_register("ELF: Symbol (ELF) Menu",
			kdbg_elf_sym_kdmenu , NULL, KDBG_MENU_ELF);
	return 0;
}
