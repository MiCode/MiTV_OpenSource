/*
 *  linux/drivers/oprofile/kdbg_elf_def.h
 *
 *  ELF file releated defines are defined here
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-09-02 Created by gaurav.jindal@samsung.com.
 *
 */

/* Legal values for e_type (object file type).  */

#ifndef _LINUX_KDBG_ELF_ELF_DEF_H
#define _LINUX_KDBG_ELF_ELF_DEF_H

#include "kdbg_elf_sym_api.h"

#define KDBG_ELF_NONE		0	/* No file type */
#define KDBG_ELF_REL		1	/* Relocatable file */
#define KDBG_ELF_EXEC		2	/* Executable file */
#define KDBG_ELF_DYN		3	/* Shared object file */
#define KDBG_ELF_CORE		4	/* Core file */
#define	KDBG_ELF_NUM		5	/* Number of defined types */
#define KDBG_ELF_LOOS		0xfe00	/* OS-specific range start */
#define KDBG_ELF_HIOS		0xfeff	/* OS-specific range end */
#define KDBG_ELF_LOPROC	0xff00	/* Processor-specific range start */
#define KDBG_ELF_HIPROC	0xffff	/* Processor-specific range end */

/* Legal values for e_machine (architecture).  */

/* Values for e_machine, which identifies the architecture.  These numbers
   are officially assigned by registry@caldera.com.  See below for a list of
   ad-hoc numbers used during initial development.  */

#define EM_NONE		  0	/* No machine */
#define EM_M32		  1	/* AT&T WE 32100 */
#define EM_SPARC	  2	/* SUN SPARC */
#define EM_386		  3	/* Intel 80386 */
#define EM_68K		  4	/* Motorola m68k family */
#define EM_88K		  5	/* Motorola m88k family */
#define EM_486		  6	/* Intel 80486 */	/* Reserved for future use */
#define EM_860		  7	/* Intel 80860 */
#define EM_MIPS		  8	/* MIPS R3000 (officially, big-endian only) */
#define EM_S370		  9	/* IBM System/370 */
#define EM_MIPS_RS3_LE	 10	/* MIPS R3000 little-endian (Oct 4 1999 Draft) Deprecated */

#define EM_PARISC	 15	/* HPPA */

#define EM_VPP550	 17	/* Fujitsu VPP500 */
#define EM_SPARC32PLUS	 18	/* Sun's "v8plus" */
#define EM_960		 19	/* Intel 80960 */
#define EM_PPC		 20	/* PowerPC */
#define EM_PPC64	 21	/* 64-bit PowerPC */
#define EM_S390		 22	/* IBM S/390 */
#define EM_SPU		 23	/* Sony/Toshiba/IBM SPU */

#define EM_V800		 36	/* NEC V800 series */
#define EM_FR20		 37	/* Fujitsu FR20 */
#define EM_RH32		 38	/* TRW RH32 */
#define EM_MCORE	 39	/* Motorola M*Core */	/* May also be taken by Fujitsu MMA */
#define EM_RCE		 39	/* Old name for MCore */
#define EM_ARM		 40	/* ARM */
#define EM_OLD_ALPHA	 41	/* Digital Alpha */
#define EM_SH		 42	/* Renesas (formerly Hitachi) / SuperH SH */
#define EM_SPARCV9	 43	/* SPARC v9 64-bit */
#define EM_TRICORE	 44	/* Siemens Tricore embedded processor */
#define EM_ARC		 45	/* ARC Cores */
#define EM_H8_300	 46	/* Renesas (formerly Hitachi) H8/300 */
#define EM_H8_300H	 47	/* Renesas (formerly Hitachi) H8/300H */
#define EM_H8S		 48	/* Renesas (formerly Hitachi) H8S */
#define EM_H8_500	 49	/* Renesas (formerly Hitachi) H8/500 */
#define EM_IA_64	 50	/* Intel IA-64 Processor */
#define EM_MIPS_X	 51	/* Stanford MIPS-X */
#define EM_COLDFIRE	 52	/* Motorola Coldfire */
#define EM_68HC12	 53	/* Motorola M68HC12 */
#define EM_MMA		 54	/* Fujitsu Multimedia Accelerator */
#define EM_PCP		 55	/* Siemens PCP */
#define EM_NCPU		 56	/* Sony nCPU embedded RISC processor */
#define EM_NDR1		 57	/* Denso NDR1 microprocesspr */
#define EM_STARCORE	 58	/* Motorola Star*Core processor */
#define EM_ME16		 59	/* Toyota ME16 processor */
#define EM_ST100	 60	/* STMicroelectronics ST100 processor */
#define EM_TINYJ	 61	/* Advanced Logic Corp. TinyJ embedded processor */
#define EM_X86_64	 62	/* Advanced Micro Devices X86-64 processor */

#define EM_PDP10	 64	/* Digital Equipment Corp. PDP-10 */
#define EM_PDP11	 65	/* Digital Equipment Corp. PDP-11 */
#define EM_FX66		 66	/* Siemens FX66 microcontroller */
#define EM_ST9PLUS	 67	/* STMicroelectronics ST9+ 8/16 bit microcontroller */
#define EM_ST7		 68	/* STMicroelectronics ST7 8-bit microcontroller */
#define EM_68HC16	 69	/* Motorola MC68HC16 Microcontroller */
#define EM_68HC11	 70	/* Motorola MC68HC11 Microcontroller */
#define EM_68HC08	 71	/* Motorola MC68HC08 Microcontroller */
#define EM_68HC05	 72	/* Motorola MC68HC05 Microcontroller */
#define EM_SVX		 73	/* Silicon Graphics SVx */
#define EM_ST19		 74	/* STMicroelectronics ST19 8-bit cpu */
#define EM_VAX		 75	/* Digital VAX */
#define EM_CRIS		 76	/* Axis Communications 32-bit embedded processor */
#define EM_JAVELIN	 77	/* Infineon Technologies 32-bit embedded cpu */
#define EM_FIREPATH	 78	/* Element 14 64-bit DSP processor */
#define EM_ZSP		 79	/* LSI Logic's 16-bit DSP processor */
#define EM_MMIX		 80	/* Donald Knuth's educational 64-bit processor */
#define EM_HUANY	 81	/* Harvard's machine-independent format */
#define EM_PRISM	 82	/* SiTera Prism */
#define EM_AVR		 83	/* Atmel AVR 8-bit microcontroller */
#define EM_FR30		 84	/* Fujitsu FR30 */
#define EM_D10V		 85	/* Mitsubishi D10V */
#define EM_D30V		 86	/* Mitsubishi D30V */
#define EM_V850		 87	/* NEC v850 */
#define EM_M32R		 88	/* Renesas M32R (formerly Mitsubishi M32R) */
#define EM_MN10300	 89	/* Matsushita MN10300 */
#define EM_MN10200	 90	/* Matsushita MN10200 */
#define EM_PJ		 91	/* picoJava */
#define EM_OPENRISC	 92	/* OpenRISC 32-bit embedded processor */
#define EM_ARC_A5	 93	/* ARC Cores Tangent-A5 */
#define EM_XTENSA	 94	/* Tensilica Xtensa Architecture */
#define EM_IP2K		101	/* Ubicom IP2022 micro controller */
#define EM_CR		103	/* National Semiconductor CompactRISC */
#define EM_MSP430	105	/* TI msp430 micro controller */
#define EM_BLACKFIN	106	/* ADI Blackfin */
#define EM_ALTERA_NIOS2	113	/* Altera Nios II soft-core processor */
#define EM_CRX		114	/* National Semiconductor CRX */
#define EM_CR16		115	/* National Semiconductor CompactRISC - CR16 */
#define EM_SCORE        135	/* Sunplus Score */

/* If it is necessary to assign new unofficial EM_* values, please pick large
   random numbers (0x8523, 0xa7f2, etc.) to minimize the chances of collision
   with official or non-GNU unofficial values.

   NOTE: Do not just increment the most recent number by one.
   Somebody else somewhere will do exactly the same thing, and you
   will have a collision.  Instead, pick a random number.

   Normally, each entity or maintainer responsible for a machine with an
   unofficial e_machine number should eventually ask registry@caldera.com for
   an officially blessed number to be added to the list above.	*/

/* Old version of Sparc v9, from before the ABI;
   This should be removed shortly.  */
#define EM_OLD_SPARCV9		11

/* Old version of PowerPC, this should be removed shortly. */
#define EM_PPC_OLD		17

/* picoJava */
#define EM_PJ_OLD      		99

/* AVR magic number.  Written in the absense of an ABI.  */
#define EM_AVR_OLD		0x1057

/* MSP430 magic number.  Written in the absense of everything.  */
#define EM_MSP430_OLD		0x1059

/* Morpho MT.   Written in the absense of an ABI.  */
#define EM_MT                   0x2530

/* FR30 magic number - no EABI available.  */
#define EM_CYGNUS_FR30		0x3330

/* OpenRISC magic number.  Written in the absense of an ABI.  */
#define EM_OPENRISC_OLD		0x3426

/* DLX magic number.  Written in the absense of an ABI.  */
#define EM_DLX			0x5aa5

/* FRV magic number - no EABI available??.  */
#define EM_CYGNUS_FRV		0x5441

/* Infineon Technologies 16-bit microcontroller with C166-V2 core.  */
#define EM_XC16X   		0x4688

/* D10V backend magic number.  Written in the absence of an ABI.  */
#define EM_CYGNUS_D10V		0x7650

/* D30V backend magic number.  Written in the absence of an ABI.  */
#define EM_CYGNUS_D30V		0x7676

/* Ubicom IP2xxx;   Written in the absense of an ABI.  */
#define EM_IP2K_OLD		0x8217

/* (Deprecated) Temporary number for the OpenRISC processor.  */
#define EM_OR32			0x8472

/* Cygnus PowerPC ELF backend.  Written in the absence of an ABI.  */
#define EM_CYGNUS_POWERPC 	0x9025

/* Alpha backend magic number.  Written in the absence of an ABI.  */
#define EM_ALPHA		0x9026

/* Cygnus M32R ELF backend.  Written in the absence of an ABI.  */
#define EM_CYGNUS_M32R		0x9041

/* V850 backend magic number.  Written in the absense of an ABI.  */
#define EM_CYGNUS_V850		0x9080

/* old S/390 backend magic number. Written in the absence of an ABI.  */
/*#define EM_S390_OLD		0xa390 */

/* Old, unofficial value for Xtensa.  */
#define EM_XTENSA_OLD		0xabc7

#define EM_XSTORMY16		0xad45

/* mn10200 and mn10300 backend magic numbers.
   Written in the absense of an ABI.  */
#define EM_CYGNUS_MN10300	0xbeef
#define EM_CYGNUS_MN10200	0xdead

/* Renesas M32C and M16C.  */
#define EM_M32C			0xFEB0

/* Vitesse IQ2000.  */
#define EM_IQ2000		0xFEBA

/* NIOS magic number - no EABI available.  */
#define EM_NIOS32		0xFEBB

#define EM_CYGNUS_MEP		0xF00D	/* Toshiba MeP */
/* Legal values for e_type (object file type).  */

#define ET_NONE		0	/* No file type */
#define ET_REL		1	/* Relocatable file */
#define ET_EXEC		2	/* Executable file */
#define ET_DYN		3	/* Shared object file */
#define ET_CORE		4	/* Core file */
#define	ET_NUM		5	/* Number of defined types */
#define ET_LOOS		0xfe00	/* OS-specific range start */
#define ET_HIOS		0xfeff	/* OS-specific range end */
#define ET_LOPROC	0xff00	/* Processor-specific range start */
#define ET_HIPROC	0xffff	/* Processor-specific range end */

/* If it is necessary to assign new unofficial KDBG_ELF_* values, please
   pick large random numbers (0x8523, 0xa7f2, etc.) to minimize the
   chances of collision with official or non-GNU unofficial values.  */

#define KDBG_ELF_ALPHA	0x9026

/* Legal values for e_version (version).  */

#define KDBG_ELF_EV_NONE		0	/* Invalid ELF version */
#define KDBG_ELF_EV_CURRENT	1	/* Current version */
#define KDBG_ELF_EV_NUM		2

#define KDBG_ELF_OSABI	7	/* OS ABI identification */
#define KDBG_ELF_ELFOSABI_NONE		0	/* UNIX System V ABI */
#define KDBG_ELF_ELFOSABI_SYSV		0	/* Alias.  */
#define KDBG_ELF_ELFOSABI_HPUX		1	/* HP-UX */
#define KDBG_ELF_ELFOSABI_NETBSD		2	/* NetBSD.  */
#define KDBG_ELF_ELFOSABI_LINUX		3	/* Linux.  */
#define KDBG_ELF_ELFOSABI_SOLARIS	6	/* Sun Solaris.  */
#define KDBG_ELF_ELFOSABI_AIX		7	/* IBM AIX.  */
#define KDBG_ELF_ELFOSABI_IRIX		8	/* SGI Irix.  */
#define KDBG_ELF_ELFOSABI_FREEBSD	9	/* FreeBSD.  */
#define KDBG_ELF_ELFOSABI_TRU64		10	/* Compaq TRU64 UNIX.  */
#define KDBG_ELF_ELFOSABI_MODESTO	11	/* Novell Modesto.  */
#define KDBG_ELF_ELFOSABI_OPENBSD	12	/* OpenBSD.  */
#define KDBG_ELF_ELFOSABI_ARM		97	/* ARM */
#define KDBG_ELF_ELFOSABI_STANDALONE	255	/* Standalone (embedded) application */

#define EI_ABIVERSION	8	/* ABI version */

/*#define EI_PAD		9		Byte index of padding bytes */

/* Legal values for ST_BIND subfield of st_info (symbol binding).  */

#define KDBG_ELF_STB_LOCAL	0	/* Local symbol */
#define KDBG_ELF_STB_GLOBAL	1	/* Global symbol */
#define KDBG_ELF_STB_WEAK	2	/* Weak symbol */
#define	KDBG_ELF_STB_NUM		3	/* Number of defined types.  */
#define KDBG_ELF_STB_LOOS	10	/* Start of OS-specific */
#define KDBG_ELF_STB_HIOS	12	/* End of OS-specific */
#define KDBG_ELF_STB_LOPROC	13	/* Start of processor-specific */
#define KDBG_ELF_STB_HIPROC	15	/* End of processor-specific */

/* Legal values for ST_TYPE subfield of st_info (symbol type).  */

#define KDBG_ELF_STT_NOTYPE	0	/* Symbol type is unspecified */
#define KDBG_ELF_STT_OBJECT	1	/* Symbol is a data object */
#define KDBG_ELF_STT_FUNC	2	/* Symbol is a code object */
#define KDBG_ELF_STT_SECTION	3	/* Symbol associated with a section */
#define KDBG_ELF_STT_FILE	4	/* Symbol's name is file name */
#define KDBG_ELF_STT_COMMON	5	/* Symbol is a common data object */
#define KDBG_ELF_STT_TLS		6	/* Symbol is thread-local data object */
#define	KDBG_ELF_STT_NUM		7	/* Number of defined types.  */
#define KDBG_ELF_STT_LOOS	10	/* Start of OS-specific */
#define KDBG_ELF_STT_HIOS	12	/* End of OS-specific */
#define KDBG_ELF_STT_LOPROC	13	/* Start of processor-specific */
#define KDBG_ELF_STT_HIPROC	15	/* End of processor-specific */

/* Symbol table indices are found in the hash buckets and chain table
   of a symbol hash table section.  This special index value indicates
   the end of a chain, meaning no further symbols are found in that bucket.  */

#define KDBG_ELF_TN_UNDEF	0	/* End of a chain.  */

/* Symbol visibility specification encoded in the st_other field.  */
#define KDBG_ELF_STV_DEFAULT	0	/* Default symbol visibility rules */
#define KDBG_ELF_STV_INTERNAL	1	/* Processor specific hidden class */
#define KDBG_ELF_STV_HIDDEN	2	/* Sym unavailable in other modules */
#define KDBG_ELF_STV_PROTECTED	3	/* Not preemptible, not exported */

/* Legal values for p_type (segment type).  */

#define	KDBG_ELF_PT_NULL		0	/* Program header table entry unused */
#define KDBG_ELF_PT_LOAD		1	/* Loadable program segment */
#define KDBG_ELF_PT_DYNAMIC	2	/* Dynamic linking information */
#define KDBG_ELF_PT_INTERP	3	/* Program interpreter */
#define KDBG_ELF_PT_NOTE		4	/* Auxiliary information */
#define KDBG_ELF_PT_SHLIB	5	/* Reserved */
#define KDBG_ELF_PT_PHDR		6	/* Entry for header table itself */
#define KDBG_ELF_PT_TLS		7	/* Thread-local storage segment */
#define	KDBG_ELF_PT_NUM		8	/* Number of defined types */
#define KDBG_ELF_PT_LOOS		0x60000000	/* Start of OS-specific */
#define KDBG_ELF_PT_GNU_EH_FRAME	0x6474e550	/* GCC .eh_frame_hdr segment */
/*#define KDBG_ELF_PT_GNU_STACK	0x6474e551	Indicates stack executability */
#define KDBG_ELF_PT_GNU_RELRO	0x6474e552	/* Read-only after relocation */
#define KDBG_ELF_PT_LOSUNW	0x6ffffffa
#define KDBG_ELF_PT_SUNWBSS	0x6ffffffa	/* Sun Specific segment */
#define KDBG_ELF_PT_SUNWSTACK	0x6ffffffb	/* Stack segment */
#define KDBG_ELF_PT_HISUNW	0x6fffffff
#define KDBG_ELF_PT_HIOS		0x6fffffff	/* End of OS-specific */
#define KDBG_ELF_PT_LOPROC	0x70000000	/* Start of processor-specific */
#define KDBG_ELF_PT_HIPROC	0x7fffffff	/* End of processor-specific */

/* Legal values for sh_type (section type).  */

#define KDBG_ELF_SHT_NULL	  0	/* Section header table entry unused */
#define KDBG_ELF_SHT_PROGBITS	  1	/* Program data */
#define KDBG_ELF_SHT_SYMTAB	  2	/* Symbol table */
#define KDBG_ELF_SHT_STRTAB	  3	/* String table */
#define KDBG_ELF_SHT_RELA	  4	/* Relocation entries with addends */
#define KDBG_ELF_SHT_HASH	  5	/* Symbol hash table */
#define KDBG_ELF_SHT_DYNAMIC	  6	/* Dynamic linking information */
#define KDBG_ELF_SHT_NOTE	  7	/* Notes */
#define KDBG_ELF_SHT_NOBITS	  8	/* Program space with no data (bss) */
#define KDBG_ELF_SHT_REL		  9	/* Relocation entries, no addends */
#define KDBG_ELF_SHT_SHLIB	  10	/* Reserved */
#define KDBG_ELF_SHT_DYNSYM	  11	/* Dynamic linker symbol table */
#define KDBG_ELF_SHT_INIT_ARRAY	  14	/* Array of constructors */
#define KDBG_ELF_SHT_FINI_ARRAY	  15	/* Array of destructors */
#define KDBG_ELF_SHT_PREINIT_ARRAY 16	/* Array of pre-constructors */
#define KDBG_ELF_SHT_GROUP	  17	/* Section group */
#define KDBG_ELF_SHT_SYMTAB_SHNDX  18	/* Extended section indeces */
/*#define	KDBG_ELF_SHT_NUM		  19		Number of defined types.  */
#define KDBG_ELF_SHT_LOOS	  0x60000000	/* Start OS-specific */
#define KDBG_ELF_SHT_GNU_LIBLIST	  0x6ffffff7	/* Prelink library list */
#define KDBG_ELF_SHT_CHECKSUM	  0x6ffffff8	/* Checksum for DSO content.  */
#define KDBG_ELF_SHT_LOSUNW	  0x6ffffffa	/* Sun-specific low bound.  */
#define KDBG_ELF_SHT_SUNW_move	  0x6ffffffa
#define KDBG_ELF_SHT_SUNW_COMDAT   0x6ffffffb
#define KDBG_ELF_SHT_SUNW_syminfo  0x6ffffffc
#define KDBG_ELF_SHT_GNU_verdef	  0x6ffffffd	/* Version definition section.  */
#define KDBG_ELF_SHT_GNU_verneed	  0x6ffffffe	/* Version needs section.  */
#define KDBG_ELF_SHT_GNU_versym	  0x6fffffff	/* Version symbol table.  */
#define KDBG_ELF_SHT_HISUNW	  0x6fffffff	/* Sun-specific high bound.  */
#define KDBG_ELF_SHT_HIOS	  0x6fffffff	/* End OS-specific type */
#define KDBG_ELF_SHT_LOPROC	  0x70000000	/* Start of processor-specific */
#define KDBG_ELF_SHT_HIPROC	  0x7fffffff	/* End of processor-specific */
#define KDBG_ELF_SHT_LOUSER	  0x80000000	/* Start of application-specific */
/*#define KDBG_ELF_SHT_HIUSER	  0x8fffffff	End of application-specific */

/* Legal values for sh_flags (section flags).  */

/*#define KDBG_ELF_SHF_WRITE	     (1 << 0)	Writable */
/*#define KDBG_ELF_SHF_ALLOC	     (1 << 1)	Occupies memory during execution */
/*#define KDBG_ELF_SHF_EXECINSTR	     (1 << 2)	Executable */
#define KDBG_ELF_SHF_MERGE	     (1 << 4)	/* Might be merged */
#define KDBG_ELF_SHF_STRINGS	     (1 << 5)	/* Contains nul-terminated strings */
#define KDBG_ELF_SHF_INFO_LINK	     (1 << 6)	/* `sh_info' contains SHT index */
#define KDBG_ELF_SHF_LINK_ORDER	     (1 << 7)	/* Preserve order after combining */
#define KDBG_ELF_SHF_OS_NONCONFORMING (1 << 8)	/* Non-standard OS specific handling
						   required */
#define KDBG_ELF_SHF_GROUP	     (1 << 9)	/* Section is member of a group.  */
#define KDBG_ELF_SHF_TLS		     (1 << 10)	/* Section hold thread-local data.  */
#define KDBG_ELF_SHF_MASKOS	     0x0ff00000	/* OS-specific.  */
#define KDBG_ELF_SHF_MASKPROC	     0xf0000000	/* Processor-specific */
#define KDBG_ELF_SHF_ORDERED	     (1 << 30)	/* Special ordering requirement
						   (Solaris).  */
#define KDBG_ELF_SHF_EXCLUDE	     (1 << 31)	/* Section is excluded unless
						   referenced or allocated (Solaris). */

/* Legal values for e_type (object file type).  */

#define KDBG_ELF_NONE		0	/* No file type */
#define KDBG_ELF_REL		1	/* Relocatable file */
#define KDBG_ELF_EXEC		2	/* Executable file */
#define KDBG_ELF_DYN		3	/* Shared object file */
#define KDBG_ELF_CORE		4	/* Core file */
#define	KDBG_ELF_ET_NUM		5	/* Number of defined types */
#define KDBG_ELF_LOOS		0xfe00	/* OS-specific range start */
#define KDBG_ELF_HIOS		0xfeff	/* OS-specific range end */
#define KDBG_ELF_LOPROC	0xff00	/* Processor-specific range start */
#define KDBG_ELF_HIPROC	0xffff	/* Processor-specific range end */

/* Like SHN_COMMON but the symbol will be allocated in the .lbss
   section.  */
#define KDBG_ELF_SHN_X86_64_LCOMMON 	0xff02

#define KDBG_ELF_SHF_X86_64_LARGE	0x10000000

/*<------- For Section Header */

/* Names for the values of the `has_arg' field of `struct option'.  */

#define no_argument		0
#define required_argument	1
#define optional_argument	2

/* This Structure Holds the total no ELF files in USB */
typedef struct {
	struct list_head usb_elf_list;
	char elf_name_actual_path[KDBG_ELF_SYM_MAX_SO_LIB_PATH_LEN];	/* ELF names with path */
	char elf_name[KDBG_ELF_MAX_ELF_FILE_NAME_LEN];	/* ELF names detect from USB */
	kdbg_elf_kernel_symbol_item *kdbg_elf_sym_head;	/* Total no of ELF found in USB */
	int elf_symbol_count;	/* Total no of ELF Symbol Found in the directory */
	int type_sym_dym;	/* 0 = normal, 1 = dynamic */
#if 1				/* FULL_MEM */
	char *sym_buff;		/* Holds Symbol name */
#endif
	uint32_t act_sym_str_size; /* Hold the total sym buff size*/
	uint32_t sym_str_size;	/* Hold the total sym buff size */
	uint32_t sym_str_offset;	/* Hold the total sym buff size */
	char path_name[KDBG_ELF_MAX_PATH_LEN];	/* USB Path */
	uint32_t virtual_addr;	/* Virtual Addr */
	uint16_t file_type;	/* file Type */
#ifdef CONFIG_DWARF_MODULE
	uint32_t dbg_line_buf_size;	/* Namit:  Temporary no need */
	uint32_t dbg_line_buf_offset;	/* Namit:  Temporary no need */
	struct line_table_info *dbg_line_tables;	/* Namit: for debug line table parsing */
#endif				/* CONFIG_DWARF_MODULE */
	int elf_status; /* hold the ELF Status */
} kdbg_elf_usb_elf_list_item;

#endif /* !_LINUX_KDBG_ELF_ELF_DEF_H */
