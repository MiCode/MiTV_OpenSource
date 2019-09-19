/*
 *  kernel/kdebugd/kdbg_key_test_player.h
 *
 *  Advance oprofile related declarations
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05 Created by gaurav.j
 *
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>

#include "kdbg_key_test_player.h"
#include "kdbg_elf_sym_debug.h"

/* Enable debug prints */
#define SYM_DEBUG_ON  0

/* Key Test Player Toggle Event Key */
#define KDBG_KEY_TEST_PLAYER_TOGGLE_KEY "987"
#define KDBG_DELAY_TO_START ((CONFIG_LIFETEST_START_SEC)*1000)
#define KDBG_KEY_TEST_PLAYER_FILE_NAME  CONFIG_LIFETEST_LOG_PATH
#define KDBG_MAX_KEY_DELAY_LEN 64

static struct task_struct *kdbg_key_test_player_task;
struct kdbg_key_test_player g_player = {
				KDBG_KEY_TEST_PLAYER_DEFAULT_DELAY, 1, 0, 0};
static int kdbg_key_test_player_thread_running;

static int kdbg_view_history_logger_file(void);
void kdbg_reg_sys_event_handler(int (*psys_event_handler) (debugd_event_t *));

/* Parse the Key and delay from the file */
int kdbg_read_line(struct file *fp, char *buf, int buf_len)
{
	char ch = 0;
	int i = 0;
	int ret = 0;

	ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
	if (ret != 1) {
		*buf = 0;
		return ret;
	}

	while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
		ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);

		if (ret != 1) {
			*buf = 0;
			return ret;
		}
	}

	while (ch == '#') {
		do {
			ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
			if (ret != 1) {
				*buf = 0;
				return ret;
			}
		} while (ch != '\n');

		while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
			ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
			if (ret != 1) {
				*buf = 0;
				return ret;
			}
		}
	}

	while (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
		buf[i++] = ch;
		ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
		if (ret != 1)
			break;
	}

	while (ch != '\n') {
		ret = fp->f_op->read(fp, &ch, 1, &fp->f_pos);
		if (ret != 1)
			break;
	}

	buf[i] = '\0';
	return i;
}

/* Max size of delay value when converted to string */
#define DELAY_BUFFER_SIZE 8
/* Max size written on file for a kdebugd command */
#define WRITE_LINE_SIZE	(DELAY_BUFFER_SIZE + DEBUGD_MAX_CHARS + 1 + 1)

/* Write key into the history file */
int kdbg_write_line(struct file *fp, char *buf, int buf_len)
{
	int ret = 0;
	char buffer[WRITE_LINE_SIZE];

	if (!fp || !buf || (buf_len <= 0)) {
		PRINT_KD("Error: buf=%p, fp=%p, buf_len=%d", fp, buf, buf_len);
		return -1;
	}

	memset(buffer, '\0', WRITE_LINE_SIZE);
	snprintf(buffer, WRITE_LINE_SIZE, "\n%d\n%s", g_player.delay, buf);

	/* First write delay in file */
	ret = fp->f_op->write(fp, buffer, WRITE_LINE_SIZE, &fp->f_pos);
	if (ret != WRITE_LINE_SIZE)
		return ret;

	return ret;
}

/* Captures events in the history logging file */
int kdbg_capture_history(char *event, int size)
{
	static struct file *key_filp;	/* File pointer to write in file */
	int ret = 0;

	mm_segment_t oldfs = get_fs();

	if (!event || (size <= 0)) {
		PRINT_KD("Error: Event key=%p, size=%d\n", event, size);
		return -1;
	}

	set_fs(KERNEL_DS);

	/* O_CREAT is used because the file is created, if not present
	 * This case happens after clearing the Key Test log file
	 */
	if (g_player.restart) {
		key_filp = filp_open(KDBG_KEY_TEST_PLAYER_FILE_NAME,
					O_CREAT | O_TRUNC | O_LARGEFILE, 0);
		g_player.restart = 0;
	} else {
		key_filp = filp_open(KDBG_KEY_TEST_PLAYER_FILE_NAME,
					O_APPEND | O_LARGEFILE, 0);
	}

	if (IS_ERR(key_filp) || (key_filp == NULL)) {
		PRINT_KD("error opening file OR file not found %s\n",
				KDBG_KEY_TEST_PLAYER_FILE_NAME);
		ret = -1;
		goto auto_out;
	}

	if (key_filp->f_op->write == NULL) {
		PRINT_KD("write not allowed\n");
		ret = -1;
		goto auto_out;
	}

	/* write key on the file */
	ret = kdbg_write_line(key_filp, event, size);

	if (ret < 0) {
		PRINT_KD("error in writing in the file\n");
		ret = -1;
		goto auto_out;
	}

	if (ret == 0) {
		PRINT_KD("Error\n");
		goto auto_out;
	}
auto_out:
	if (!IS_ERR(key_filp))
		filp_close(key_filp, NULL);
	set_fs(oldfs);
	return ret;

}

/* Read the contents of History logger file */
int kdbg_view_history_logger_file()
{
	static struct file *key_filp;	/* File pointer to read file */
	static char buf[16];
	int ret = 0;

	mm_segment_t oldfs = get_fs();

	set_fs(KERNEL_DS);

	key_filp = filp_open(KDBG_KEY_TEST_PLAYER_FILE_NAME,
						O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(key_filp) || (key_filp == NULL)) {
		PRINT_KD("error opening file OR file not found %s\n",
				KDBG_KEY_TEST_PLAYER_FILE_NAME);
		ret = -1;
		goto auto_out;
	}

	if (key_filp->f_op->read == NULL) {
		PRINT_KD("read not allowed\n");
		ret = -1;
		goto auto_out;
	}

	do {
		buf[0] = '\0';
		ret = kdbg_read_line(key_filp, buf, KDBG_MAX_KEY_DELAY_LEN);

		if (ret < 0) {
			PRINT_KD("error in reading the file\n");
			ret = -1;
			goto auto_out;
		}

		if (ret == 0)
			goto auto_out;

		PRINT_KD("%s\n", buf);
	} while (ret > 0);
auto_out:
	if (!IS_ERR(key_filp))
		filp_close(key_filp, NULL);
	set_fs(oldfs);
	return ret;
}

/* Key Test Player Thread, which continously
 * read the Keys/delay from the file
 */
int kdbg_key_test_player_thread(void *arg)
{

	static struct file *key_filp;	/* File pointer to read file */
	static char buf[16];
	int value = 0;
	int ret = 0;
	debugd_event_t event;
	char *ptr = NULL;

	static int first_time = 1;
	mm_segment_t oldfs = get_fs();

	sym_printk("enter\n");

	if (first_time && !kdebugd_running) {
		msleep(KDBG_DELAY_TO_START);
		first_time = 0;
		if (!kdebugd_running) {
			kdebugd_start();
			kdebugd_running = 1;
		} else {
			/* If kdebugd is running, don't start AutoTest */
			return 0;
		}
	}

	set_fs(KERNEL_DS);

	sym_printk("####### Starting Autotest thread #######\n");
	kdbg_key_test_player_thread_running = 1;

	key_filp =
		filp_open(KDBG_KEY_TEST_PLAYER_FILE_NAME, O_RDONLY | O_LARGEFILE,
				0);
	if (IS_ERR(key_filp) || (key_filp == NULL)) {
		PRINT_KD("error opening file OR file not found %s\n",
				KDBG_KEY_TEST_PLAYER_FILE_NAME);
		ret = -1;
		goto auto_out;
	}

	if (key_filp->f_op->read == NULL) {
		PRINT_KD("read not allowed\n");
		ret = -1;
		goto auto_out;
	}

	/* 99 is added to exit from the Key Test Player menu */
	strncpy(event.input_string, "99",
			sizeof(event.input_string) - 1);

	event.input_string[sizeof(event.input_string) - 1] = '\0';

	PRINT_KD("%s\n", event.input_string);

	sym_printk("######  Adding event - %s\n", __func__,
			__LINE__, event.input_string);
	queue_add_event(&kdebugd_queue, &event);

	/* Wait till kdbg_key_test_player_task is NULL */
	while (!kdbg_key_test_player_task)
		msleep(KEY_TEST_PLAYER_START_DELAY);

	while (!kthread_should_stop()) {

		ptr = NULL;
		buf[0] = '\0';

		ret = kdbg_read_line(key_filp, buf, KDBG_MAX_KEY_DELAY_LEN);

		if (ret < 0) {
			PRINT_KD("error in reading the file\n");
			ret = -1;
			goto auto_out;
		}

		if (ret == 0) {
			PRINT_KD("reached at EOF\n");

			PRINT_KD("going to repeat\n");
			key_filp->f_pos = 0;
			ret =
			    kdbg_read_line(key_filp, buf,
					KDBG_MAX_KEY_DELAY_LEN);
			if (ret <= 0) {
				PRINT_KD(" ERROR:in reading line=%d\n",
						__LINE__);
				goto auto_out;

			}
		}

		value = simple_strtoul(buf, NULL, 0);

		sym_printk("######### sleeping for %d\n", value);
		msleep(value);
		ret = kdbg_read_line(key_filp, buf, KDBG_MAX_KEY_DELAY_LEN);

		if (ret <= 0) {
			PRINT_KD("ERROR:: in reading the value of scan\n");
			goto auto_out;
		}

		/* remove leading and trailing whitespaces */
		ptr = strstrip(buf);

		/* create a event */
		strncpy(event.input_string, ptr,
				sizeof(event.input_string) - 1);
		event.input_string[sizeof(event.input_string) - 1] = '\0';

		sym_printk("######  Adding event - %s\n", __FUNCTION__,
				__LINE__, event.input_string);
		PRINT_KD("%s\n", event.input_string);
		queue_add_event(&kdebugd_queue, &event);
	}
auto_out:
	if (!IS_ERR(key_filp))
		filp_close(key_filp, NULL);
	set_fs(oldfs);

	kdbg_key_test_player_thread_running = 0;

	PRINT_KD("##### Exiting autotest thread######\n");
	return ret;
}

/* Key test player stop handler */
int kdbg_key_test_stop_handler(debugd_event_t *event)
{
	if (!strcmp(event->input_string, KDBG_KEY_TEST_PLAYER_TOGGLE_KEY)) {
		kdbg_stop_key_test_player_thread();
		return 0;
	} else
		return 1;
}

/* Start the Key test Player thread - read from the file */
int kdbg_start_key_test_player_thread(void)
{
	int ret = 0;

	PRINT_KD("#####  Starting key test player thread #####\n");
	kdbg_reg_sys_event_handler(kdbg_key_test_stop_handler);
	kdbg_key_test_player_task =
	    kthread_create((void *)kdbg_key_test_player_thread, NULL,
			   "Autotest Thread");
	if (IS_ERR(kdbg_key_test_player_task)) {
		ret = PTR_ERR(kdbg_key_test_player_task);
		kdbg_key_test_player_task = NULL;
		return ret;
	}

	kdbg_key_test_player_task->flags |= PF_NOFREEZE;
	wake_up_process(kdbg_key_test_player_task);

	return ret;
}

/* Stop the Key test Player thread - read from the file */
int kdbg_stop_key_test_player_thread(void)
{
	if (kdbg_key_test_player_thread_running) {
		kdbg_reg_sys_event_handler(NULL);
		PRINT_KD("#####  Stoping key test player thread #####\n");
		if (kdbg_key_test_player_task)
			kthread_stop(kdbg_key_test_player_task);
		kdbg_key_test_player_task = NULL;
		kdbg_key_test_player_thread_running = 0;
	}
	return 0;
}

/*
FUNCTION NAME	 	:	kdbg_key_test_player_kdmenu
DESCRIPTION			:	main entry routine for the Key Test Player
ARGUMENTS			:	option , File Name
RETURN VALUE	 	:	0 for success
AUTHOR			 	:	Gaurav Jindal
 **********************************************/
int kdbg_key_test_player_kdmenu(void)
{
	int operation = 0;
	int delay = 0;
	int ret = 1;
	bool capture = g_player.prev_capture;
	do {
		if (ret) {
			PRINT_KD("\n");
			PRINT_KD("Options are:\n");
			PRINT_KD
			    ("------------------------------------------------"
			     "--------------------\n");
			PRINT_KD(" 1. Set Key events delay in ms (Currently: %d)\n", g_player.delay);
			PRINT_KD(" 2. VIEW Key Test Player log file\n");
			PRINT_KD(" 3. CLEAR Key Test Player log file\n");

			if (capture == 1)
				PRINT_KD(" 4. STOP capturing Key Test Player log\n");
			else if (capture == 0)
				PRINT_KD(" 4. START capturing Key Test Player log\n");

			PRINT_KD(" 5. START Key Test Player (Stop using 987)\n");
			PRINT_KD
			    ("------------------------------------------------"
			     "--------------------\n");
			PRINT_KD(" 99 Key Test Player: Exit Menu\n");
			PRINT_KD
			    ("------------------------------------------------"
			     "--------------------\n");
			PRINT_KD("[Key Test Player] Option ==>  ");
		}

		operation = debugd_get_event_as_numeric(NULL, NULL);
		PRINT_KD("\n");

		switch (operation) {
		case 1:
			PRINT_KD("\n");
			PRINT_KD("Enter the delay: ");
			delay = debugd_get_event_as_numeric(NULL, NULL);
			while (delay < 0) {
				PRINT_KD("\n");
				PRINT_KD("Invalid input, try again!!!\n");
				PRINT_KD("Enter the delay: ");
				delay = debugd_get_event_as_numeric(NULL, NULL);
			}
			g_player.delay = delay;
			PRINT_KD("\n");
			break;
		case 2:
			PRINT_KD("\n");
			if (g_player.restart) {
				PRINT_KD("Key Test Player Log File Empty!!!\n");
				break;
			}
			if (kdbg_view_history_logger_file() == -1)
				PRINT_KD("Error in viewing history logger file!!!\n");
			break;
		case 3:
			PRINT_KD("\n");
			PRINT_KD("Key Test Player Log File Cleared\n");
			g_player.restart = 1;
			break;
		case 4:
			if (capture == 1) {
				PRINT_KD("\n");
				PRINT_KD("#####  Capturing Key Test Player Log File Stopped  #####\n");
				capture = 0;
			} else if (capture == 0) {
				PRINT_KD("\n");
				PRINT_KD("Capturing will start after exiting Key Test Player menu\n");
				capture = 1;
			}
			break;
		case 5:
			if (!kdbg_key_test_player_thread_running) {
				/* Check if the log file is empty */
				if (g_player.restart) {
					PRINT_KD("\n");
					PRINT_KD("Key Test Player Log File empty, capture events first!!!\n");
					ret = 1;
					break;
				}

				/* First, stopping the capture status */
				PRINT_KD("\n");
				PRINT_KD("#####  Capturing Key Test Player Log File Stopped  #####\n");
				capture = 0;
				g_player.capture = 0;
				g_player.prev_capture = 0;

				/* Start the thread */
				kdbg_start_key_test_player_thread();
			} else
				PRINT_KD("Key Test Player already started\n");
			break;
		case 99:
			/* Key Test Player Menu Exit */
			if (capture == 1) {
				g_player.capture = 1;
				g_player.prev_capture = 1;
			} else if (capture == 0) {
				g_player.capture = 0;
				g_player.prev_capture = 0;
			}
			break;

		default:
			PRINT_KD("Key Test Player invalid option....\n");
			ret = 1;	/* to show menu */
			break;
		}
	} while (operation != 99);

	PRINT_KD("Key Test Player menu exit....\n");
	/* as this return value is mean to show or not show the kdebugd menu options */
	return ret;
}

/*
 * Key Test Player  Module init function, which initialize Key Test Player Module and start functions
 * and allocateKey Test Player module.
 */
int kdbg_key_test_player_init(void)
{
	/* Kdbg Key Test Player menu options */
	kdbg_register("KEY DBG: Key Test Player",
		      kdbg_key_test_player_kdmenu, NULL,
		      KDBG_MENU_KEY_TEST_PLAYER);

	return 0;
}
