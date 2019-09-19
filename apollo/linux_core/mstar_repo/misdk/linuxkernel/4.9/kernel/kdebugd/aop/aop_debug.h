
#ifndef _AOP_DEBUG_H
#define _AOP_DEBUG_H  1

/*debug logs will be on if AOP_DEBUG_ON is 1, otherwise off*/
#define AOP_DEBUG_ON 0

#if defined(AOP_DEBUG_ON) && (AOP_DEBUG_ON != 0)
#define aop_printk(fmt, args...) do { \
	PRINT_KD("AOP: Tr: %s " fmt, __FUNCTION__ , ##args); \
} while (0)
#else
#define aop_printk(fmt, ...) do { } while (0)
#endif /* AOP_DEBUG_ON */

#define aop_errk(fmt, args...) PRINT_KD("AOP: Err: %s:%d: %s()  " fmt, \
		__FILE__, __LINE__, __FUNCTION__ , ##args)

#endif /* #ifndef _AOP_DEBUG_H */
