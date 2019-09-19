

#ifndef _LINUX_AOP_REPORT_H
#define _LINUX_AOP_REPORT_H

#include "aop_debug.h"

/*process all the samples from the buffer*/
int aop_process_all_samples(void);

/*Dump the application data*/
int aop_op_generate_app_samples(void);

/*Dump the library data*/
int aop_op_generate_lib_samples(void);

/*Dump whole data*/
int aop_op_generate_all_samples(void);

/*Dump data- TGID wise*/
int aop_op_generate_report_tgid(void);

/*Dump data- TID wise*/
int aop_op_generate_report_tid(int cpu_wise);

/* Dump callgraph for all samples */
int aop_cp_show_menu(void);
int aop_dump_all_samples_callgraph(void); /* callgraph */

/*free all the resources that are taken by the system
while processing the tid and tgid data*/
void aop_free_tgid_tid_resources(void);

/* collect all the process id and collect elf files belongs to the process
and load the elf database */
int aop_load_elf_db_for_all_samples(void);

/*free all the resources taken by the system while processing the data*/
void aop_free_resources(void);

#if defined(AOP_DEBUG_ON) && (AOP_DEBUG_ON != 0)
/*Print data- TID wise*/
void AOP_PRINT_TID_LIST(const char *msg);
/*Print data- TGID wise*/
void AOP_PRINT_TGID_LIST(const char *msg);
/* Check all AOP resources */
void aop_chk_resources(void);
#else
#define AOP_PRINT_TID_LIST(msg)  do { } while (0)
/*Print data- TGID wise*/
#define AOP_PRINT_TGID_LIST(msg)   do { } while (0)
/* Check all AOP resources */
#define aop_chk_resources()    do { } while (0)
#endif

#endif /* !_LINUX_AOP_REPORT_H */
