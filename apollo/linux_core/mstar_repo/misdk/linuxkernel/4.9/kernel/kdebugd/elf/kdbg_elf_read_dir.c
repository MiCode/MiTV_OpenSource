/*
 *  kernel/kdebugd/elf/KDBG_ELF_read_dir.c
 *
 *  reading files in directory and no of usb conected along with Path
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-08-11  Created by Namit Gupta(gupta.namit@samsung.com)
 *
 */

#include <linux/proc_fs.h>
#include <kdebugd.h>

#include <linux/fs.h>
#include <linux/dirent.h>
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/syscalls.h>

#ifdef KDBG_MEM_DBG
#include "kdbg_util_mem.h"
#endif

#include "kdbg_elf_sym_api.h"
#include "kdbg_elf_sym_debug.h"

/*
 *   The fucntion prototype for reading the directory in given path
 */
static int KDBG_ELF_read_dir_raw(char *filename, unsigned char *buf,
				 int buff_size);

#define KDBG_ELF_DIR_READ_BUFF_SIZE 	(12*1024)
#define KDBG_ELF_FILE_READ_BUFF_SIZE	4096

/* type = 0xFF => all files */
/*
 * init function for initilizing directory_read
 *
 */
int kdbg_elf_dir_read(struct kdbg_elf_dir_list *pdir_list)
{

	char *pbuff = NULL;
	char *dirname = NULL;
	int i = 0;
	int n = 0;
	int ret = 0;
	int total_files = 0;

	if (!pdir_list)
		goto out1;

	pbuff = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
					     KDBG_ELF_DIR_READ_BUFF_SIZE,
					     GFP_KERNEL);
	if (!pbuff) {
		sym_errk("pbuff: no memory\n");
		ret = -1;
		goto out1;	/* no memory!! */
	}
	dirname = pdir_list->path_dir;

	PRINT_KD("dirname %s\n", dirname);

	n = KDBG_ELF_read_dir_raw(dirname, pbuff,
			   KDBG_ELF_DIR_READ_BUFF_SIZE);
	if (n < 0) {
		sym_errk(" error in opening directory\n");
		ret = -1;
		goto out1;
	}

	for (i = 0; i < n; i += pdir_list->dirent[total_files++].d_reclen) {
		if (total_files == KDBG_ELF_MAX_FILES) {
			PRINT_KD
			    ("Number of files TOTAL FILE(%d)exceeds KDBG_ELF_MAX_FILES(%d)\n",
			     total_files, KDBG_ELF_MAX_FILES);
			break;
		}
		memcpy(&pdir_list->dirent[total_files], &pbuff[i],
		       sizeof(struct ___dirent64));
	}

	pdir_list->num_files = total_files;

out1:
	if (pbuff) {
		KDBG_MEM_DBG_KFREE(pbuff);
		pbuff = NULL;
	}

	return ret;

}

/*
 * init function for  initilizing  usb_read
 * to  identify  where usb has been mounted
 * and how many files are there in that usb
 *
 */

int kdbg_elf_usb_detect(struct kdbg_elf_usb_path *usb_path)
{
	unsigned char *read_buf = NULL;
	struct file *filp = NULL;
	int ret = 0;
	int bytesread = 0;
	unsigned char *name_buf = NULL;
	unsigned char *pbuf[10], *pname_buf;	/* 10 string temporary will put */
	int j = 0, i = 0;

	if (!usb_path) {
		ret = -1;
		goto out;
	}

	read_buf = (char *)KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
						KDBG_ELF_FILE_READ_BUFF_SIZE,
						GFP_KERNEL);
	if (!read_buf) {
		sym_errk("read_buf: no memory\n");
		ret = -1;
		goto out;	/* no memory!! */
	}
	/*
	 *  opening file to know where usb has mounted
	 */

	filp = filp_open("/proc/mounts", O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp) || (filp == NULL)) {
		sym_errk("error opening file\n");
		ret = -1;
		goto out;
	}

	if (filp->f_op->read == NULL) {
		sym_errk("read not allowed\n");
		ret = -1;
		goto out_close;
	}

	filp->f_pos = 0;

	PRINT_KD(" USB MOUNT PATH(s):\n");
	name_buf = KDBG_MEM_DBG_KMALLOC(KDBG_MEM_ELF_MODULE,
					KDBG_ELF_MAX_PATH_LEN, GFP_KERNEL);
	if (!name_buf) {
		sym_errk("memory failure\n");
		goto out_close;
	}

	/*
	 *  reading a /proc/mount file here (to know where usb has mounted)
	 */

	/* read only 4 KB data of file. dont bother other data. */

	bytesread =
	     filp->f_op->read(filp, read_buf, KDBG_ELF_FILE_READ_BUFF_SIZE,
			      &filp->f_pos);
	if (bytesread <= 0) {
		sym_errk("error reading file --1  = %p\n", filp->f_op->read);
		ret = -1;
		goto out_close;
	}
	read_buf[bytesread - 1] = 0;

	pbuf[0] = read_buf;

	/*
	 * The idea for this loop is to take all the string
	 * started  with  "/dev/sd"  (usb  make dev node in
	 * /dev/sd**)
	 *  the format of that file is
	 *  "devnode"  "moount dir" "option" ...............
	 *  by this way we can find the directory where usb
	 *  has mounted
	 */

	for (j = 0;; j++) {
		pbuf[j + 1] = NULL;
		/*
		 * pbuf[j]+1 to avoid traping in infinite loop
		 */

		pbuf[j + 1] = strstr((pbuf[j] + 1), "/dev/sd");

		if (pbuf[j + 1]) {
			memset(name_buf, 0, KDBG_ELF_MAX_PATH_LEN);
			/*Search for the space */
			pname_buf = strstr(pbuf[j + 1], " ");
			if (pname_buf) {
				for (i = 1;; i++) {
					if (*(pname_buf + i) == '\n' ||
					    *(pname_buf + i) == '\0'
					    || *(pname_buf + i) == ' ') {
						name_buf[i] = 0;
						PRINT_KD("\t %d:%s \n", j + 1,
							 name_buf);
						snprintf(usb_path->name[j],
							 KDBG_ELF_MAX_PATH_LEN,
							 "%s", name_buf);
						usb_path->num_usb = j + 1;
						break;
					}
					name_buf[i - 1] = *(pname_buf + i);
				}
			}
		} else
			break;
	}

out_close:
	filp_close(filp, NULL);

out:
	if (read_buf)
		KDBG_MEM_DBG_KFREE(read_buf);

	if (name_buf)
		KDBG_MEM_DBG_KFREE(name_buf);

	return ret;

}

/*
 *   The fucntion for reading the directory in given path
 */
static int KDBG_ELF_read_dir_raw(char *filename, unsigned char *buf,
				 int buff_size)
{
	int ret = 0;
	int fd = -1;

	fd = sys_open(filename,
		      O_RDONLY | O_NONBLOCK | O_LARGEFILE | O_DIRECTORY, 0);
	if (fd == -1) {
		sym_errk(" Unable to open file\n");
		ret = -1;
		goto out1;
	}

	/*TODO: It should be done in a clean way ie use struct dirent64 for buff */
	ret =
	    sys_getdents64(fd, (struct linux_dirent64 __user *)buf, buff_size);
	if (ret < 0) {
		sym_errk(" error in sys_getdents64\n");
		goto out2;
	}
out2:
	/* close file here */
	sys_close(fd);
out1:
	return ret;
}
