/*---------------------------------------------------------------------------
//
//	Copyright(C) SMIT Corporation, 2010-2020.
//
//	File    :	usbcimoduledrv.c
//	Purpose :	SMiT USB CI module driver
//	History :
//				2015-10-09 2.0.0 created by Zet Wang.
//				2016-05-05 2.0.1 modified by Hui Liu.
//				2016-05-20 2.0.2 modified by Hui Liu.
//				2016-05-26 2.0.3 modified by Hui Liu.
//				2016-06-29 2.0.4 modified by Hui Liu.
//				2016-07-04 2.0.5 modified by Hui Liu.
//				2016-07-18 2.0.6 modified by Hui Liu.
//				2016-08-01 2.0.7 modified by Hui Liu.
//				2016-08-10 2.0.8 modified by Hui Liu.
//				2016-09-07 2.0.9 modified by Hui Liu.
//				2016-12-14 2.0.10 modified by Hui Liu.
//				2017-03-23 2.0.11 modified by Hui Liu.
//				2017-06-08 2.0.12 modified by Hui Liu.
//				2018-02-27 2.1.0 modified by Hui Liu.
--------------------------------------------------------------------------*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/gfp.h>
#include <linux/compat.h>
#include <asm/current.h>

#define USB_CI_MODULE_DRIVER_VERSION	0x02010000	//Version: 2.1.0.0

#define USB_CIMODULE_COMMAND_MINOR_BASE 	192
#define USB_CIMODULE_MEDIA_MINOR_BASE 		208

#define USB_CIMODULE_COMMAND_MAX_PAGE_ORDER		2
#define USB_CIMODULE_COMMAND_MAX_MALLOC_MEM     0x4000  // 0x4000 = 1024*4*2^(USB_CIMODULE_COMMAND_MAX_PAGE_ORDER)
#define USB_CIMODULE_COMMAND_MAX_KMEM_SIZE 		(USB_CIMODULE_COMMAND_MAX_MALLOC_MEM / 2) // in or out buffer size of command interface

#define USB_CIMODULE_MEDIA_MAX_PAGE_ORDER      	6
#define USB_CIMODULE_MEDIA_MAX_KMEM_SIZE    	0x40000		// 0x40000 = 1024*4*2^(USB_CIMODULE_MEDIA_MAX_PAGE_ORDER)) // in or out buffer size of media interface

#define TRUE	1
#define FALSE	0

#define DEVICE_CLASS				0xEF
#define DEVICE_SUBCLASS				0x02
#define DEVICE_PROTOCOL				0x01

#define MATCHED_INTF_CLASS			0xFF
#define MATCHED_INTF_SUBCLASS		0x11

#define IAD_FUNCTION_CLASS			0xFF
#define IAD_FUNCTION_SUBCLASS		0x11
#define IAD_FUNCTION_PROTOCOL		0x01

#define COMMAND_INTF_CLASS			0xFF
#define COMMAND_INTF_SUBCLASS		0x11
#define COMMAND_INTF_POTOCOL		0x01

#define MEDIA_INTF_CLASS			0xFF
#define MEDIA_INTF_SUBCLASS			0x11
#define MEDIA_INTF_POTOCOL			0x02

#define MEDIA_INTF_READ_ACCESS_MODE		0	//O_RDONLY
#define MEDIA_INTF_WRITE_ACCESS_MODE	2	//O_RDWR

#define USB_DEVICE_INFO_AND_INTF_INFO(dcl, dsc, dpr, icl, isc) \
	.match_flags = USB_DEVICE_ID_MATCH_DEV_INFO \
				| USB_DEVICE_ID_MATCH_INT_CLASS \
				| USB_DEVICE_ID_MATCH_INT_SUBCLASS, \
	.bDeviceClass = (dcl), \
	.bDeviceSubClass = (dsc), \
	.bDeviceProtocol = (dpr), \
	.bInterfaceClass = (icl), \
	.bInterfaceSubClass = (isc)

static struct usb_device_id cimodule_table[] =
{
	{USB_DEVICE_INFO_AND_INTF_INFO(DEVICE_CLASS, DEVICE_SUBCLASS, DEVICE_PROTOCOL, MATCHED_INTF_CLASS, MATCHED_INTF_SUBCLASS)},
	{}
};

MODULE_DEVICE_TABLE(usb, cimodule_table);

#define CIMODULE_DEBUG
#ifdef CIMODULE_DEBUG
#define LOG_ERR(fmt, args...) \
	printk(KERN_ERR "[usbci][%5d, %5d] %s():%i: " fmt "\r\n", (get_current())->tgid, (get_current())->pid, __FUNCTION__, __LINE__, ##args)
#define LOG_INFO(fmt, args...) \
	printk(KERN_INFO "[usbci][%5d, %5d] %s():%i: " fmt "\r\n", (get_current())->tgid, (get_current())->pid, __FUNCTION__, __LINE__, ##args)
#else
#define LOG_ERR(fmt, args...) \
	do{ }while(0)
#define LOG_INFO(fmt, args...) \
	do{ }while(0)
#endif
#define LOG_CRIT(fmt, args...) \
	printk(KERN_CRIT "[usbci][%5d, %5d] %s():%i: " fmt "\r\n", (get_current())->tgid, (get_current())->pid, __FUNCTION__, __LINE__, ##args)

struct usb_cimodule_info
{
	unsigned short m_wVendorId;
	unsigned short m_wProductId;
	uint32_t m_dwCiCompatibility;
	unsigned char m_arReserve[32];
};

//USB CI Module ioctl Command
#define CIMODULE_IOC_MAGIC          				'c'
#define CIMODULE_GET_DRIVER_VERSION  				_IOR(CIMODULE_IOC_MAGIC, 0, uint32_t)
#define CIMODULE_GET_USB_CIMODULE_INFO    			_IOR(CIMODULE_IOC_MAGIC, 1, struct usb_cimodule_info)
#define CIMODULE_RESET   		 	    			_IO(CIMODULE_IOC_MAGIC, 2)
#define CIMODULE_CANCEL_CMD_TRANSFER    			_IO(CIMODULE_IOC_MAGIC, 3)
#define CIMODULE_CANCEL_MEDIA_TRANSFER  			_IO(CIMODULE_IOC_MAGIC, 4)

struct usb_cimodule
{
	struct usb_device *m_ptUsbDev;
	struct usb_interface *m_ptCmdIntf;
	struct usb_interface *m_ptMediaIntf;
	struct usb_endpoint_descriptor *m_ptCmdIn, *m_ptCmdOut;
	struct usb_endpoint_descriptor *m_ptMediaIn, *m_ptMediaOut;

	unsigned char *m_pbCmdInBuf;
	struct urb *m_pbCmdInUrb;
	unsigned char *m_pbCmdOutBuf;
	struct urb *m_pbCmdOutUrb;
	unsigned int m_dwCmdInCondition;
	unsigned int m_dwCmdOutCondition;
	wait_queue_head_t m_tCmdInWait;
	wait_queue_head_t m_tCmdOutWait;
	size_t m_dwCmdReadLen;
	size_t m_dwCmdWriteLen;
	int m_nCmdInErr;
	int m_nCmdOutErr;
	struct mutex m_tCmdOpenMutex;
	unsigned int m_dwCmdOpenRef;

	unsigned char *m_pbMediaInBuf;
	struct urb *m_pbMediaInUrb;
	unsigned char *m_pbMediaOutBuf;
	struct urb *m_pbMediaOutUrb;
	unsigned int m_dwMediaInCondition;
	unsigned int m_dwMediaOutCondition;
	wait_queue_head_t m_tMediaInWait;
	wait_queue_head_t m_tMediaOutWait;
	size_t m_dwMediaReadLen;
	size_t m_dwMediaWriteLen;
	int m_nMediaInErr;
	int m_nMediaOutErr;
	struct mutex m_tMediaReadOpenMutex;
	struct mutex m_tMediaWriteOpenMutex;
	unsigned int m_dwMediaReadOpenRef;
	unsigned int m_dwMediaWriteOpenRef;

	unsigned char m_isCmdIntfReg;
	unsigned char m_isMediaIntfReg;
	struct kref   m_tKref;
	struct usb_cimodule_info m_tUsbCiModuleInfo;

	struct mutex m_tCmdInMutex;
	struct mutex m_tCmdOutMutex;
	struct mutex m_tMediaInMutex;
	struct mutex m_tMediaOutMutex;

	unsigned long m_dwVmCmdReadBuf;
	unsigned long m_dwVmCmdWriteBuf;
	unsigned long m_dwVmMediaReadBuf;
	unsigned long m_dwVmMediaWriteBuf;
};

static struct usb_driver cimodule_driver;
static struct page *gs_ptCmdShmPage = NULL;
static struct page *gs_ptMediaReadShmPage = NULL;
static struct page *gs_ptMediaWriteShmPage = NULL;

static long cimodule_ioctl(struct file *i_pFile, unsigned int i_dwCmd, unsigned long i_dwArg);
#ifdef CONFIG_COMPAT
static long cimodule_compat_ioctl(struct file *i_pFile, unsigned int i_dwOp, unsigned long i_dwArg);
#endif
static int command_intf_open(struct inode *i_ptInode, struct file *i_pFile);
static int command_intf_release(struct inode *i_ptInode, struct file *i_pFile);
static ssize_t command_intf_read(struct file *i_pFile, char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos);
static ssize_t command_intf_write(struct file *i_pFile, const char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos);
static int command_intf_mmap(struct file *i_pFile, struct vm_area_struct *i_ptVma);
static int media_intf_open(struct inode *i_ptInode, struct file *i_pFile);
static int media_intf_release(struct inode *i_ptInode, struct file *i_pFile);
static ssize_t media_intf_read(struct file *i_pFile, char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos);
static ssize_t media_intf_write(struct file *i_pFile, const char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos);
static int media_intf_mmap(struct file *i_pFile, struct vm_area_struct *i_ptVma);
static int cimodule_pre_reset(struct usb_interface *intf);
static int cimodule_post_reset(struct usb_interface *intf);

static const struct file_operations command_intf_fops =
{
	.owner =	THIS_MODULE,
	.open =		command_intf_open,
	.release =	command_intf_release,
	.read = 	command_intf_read,
	.write = 	command_intf_write,
	.mmap = 	command_intf_mmap,
	.unlocked_ioctl = cimodule_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = cimodule_compat_ioctl,
#endif
};

static const struct file_operations media_intf_fops =
{
	.owner =	THIS_MODULE,
	.open =		media_intf_open,
	.release =	media_intf_release,
	.read = 	media_intf_read,
	.write = 	media_intf_write,
	.mmap = 	media_intf_mmap,
	.unlocked_ioctl = cimodule_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = cimodule_compat_ioctl,
#endif
};

static struct usb_class_driver cimodule_commandclass =
{
	.name = "cimodule_command%d",
	.fops = &command_intf_fops,
	.minor_base = USB_CIMODULE_COMMAND_MINOR_BASE,
};

static struct usb_class_driver cimodule_mediaclass =
{
	.name = "cimodule_media%d",
	.fops = &media_intf_fops,
	.minor_base = USB_CIMODULE_MEDIA_MINOR_BASE,
};

static unsigned long cimodule_get_driver_version(struct usb_cimodule *i_ptDevData, unsigned long i_dwArg)
{
	int nRetval;
	unsigned int dwDriverVersion = USB_CI_MODULE_DRIVER_VERSION;

	if (!i_ptDevData || !i_dwArg)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	nRetval = copy_to_user((unsigned int *)i_dwArg, &dwDriverVersion, 4);
	if (nRetval)
	{
		LOG_ERR("copy data to user error");
		return nRetval;
	}

	return nRetval;
}

static unsigned long cimodule_get_usb_cimodule_info(struct usb_cimodule *i_ptDevData, unsigned long i_dwArg)
{
	int nRetval;

	if (!i_ptDevData || !i_dwArg)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	nRetval = copy_to_user((struct usb_cimodule_info*)i_dwArg, &i_ptDevData->m_tUsbCiModuleInfo, sizeof(struct usb_cimodule_info));
	if (nRetval)
	{
		LOG_ERR("copy data to user error");
		return nRetval;
	}

	return nRetval;
}

static unsigned long cimodule_cancel_cmd_transfer(struct usb_cimodule *i_ptDevData)
{
	if (!i_ptDevData)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	if (atomic_read(&i_ptDevData->m_pbCmdInUrb->use_count))
	{
		LOG_INFO("unlink the command read urb");
		usb_unlink_urb(i_ptDevData->m_pbCmdInUrb);
	}
	if (atomic_read(&i_ptDevData->m_pbCmdOutUrb->use_count))
	{
		LOG_INFO("unlink the command write urb");
		usb_unlink_urb(i_ptDevData->m_pbCmdOutUrb);
	}

	return 0;
}

static unsigned long cimodule_cancel_media_transfer(struct file *i_pFile, struct usb_cimodule *i_ptDevData)
{
	if (!i_ptDevData)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_READ_ACCESS_MODE)
	{
		if (atomic_read(&i_ptDevData->m_pbMediaInUrb->use_count))
		{
			LOG_INFO("unlink the media read urb");
			usb_unlink_urb(i_ptDevData->m_pbMediaInUrb);
		}
	}
	else if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_WRITE_ACCESS_MODE)
	{
		if (atomic_read(&i_ptDevData->m_pbMediaOutUrb->use_count))
		{
			LOG_INFO("unlink the media write urb");
			usb_unlink_urb(i_ptDevData->m_pbMediaOutUrb);
		}
	}
	else
	{
		LOG_ERR("cancel media transfer failed: can't support this flags: 0x%x", i_pFile->f_flags);
		return -EPERM;
	}

	return 0;
}

//used for linux 2.6.35 and subsequent version
static long cimodule_ioctl(struct file *i_pFile, unsigned int i_dwOp, unsigned long i_dwArg)
{
	int nRetval = 0;
	struct usb_cimodule *ptDevData = NULL;

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("no device connected\r\n");
		nRetval = -ENODEV;
		return nRetval;
	}

	switch (i_dwOp)
	{
		case CIMODULE_GET_DRIVER_VERSION:
			LOG_INFO("ioctl op: get usb cimoudle driver version");
			nRetval = cimodule_get_driver_version(ptDevData, i_dwArg);
			break;
		case CIMODULE_GET_USB_CIMODULE_INFO:
			LOG_INFO("ioctl op: get usb cimodule info");
			nRetval = cimodule_get_usb_cimodule_info(ptDevData, i_dwArg);
			break;
		case CIMODULE_RESET:
			LOG_INFO("ioctl op: reset usb cimodule");
			usb_lock_device_for_reset(ptDevData->m_ptUsbDev, NULL);
			usb_reset_device(ptDevData->m_ptUsbDev);
			usb_unlock_device(ptDevData->m_ptUsbDev);
			break;
		case CIMODULE_CANCEL_CMD_TRANSFER:
			LOG_INFO("ioctl op: cancel command transfer");
			nRetval = cimodule_cancel_cmd_transfer(ptDevData);
			break;
		case CIMODULE_CANCEL_MEDIA_TRANSFER:
			LOG_INFO("ioctl op: cancel media transfer");
			nRetval = cimodule_cancel_media_transfer(i_pFile, ptDevData);
			break;
		default:
			LOG_ERR("unknown op type %08x", i_dwOp);
			nRetval = -1;
			break;
	}

	return nRetval;
}

#ifdef CONFIG_COMPAT
static long cimodule_compat_ioctl(struct file *i_pFile, unsigned int i_dwOp, unsigned long i_dwArg)
{
	int nRetval = 0;
	unsigned long dwTranslatedArg;

	dwTranslatedArg = (unsigned long)compat_ptr(i_dwArg);
	nRetval = cimodule_ioctl(i_pFile, i_dwOp, dwTranslatedArg);

	return nRetval;
}
#endif

static void cimodule_delete(struct kref *i_tKref)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_tKref)
	{
		LOG_ERR("invalid parameter");
		return;
	}

	ptDevData = container_of(i_tKref, struct usb_cimodule, m_tKref);

	usb_put_dev(ptDevData->m_ptUsbDev);

	usb_kill_urb(ptDevData->m_pbCmdInUrb);
	usb_kill_urb(ptDevData->m_pbCmdOutUrb);
	usb_kill_urb(ptDevData->m_pbMediaInUrb);
	usb_kill_urb(ptDevData->m_pbMediaOutUrb);
	usb_free_urb(ptDevData->m_pbCmdInUrb);
	usb_free_urb(ptDevData->m_pbCmdOutUrb);
	usb_free_urb(ptDevData->m_pbMediaInUrb);
	usb_free_urb(ptDevData->m_pbMediaOutUrb);

	kfree(ptDevData);
	ptDevData = NULL;
	LOG_INFO("command and media interface buffer free succeessfully");
}

static int command_intf_open(struct inode *i_tInode, struct file *o_pFile)
{

	struct usb_cimodule *ptDevData = NULL;
	struct usb_interface *ptIntf = NULL;
	int nSubminor;

	LOG_INFO("enter");

	if (!i_tInode || !o_pFile)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}
	nSubminor = iminor(i_tInode);

	ptIntf = usb_find_interface(&cimodule_driver, nSubminor);
	if (!ptIntf)
	{
		LOG_ERR("can't find device for minor %d", nSubminor);
		return -ENODEV;
	}

	ptDevData = usb_get_intfdata(ptIntf);
	if (!ptDevData)
	{
		LOG_ERR("can not get intfdata");
		return -ENODEV;
	}

	mutex_lock(&ptDevData->m_tCmdOpenMutex);
	if (ptDevData->m_dwCmdOpenRef >= 1)
	{
		LOG_ERR("open failed: command interface is opened before");
		mutex_unlock(&ptDevData->m_tCmdOpenMutex);
		return -EPERM;
	}
	ptDevData->m_dwCmdOpenRef = ptDevData->m_dwCmdOpenRef + 1;
	mutex_unlock(&ptDevData->m_tCmdOpenMutex);

	kref_get(&ptDevData->m_tKref);
	//save our object in the file's private structure
	o_pFile->private_data = ptDevData;
	LOG_INFO("command interface open succeessfully");
	return 0;
}

static int command_intf_mmap(struct file *i_pFile, struct vm_area_struct *i_ptVma)
{
	struct usb_cimodule *ptDevData = NULL;
	unsigned long dwStart = 0;
	unsigned long dwSize = 0;

	//LOG_INFO("enter");
	if (!i_pFile || !i_ptVma)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	dwStart = i_ptVma->vm_start;
	dwSize = i_ptVma->vm_end - i_ptVma->vm_start;

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if (remap_pfn_range(i_ptVma, dwStart, page_to_pfn(gs_ptCmdShmPage + i_ptVma->vm_pgoff), dwSize, i_ptVma->vm_page_prot))
	{
		LOG_ERR("remap failed");
		return -EAGAIN;
	}

	if (((i_ptVma->vm_flags & 0x03) == VM_READ) && (ptDevData->m_dwVmCmdReadBuf == 0))
	{
		ptDevData->m_dwVmCmdReadBuf = i_ptVma->vm_start;
		LOG_ERR("virtaul memory read buffer: 0x%016lx", i_ptVma->vm_start);
	}
	else if (((i_ptVma->vm_flags & 0x03) == VM_WRITE) && (ptDevData->m_dwVmCmdWriteBuf == 0))
	{
		ptDevData->m_dwVmCmdWriteBuf = i_ptVma->vm_start;
		LOG_ERR("virtaul memory write buffer: 0x%016lx", i_ptVma->vm_start);
	}
	LOG_INFO("cmd buffer mmap succeessfully");

	return 0;
}

static void command_intf_write_complete(struct urb *i_ptUrb)
{
	int nStatus;
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptUrb)
	{
		LOG_ERR("invalid parameter");
		return;
	}

	nStatus = i_ptUrb->status;
	ptDevData = i_ptUrb->context;

	if (nStatus)
	{
		ptDevData->m_dwCmdWriteLen = 0;
		ptDevData->m_nCmdOutErr = nStatus;
		LOG_ERR("urb %p write urb error, status %d", i_ptUrb, nStatus);
	}
	else
	{
		ptDevData->m_dwCmdWriteLen = i_ptUrb->actual_length;
		ptDevData->m_nCmdOutErr = 0;
	}

	ptDevData->m_dwCmdOutCondition = 1;
	wake_up_interruptible(&ptDevData->m_tCmdOutWait);
}

static ssize_t command_intf_write(struct file *i_pFile, const char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos)
{
	struct usb_cimodule *ptDevData = NULL;
	size_t u32WriteSize = 0;
	int nRetval = 0;
	int nPipe;

	LOG_INFO("enter");

	if (!i_pFile || !i_pbBuffer || !i_pPpos)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((unsigned long)i_pbBuffer != ptDevData->m_dwVmCmdWriteBuf)
	{
		LOG_ERR("invalid parameter, i_pbBuffer(0x%016lx) is not mmaped before", (unsigned long)i_pbBuffer);
		return -EPERM;
	}

	if (i_dwCount > USB_CIMODULE_COMMAND_MAX_KMEM_SIZE)
	{
		LOG_ERR("command write size(%d) error, must be less %d", (unsigned int)i_dwCount, USB_CIMODULE_COMMAND_MAX_KMEM_SIZE);
		return -EPERM;
	}

	mutex_lock(&ptDevData->m_tCmdOutMutex);
	if (!ptDevData->m_ptCmdIntf)
	{
		LOG_ERR("command write failed, command interface is deregistered");
		mutex_unlock(&ptDevData->m_tCmdOutMutex);
		return -ENODEV;
	}

	ptDevData->m_dwCmdOutCondition = 0;
	if (usb_endpoint_is_int_out(ptDevData->m_ptCmdOut))
	{
		nPipe = usb_sndintpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptCmdOut->bEndpointAddress);
		usb_fill_int_urb(ptDevData->m_pbCmdOutUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbCmdOutBuf, i_dwCount, command_intf_write_complete, ptDevData, ptDevData->m_ptCmdOut->bInterval);
	}
	else
	{
		nPipe = usb_sndbulkpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptCmdOut->bEndpointAddress);
		usb_fill_bulk_urb(ptDevData->m_pbCmdOutUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbCmdOutBuf, i_dwCount, command_intf_write_complete, ptDevData);
	}
	nRetval = usb_submit_urb(ptDevData->m_pbCmdOutUrb, GFP_KERNEL);
	mutex_unlock(&ptDevData->m_tCmdOutMutex);
	if (nRetval)
	{
		LOG_ERR("submit urb %p failed, return = %d", ptDevData->m_pbCmdOutUrb, nRetval);
		return nRetval;
	}
	wait_event_interruptible(ptDevData->m_tCmdOutWait, ptDevData->m_dwCmdOutCondition);

	//write command error
	if (ptDevData->m_nCmdOutErr < 0)
	{
		nRetval = ptDevData->m_nCmdOutErr;
		LOG_ERR("command write error, return = %d", nRetval);
		return nRetval;
	}
	u32WriteSize = ptDevData->m_dwCmdWriteLen;

	if (i_dwCount % le16_to_cpu(ptDevData->m_ptCmdOut->wMaxPacketSize) == 0)
	{
		LOG_INFO("write a zero-length packet to cimodule");
		mutex_lock(&ptDevData->m_tCmdOutMutex);
		if (!ptDevData->m_ptCmdIntf)
		{
			LOG_ERR("submit zero-length packet command write failed, command interface is deregistered");
			mutex_unlock(&ptDevData->m_tCmdOutMutex);
			return -ENODEV;
		}
		ptDevData->m_dwCmdOutCondition = 0;
		if (usb_endpoint_is_int_out(ptDevData->m_ptCmdOut))
			usb_fill_int_urb(ptDevData->m_pbCmdOutUrb, ptDevData->m_ptUsbDev, nPipe, NULL, 0, command_intf_write_complete, ptDevData, ptDevData->m_ptCmdOut->bInterval);
		else
			usb_fill_bulk_urb(ptDevData->m_pbCmdOutUrb, ptDevData->m_ptUsbDev, nPipe, NULL, 0, command_intf_write_complete, ptDevData);
		nRetval = usb_submit_urb(ptDevData->m_pbCmdOutUrb, GFP_KERNEL);
		mutex_unlock(&ptDevData->m_tCmdOutMutex);
		if (nRetval)
		{
			LOG_ERR("submit zero-length packet urb %p failed, return = %d", ptDevData->m_pbCmdOutUrb, nRetval);
			return nRetval;
		}
		wait_event_interruptible(ptDevData->m_tCmdOutWait, ptDevData->m_dwCmdOutCondition);
		//write zero-length packet error
		if (ptDevData->m_nCmdOutErr < 0)
		{
			nRetval = ptDevData->m_nCmdOutErr;
			LOG_ERR("zero-length packet write error, return = %d", nRetval);
			return nRetval;
		}
	}

	LOG_INFO("write size: %08d", (unsigned int)u32WriteSize);
	return u32WriteSize;
}

static void command_intf_read_complete(struct urb *i_ptUrb)
{
	int nStatus;
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptUrb)
	{
		LOG_ERR("invalid parameter");
		return;
	}
	nStatus = i_ptUrb->status;
	ptDevData = i_ptUrb->context;

	if (nStatus)
	{
		ptDevData->m_dwCmdReadLen = 0;
		ptDevData->m_nCmdInErr = nStatus;
		LOG_ERR("urb %p read urb error, status %d", i_ptUrb, nStatus);
	}
	else
	{
		ptDevData->m_dwCmdReadLen = i_ptUrb->actual_length;
		ptDevData->m_nCmdInErr = 0;
	}

	ptDevData->m_dwCmdInCondition = 1;
	wake_up_interruptible(&ptDevData->m_tCmdInWait);
}

static ssize_t command_intf_read(struct file *i_pFile, char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos)
{
	struct usb_cimodule *ptDevData = NULL;
	size_t dwReadSize = 0;
	int nRetval = 0;
	int nPipe;

	LOG_INFO("enter");

	if (!i_pFile || !i_pbBuffer || !i_pPpos)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((unsigned long)i_pbBuffer != ptDevData->m_dwVmCmdReadBuf)
	{
		LOG_ERR("invalid parameter, i_pbBuffer(0x%016lx) is not mmaped before", (unsigned long)i_pbBuffer);
		return -EPERM;
	}

	if (i_dwCount > USB_CIMODULE_COMMAND_MAX_KMEM_SIZE)
	{
		LOG_ERR("command read size(%d) error, must be less %d", (unsigned int)i_dwCount, USB_CIMODULE_COMMAND_MAX_KMEM_SIZE);
		return -EPERM;
	}

	mutex_lock(&ptDevData->m_tCmdInMutex);
	if (!ptDevData->m_ptCmdIntf)
	{
		LOG_ERR("command read failed, command interface is deregistered");
		mutex_unlock(&ptDevData->m_tCmdInMutex);
		return -ENODEV;
	}
	//bulk transfer
	ptDevData->m_dwCmdInCondition = 0;
	if (usb_endpoint_is_int_in(ptDevData->m_ptCmdIn))
	{
		nPipe = usb_rcvintpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptCmdIn->bEndpointAddress);
		usb_fill_int_urb(ptDevData->m_pbCmdInUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbCmdInBuf, i_dwCount, command_intf_read_complete, ptDevData, ptDevData->m_ptCmdIn->bInterval);
	}
	else if (usb_endpoint_is_bulk_in(ptDevData->m_ptCmdIn))
	{
		nPipe = usb_rcvbulkpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptCmdIn->bEndpointAddress);
		usb_fill_bulk_urb(ptDevData->m_pbCmdInUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbCmdInBuf, i_dwCount, command_intf_read_complete, ptDevData);
	}
	nRetval = usb_submit_urb(ptDevData->m_pbCmdInUrb, GFP_KERNEL);
	mutex_unlock(&ptDevData->m_tCmdInMutex);
	if (nRetval)
	{
		LOG_ERR("submit urb %p failed, return = %d", ptDevData->m_pbCmdInUrb, nRetval);
		return nRetval;
	}
	wait_event_interruptible(ptDevData->m_tCmdInWait, ptDevData->m_dwCmdInCondition);

	//command read error
	if (ptDevData->m_nCmdInErr < 0)
	{
		nRetval = ptDevData->m_nCmdInErr;
		LOG_ERR("cmd read error, return = %d", nRetval);
		return nRetval;
	}

	dwReadSize = ptDevData->m_dwCmdReadLen;
	LOG_INFO("read size: %08d", (unsigned int)dwReadSize);

	return dwReadSize;
}

static int command_intf_release(struct inode *i_ptInode, struct file *i_pFile)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptInode || !i_pFile)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	mutex_lock(&ptDevData->m_tCmdOpenMutex);
	if (ptDevData->m_dwCmdOpenRef >= 1)
	{
		ptDevData->m_dwCmdOpenRef = ptDevData->m_dwCmdOpenRef - 1;
		ptDevData->m_dwVmCmdReadBuf = 0;
		ptDevData->m_dwVmCmdWriteBuf = 0;
	}
	else
	{
		LOG_ERR("command interface fd is released before");
		mutex_unlock(&ptDevData->m_tCmdOpenMutex);
		return -ENODEV;
	}
	mutex_unlock(&ptDevData->m_tCmdOpenMutex);

	if (atomic_read(&ptDevData->m_pbCmdInUrb->use_count))
	{
		LOG_INFO("unlink the command read urb");
		usb_unlink_urb(ptDevData->m_pbCmdInUrb);
	}
	if (atomic_read(&ptDevData->m_pbCmdOutUrb->use_count))
	{
		LOG_INFO("unlink the command write urb");
		usb_unlink_urb(ptDevData->m_pbCmdOutUrb);
	}

	kref_put(&ptDevData->m_tKref, cimodule_delete);
	LOG_INFO("command interface release succeessfully");
	return 0;
}

static int media_intf_open(struct inode *i_ptInode, struct file *i_pFile)
{
	struct usb_cimodule *ptDevData = NULL;
	struct usb_interface *ptIntf = NULL;
	int nSubminor;

	LOG_INFO("enter");

	if (!i_ptInode || !i_pFile)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	nSubminor = iminor(i_ptInode);

	ptIntf = usb_find_interface(&cimodule_driver, nSubminor);
	if (!ptIntf)
	{
		LOG_ERR("can not find device for minor %d", nSubminor);
		return -ENODEV;
	}

	ptDevData = usb_get_intfdata(ptIntf);
	if (!ptDevData)
	{
		LOG_ERR("can not get intfdata");
		return -ENODEV;
	}

	if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_READ_ACCESS_MODE)
	{
		mutex_lock(&ptDevData->m_tMediaReadOpenMutex);
		if (ptDevData->m_dwMediaReadOpenRef >= 1)
		{
			LOG_ERR("open failed: media interface read fd is opened before");
			mutex_unlock(&ptDevData->m_tMediaReadOpenMutex);
			return -EPERM;
		}
		else
		{
			ptDevData->m_dwMediaReadOpenRef = ptDevData->m_dwMediaReadOpenRef + 1;
			LOG_INFO("media interface read fd open succeessfully");
		}
		mutex_unlock(&ptDevData->m_tMediaReadOpenMutex);
	}
	else if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_WRITE_ACCESS_MODE)
	{
		mutex_lock(&ptDevData->m_tMediaWriteOpenMutex);
		if (ptDevData->m_dwMediaWriteOpenRef >= 1)
		{
			LOG_ERR("open failed: media interface write fd is opened before");
			mutex_unlock(&ptDevData->m_tMediaWriteOpenMutex);
			return -EPERM;
		}
		else
		{
			ptDevData->m_dwMediaWriteOpenRef = ptDevData->m_dwMediaWriteOpenRef + 1;
			LOG_INFO("media interface write fd open succeessfully");
		}
		mutex_unlock(&ptDevData->m_tMediaWriteOpenMutex);
	}
	else
	{
		LOG_ERR("open failed: can't support this flags: 0x%x", i_pFile->f_flags);
		return -EPERM;
	}

	kref_get(&ptDevData->m_tKref);

	//save our object in the file's private structure
	i_pFile->private_data = ptDevData;
	return 0;
}

static void media_intf_write_complete(struct urb *i_ptUrb)
{
	int nStatus;
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptUrb)
	{
		LOG_ERR("invalid parameter");
		return;
	}

	nStatus = i_ptUrb->status;
	ptDevData = i_ptUrb->context;

	if (nStatus)
	{
		ptDevData->m_dwMediaWriteLen = 0;
		ptDevData->m_nMediaOutErr = nStatus;
		LOG_ERR("urb %p write urb error, status %d", i_ptUrb, nStatus);
	}
	else
	{
		ptDevData->m_dwMediaWriteLen = i_ptUrb->actual_length;
		ptDevData->m_nMediaOutErr = 0;
	}

	ptDevData->m_dwMediaOutCondition = 1;
	wake_up_interruptible(&ptDevData->m_tMediaOutWait);
}

static ssize_t media_intf_write(struct file *i_pFile, const char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos)
{
	struct usb_cimodule *ptDevData = NULL;
	size_t u32WriteSize;
	int nRetval = -EINVAL;
	int nPipe;

	//LOG_INFO("enter");

	if (!i_pFile || !i_pbBuffer || !i_pPpos)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	if ((i_pFile->f_flags & 0x03) != MEDIA_INTF_WRITE_ACCESS_MODE)
	{
		LOG_ERR("media write failed, just media write fd support this operation");
		return -EPERM;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((unsigned long)i_pbBuffer != ptDevData->m_dwVmMediaWriteBuf)
	{
		LOG_ERR("invalid parameter, i_pbBuffer(0x%016lx) is not mmaped before", (unsigned long)i_pbBuffer);
		return -EPERM;
	}

	if (i_dwCount > USB_CIMODULE_MEDIA_MAX_KMEM_SIZE)
	{
		LOG_ERR("media write size(%d) error, must be less %d", (unsigned int)i_dwCount, USB_CIMODULE_MEDIA_MAX_KMEM_SIZE);
		return -EPERM;
	}

	mutex_lock(&ptDevData->m_tMediaOutMutex);
	if (!ptDevData->m_ptMediaIntf)
	{
		LOG_ERR("media write failed, media interface is deregistered");
		mutex_unlock(&ptDevData->m_tMediaOutMutex);
		return -ENODEV;
	}
	//bulk transfer
	ptDevData->m_dwMediaOutCondition = 0;
	nPipe = usb_sndbulkpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptMediaOut->bEndpointAddress);
	usb_fill_bulk_urb(ptDevData->m_pbMediaOutUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbMediaOutBuf, i_dwCount, media_intf_write_complete, ptDevData);
	nRetval = usb_submit_urb(ptDevData->m_pbMediaOutUrb, GFP_KERNEL);
	mutex_unlock(&ptDevData->m_tMediaOutMutex);
	if (nRetval)
	{
		LOG_ERR("submit urb %p failed, return = %d", ptDevData->m_pbMediaOutUrb, nRetval);
		return nRetval;
	}
	wait_event_interruptible(ptDevData->m_tMediaOutWait, ptDevData->m_dwMediaOutCondition);

	//write media error
	if (ptDevData->m_nMediaOutErr < 0)
	{
		nRetval = ptDevData->m_nMediaOutErr;
		LOG_ERR("media write error, return = %d", nRetval);
		return nRetval;
	}
	u32WriteSize = ptDevData->m_dwMediaWriteLen;

	if (i_dwCount % le16_to_cpu(ptDevData->m_ptMediaOut->wMaxPacketSize) == 0)
	{
		//LOG_INFO("write a zero-length packet to cimodule");
		mutex_lock(&ptDevData->m_tMediaOutMutex);
		if (!ptDevData->m_ptMediaIntf)
		{
			LOG_ERR("submit zero-length packet media write failed, media interface is deregistered");
			mutex_unlock(&ptDevData->m_tMediaOutMutex);
			return -ENODEV;
		}
		ptDevData->m_dwMediaOutCondition = 0;
		usb_fill_bulk_urb(ptDevData->m_pbMediaOutUrb, ptDevData->m_ptUsbDev, nPipe, NULL, 0, media_intf_write_complete, ptDevData);
		nRetval = usb_submit_urb(ptDevData->m_pbMediaOutUrb, GFP_KERNEL);
		mutex_unlock(&ptDevData->m_tMediaOutMutex);
		if (nRetval)
		{
			LOG_ERR("submit zero-length packet urb %p failed, return = %d", ptDevData->m_pbMediaOutUrb, nRetval);
			return nRetval;
		}
		wait_event_interruptible(ptDevData->m_tMediaOutWait, ptDevData->m_dwMediaOutCondition);

		if (ptDevData->m_nMediaOutErr < 0)
		{
			nRetval = ptDevData->m_nMediaOutErr;
			LOG_ERR("zero-length packet media write error, return = %d", nRetval);
			return nRetval;
		}
	}
	//LOG_INFO("write size: %08d", (unsigned int)u32WriteSize);
	return u32WriteSize;
}

static void media_intf_read_complete(struct urb *i_ptUrb)
{
	int nStatus;
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptUrb)
	{
		LOG_ERR("invalid parameter");
		return;
	}
	nStatus = i_ptUrb->status;
	ptDevData = i_ptUrb->context;

	if (nStatus)
	{
		ptDevData->m_dwMediaReadLen = 0;
		ptDevData->m_nMediaInErr = nStatus;
		LOG_ERR("urb %p read urb error, status %d", i_ptUrb, nStatus);
	}
	else
	{
		ptDevData->m_dwMediaReadLen = i_ptUrb->actual_length;
		ptDevData->m_nMediaInErr = 0;
	}

	ptDevData->m_dwMediaInCondition = 1;
	wake_up_interruptible(&ptDevData->m_tMediaInWait);
}

static ssize_t media_intf_read(struct file *i_pFile, char *i_pbBuffer, size_t i_dwCount, loff_t *i_pPpos)
{
	struct usb_cimodule *ptDevData = NULL;
	size_t dwReadSize = 0;
	int nRetval = -ENODEV;
	int nPipe;

	//LOG_INFO("enter");

	if (!i_pFile || !i_pbBuffer || !i_pPpos)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	if ((i_pFile->f_flags & 0x03) != MEDIA_INTF_READ_ACCESS_MODE)
	{
		LOG_ERR("media read failed, just media read fd support this operation");
		return -EPERM;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((unsigned long)i_pbBuffer != ptDevData->m_dwVmMediaReadBuf)
	{
		LOG_ERR("invalid parameter, i_pbBuffer(0x%016lx) is not mmaped before", (unsigned long)i_pbBuffer);
		return -EPERM;
	}

	if (i_dwCount > USB_CIMODULE_MEDIA_MAX_KMEM_SIZE)
	{
		LOG_ERR("media read size(%d) error, must be less %d", (unsigned int)i_dwCount, USB_CIMODULE_MEDIA_MAX_KMEM_SIZE);
		return -EPERM;
	}

	mutex_lock(&ptDevData->m_tMediaInMutex);
	if (!ptDevData->m_ptMediaIntf)
	{
		LOG_ERR("media read failed, media interface is deregistered");
		mutex_unlock(&ptDevData->m_tMediaInMutex);
		return -ENODEV;
	}
	//bulk transfer
	ptDevData->m_dwMediaInCondition = 0;
	nPipe = usb_rcvbulkpipe(ptDevData->m_ptUsbDev, ptDevData->m_ptMediaIn->bEndpointAddress);
	usb_fill_bulk_urb(ptDevData->m_pbMediaInUrb, ptDevData->m_ptUsbDev, nPipe, ptDevData->m_pbMediaInBuf, i_dwCount, media_intf_read_complete, ptDevData);
	nRetval = usb_submit_urb(ptDevData->m_pbMediaInUrb, GFP_KERNEL);
	mutex_unlock(&ptDevData->m_tMediaInMutex);
	if (nRetval)
	{
		LOG_ERR("submit urb %p failed, return = %d", ptDevData->m_pbMediaInUrb, nRetval);
		return nRetval;
	}
	wait_event_interruptible(ptDevData->m_tMediaInWait, ptDevData->m_dwMediaInCondition);

	//media read error
	if (ptDevData->m_nMediaInErr < 0)
	{
		nRetval = ptDevData->m_nMediaInErr;
		LOG_ERR("media read error, return = %d", nRetval);
		return nRetval;
	}

	dwReadSize = ptDevData->m_dwMediaReadLen;
	//LOG_INFO("read size: %08d", (unsigned int)dwReadSize);
	return dwReadSize;
}

static int media_intf_release(struct inode *i_ptInode, struct file *i_pFile)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_pFile || !i_pFile)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_READ_ACCESS_MODE)
	{
		if (atomic_read(&ptDevData->m_pbMediaInUrb->use_count))
		{
			LOG_INFO("unlink the media read urb");
			usb_unlink_urb(ptDevData->m_pbMediaInUrb);
		}
		mutex_lock(&ptDevData->m_tMediaReadOpenMutex);
		if (ptDevData->m_dwMediaReadOpenRef >= 1)
		{
			ptDevData->m_dwMediaReadOpenRef = ptDevData->m_dwMediaReadOpenRef - 1;
			ptDevData->m_dwVmMediaReadBuf = 0;
			LOG_INFO("media interface read fd release succeessfully");
		}
		else
		{
			LOG_ERR("media interface read fd is released before");
			mutex_unlock(&ptDevData->m_tMediaReadOpenMutex);
			return -ENODEV;
		}
		mutex_unlock(&ptDevData->m_tMediaReadOpenMutex);
	}
	else if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_WRITE_ACCESS_MODE)
	{
		if (atomic_read(&ptDevData->m_pbMediaOutUrb->use_count))
		{
			LOG_INFO("unlink the media write urb");
			usb_unlink_urb(ptDevData->m_pbMediaOutUrb);
		}
		mutex_lock(&ptDevData->m_tMediaWriteOpenMutex);
		if (ptDevData->m_dwMediaWriteOpenRef >= 1)
		{
			ptDevData->m_dwMediaWriteOpenRef = ptDevData->m_dwMediaWriteOpenRef - 1;
			ptDevData->m_dwVmMediaWriteBuf = 0;
			LOG_INFO("media interface write fd release succeessfully");
		}
		else
		{
			LOG_ERR("media interface write fd is released before");
			mutex_unlock(&ptDevData->m_tMediaWriteOpenMutex);
			return -ENODEV;
		}
		mutex_unlock(&ptDevData->m_tMediaWriteOpenMutex);
	}
	else
	{
		LOG_ERR("release failed: can't support this flags: %d", i_pFile->f_flags);
		return -EPERM;
	}

	kref_put(&ptDevData->m_tKref, cimodule_delete);
	return 0;
}

static int media_intf_mmap(struct file *i_pFile, struct vm_area_struct *i_ptVma)
{
	struct usb_cimodule *ptDevData = NULL;
	unsigned long dwStart = 0;
	unsigned long dwSize = 0;

	//LOG_INFO("enter");
	if (!i_pFile || !i_ptVma)
	{
		LOG_ERR("invalid parameter");
		return -EINVAL;
	}

	dwStart = i_ptVma->vm_start;
	dwSize = i_ptVma->vm_end - i_ptVma->vm_start;

	ptDevData = i_pFile->private_data;
	if (!ptDevData)
	{
		LOG_ERR("private data is null");
		return -ENODEV;
	}

	if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_READ_ACCESS_MODE)
	{
		if (remap_pfn_range(i_ptVma, dwStart, page_to_pfn(gs_ptMediaReadShmPage + i_ptVma->vm_pgoff), dwSize, i_ptVma->vm_page_prot))
		{
			LOG_ERR("remap read buffer failed");
			return -EAGAIN;
		}
		LOG_ERR("virtaul memory read buffer: 0x%016lx", i_ptVma->vm_start);
		if (ptDevData->m_dwVmMediaReadBuf == 0)
			ptDevData->m_dwVmMediaReadBuf = i_ptVma->vm_start;
		LOG_INFO("media read buffer mmap succeessfully");
	}
	else if ((i_pFile->f_flags & 0x03) == MEDIA_INTF_WRITE_ACCESS_MODE)
	{
		if (remap_pfn_range(i_ptVma, dwStart, page_to_pfn(gs_ptMediaWriteShmPage + i_ptVma->vm_pgoff), dwSize, i_ptVma->vm_page_prot))
		{
			LOG_ERR("remap write buffer failed");
			return -EAGAIN;
		}
		LOG_ERR("virtaul memory read buffer: 0x%016lx", i_ptVma->vm_start);
		if (ptDevData->m_dwVmMediaWriteBuf == 0)
			ptDevData->m_dwVmMediaWriteBuf = i_ptVma->vm_start;
		LOG_INFO("media write buffer mmap succeessfully");
	}
	else
	{
		LOG_ERR("mmap failed: can't support this flags: 0x%x", i_pFile->f_flags);
		return -EPERM;
	}

	return 0;
}

static int cimodule_probe(struct usb_interface *i_ptIntf, const struct usb_device_id *i_pId)
{
	int nRetval = -ENOMEM;
	struct usb_endpoint_descriptor *ptCmdInDesc=NULL, *ptCmdOutDesc=NULL;
	struct usb_endpoint_descriptor *ptMdiInDesc=NULL, *ptMdiOutDesc=NULL;
	struct usb_device *ptUsbDev = NULL;
	struct usb_interface_assoc_descriptor *ptAssocDesc = NULL;
	static struct usb_cimodule *s_ptDevData = NULL;
	unsigned char bIntfNum;
	unsigned char * pbExtraTemp = NULL;
	int nExtraSize = 0;
	unsigned char bDescriptorLen = 0;
	unsigned char bDescriptorType = 0;

	LOG_INFO("entry");

	if (!i_ptIntf || !i_pId)
	{
		LOG_ERR("invalid parameter");
		nRetval = -EINVAL;
		goto error;
	}

	ptUsbDev = interface_to_usbdev(i_ptIntf);
	bIntfNum = i_ptIntf->cur_altsetting->desc.bInterfaceNumber;
	LOG_INFO("matched module parameter, vendorID=0x%04x productID=0x%04x",
	le16_to_cpu(ptUsbDev->descriptor.idVendor),
	le16_to_cpu(ptUsbDev->descriptor.idProduct));

	ptAssocDesc = ptUsbDev->actconfig->intf_assoc[0];
	if (!ptAssocDesc)
	{
		LOG_CRIT("interface association descriptor is null");
		nRetval = -ENODEV;
		goto error;
	}

	if (ptAssocDesc->bFunctionClass != IAD_FUNCTION_CLASS
		|| ptAssocDesc->bFunctionSubClass != IAD_FUNCTION_SUBCLASS
		|| ptAssocDesc->bFunctionProtocol != IAD_FUNCTION_PROTOCOL)
	{
		LOG_CRIT("mismatched function info in IAD");
		nRetval = -ENODEV;
		goto error;
	}

	if (bIntfNum < ptAssocDesc->bFirstInterface
	   || bIntfNum > ptAssocDesc->bFirstInterface + ptAssocDesc->bInterfaceCount)
	{
		LOG_CRIT("mismatched interface number in IAD");
		nRetval = -ENODEV;
		goto error;
	}

	if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == COMMAND_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == COMMAND_INTF_SUBCLASS
 		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == COMMAND_INTF_POTOCOL)
	{
		if (i_ptIntf->cur_altsetting->desc.bNumEndpoints < 2)
		{
			LOG_CRIT("not enough endpoints in command interface");
			goto error;
		}

		ptCmdOutDesc = &i_ptIntf->cur_altsetting->endpoint[0].desc;
		ptCmdInDesc = &i_ptIntf->cur_altsetting->endpoint[1].desc;
		if ((!usb_endpoint_is_bulk_out(ptCmdOutDesc) && !usb_endpoint_is_int_out(ptCmdOutDesc))
			|| (!usb_endpoint_is_bulk_in(ptCmdInDesc) && !usb_endpoint_is_int_in(ptCmdInDesc)))
		{
			LOG_CRIT("mismatched endpoint type in command interface");
			goto error;
		}

		//malloc memory for interface data
		s_ptDevData = kzalloc(sizeof(struct usb_cimodule), GFP_KERNEL);
		if (!s_ptDevData)
		{
			LOG_CRIT("memory alloc failed for interface data");
			nRetval = -ENOMEM;
			goto error;
		}

		LOG_CRIT("usb ci module driver version: %d.%d.%d.%d",
			(USB_CI_MODULE_DRIVER_VERSION & 0xFF000000) >> 24,
			(USB_CI_MODULE_DRIVER_VERSION & 0x00FF0000) >> 16,
			(USB_CI_MODULE_DRIVER_VERSION & 0x0000FF00) >> 8,
			USB_CI_MODULE_DRIVER_VERSION & 0x000000FF);

		//init the command interface part of interface data
		s_ptDevData->m_ptUsbDev = usb_get_dev(ptUsbDev);
		s_ptDevData->m_ptCmdIntf = i_ptIntf;
		s_ptDevData->m_ptCmdOut = ptCmdOutDesc;
		s_ptDevData->m_ptCmdIn = ptCmdInDesc;
		s_ptDevData->m_dwCmdOpenRef = 0;
		mutex_init(&s_ptDevData->m_tCmdOpenMutex);
		s_ptDevData->m_tUsbCiModuleInfo.m_wVendorId = le16_to_cpu(ptUsbDev->descriptor.idVendor);
		s_ptDevData->m_tUsbCiModuleInfo.m_wProductId = le16_to_cpu(ptUsbDev->descriptor.idProduct);
		kref_init(&s_ptDevData->m_tKref);
		mutex_init(&s_ptDevData->m_tCmdInMutex);
		mutex_init(&s_ptDevData->m_tCmdOutMutex);
		init_waitqueue_head(&s_ptDevData->m_tCmdInWait);
		init_waitqueue_head(&s_ptDevData->m_tCmdOutWait);

		s_ptDevData->m_pbCmdInUrb = usb_alloc_urb(0, GFP_KERNEL);
		if (!s_ptDevData->m_pbCmdInUrb)
		{
			LOG_CRIT("command read urb alloc failed");
			nRetval = -ENOMEM;
			goto error;
		}
		s_ptDevData->m_pbCmdOutUrb = usb_alloc_urb(0, GFP_KERNEL);
		if (!s_ptDevData->m_pbCmdOutUrb)
		{
			LOG_CRIT("command write urb alloc failed");
			nRetval = -ENOMEM;
			goto error;
		}

		if (!gs_ptCmdShmPage)
		{
			LOG_CRIT("cmd shm_page is not alloced before");
			nRetval = -ENODEV;
			goto error;
		}
		s_ptDevData->m_pbCmdInBuf = (unsigned char*)page_address(gs_ptCmdShmPage);
		s_ptDevData->m_pbCmdOutBuf = s_ptDevData->m_pbCmdInBuf + USB_CIMODULE_COMMAND_MAX_MALLOC_MEM / 2;

		//parse the compatibility descriptor
		if (ptUsbDev->actconfig->extralen < 2 || !ptUsbDev->actconfig->extra)
		{
			LOG_CRIT("parse the compatibility descriptor failed, extra data is not match");
			nRetval = -ENODEV;
			goto error;
		}
		pbExtraTemp = ptUsbDev->actconfig->extra;
		nExtraSize = ptUsbDev->actconfig->extralen;
		while(nExtraSize > 0)
		{
			bDescriptorLen = *pbExtraTemp;
			bDescriptorType = *(pbExtraTemp + 1);
			if (bDescriptorLen > nExtraSize || bDescriptorLen < 2)
			{
				LOG_CRIT("parse the compatibility descriptor failed, extra data has an invalid descriptor");
				goto error;
			}
			if (0x41 == bDescriptorType && 6 == bDescriptorLen) //0x41: compatibility descriptor type, 6:compatibility descriptor length
			{
				s_ptDevData->m_tUsbCiModuleInfo.m_dwCiCompatibility = *(pbExtraTemp + 2) << 24 | *(pbExtraTemp + 3) << 16 | *(pbExtraTemp + 4) << 8 | *(pbExtraTemp + 5);
				LOG_INFO("ci compatibility: 0x%08x", s_ptDevData->m_tUsbCiModuleInfo.m_dwCiCompatibility);
				if ((s_ptDevData->m_tUsbCiModuleInfo.m_dwCiCompatibility & 0x07) != 2)	// 2:architecture version 2
				{
					LOG_CRIT("this driver supports only ARCH v2(value:2) but does no support ARCH value: %d", s_ptDevData->m_tUsbCiModuleInfo.m_dwCiCompatibility & 0x07);
					goto error;
				}
				break;
			}
			else
			{
				LOG_INFO("extra data contains an %s descriptor of type 0x%02X, skipping", bDescriptorType == 0x0B ? "interface association" : "unexpected", bDescriptorType);
				pbExtraTemp += bDescriptorLen;
				nExtraSize -= bDescriptorLen;
			}
		}

		if (!s_ptDevData->m_tUsbCiModuleInfo.m_dwCiCompatibility)
		{
			LOG_CRIT("parse compatibility descriptor failed, not found this descriptor");
			goto error;
		}

		usb_set_intfdata(i_ptIntf, s_ptDevData);
		//command interface registration
		nRetval = usb_register_dev(i_ptIntf, &cimodule_commandclass);
		if (nRetval)
		{
			LOG_CRIT("get minor failed for command interface");
			usb_set_intfdata(i_ptIntf, NULL);
			nRetval = -ENODEV;
			goto error;
		}
		s_ptDevData->m_isCmdIntfReg = TRUE;
		LOG_CRIT("command interface register succeessfully");
	}
	else if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == MEDIA_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == MEDIA_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == MEDIA_INTF_POTOCOL)
	{
		if (!s_ptDevData)
		{
			LOG_CRIT("previous interface init failed, skip the second interface init");
			nRetval = -ENODEV;
			goto error;
		}

		if (i_ptIntf->cur_altsetting->desc.bNumEndpoints < 2)
		{
			LOG_CRIT("not enough endpoints in media interface");
			nRetval = -ENODEV;
			goto error;
		}

		ptMdiOutDesc = &i_ptIntf->cur_altsetting->endpoint[0].desc;
		ptMdiInDesc = &i_ptIntf->cur_altsetting->endpoint[1].desc;
		if (!usb_endpoint_is_bulk_out(ptMdiOutDesc) || !usb_endpoint_is_bulk_in(ptMdiInDesc))
		{
			LOG_CRIT("mismatched endpoint type in media interface");
			nRetval = -ENODEV;
			goto error;
		}

		//init the media interface part of interface data
		kref_get(&s_ptDevData->m_tKref);
		s_ptDevData->m_ptMediaIntf = i_ptIntf;
		s_ptDevData->m_ptMediaOut = ptMdiOutDesc;
		s_ptDevData->m_ptMediaIn = ptMdiInDesc;
		s_ptDevData->m_dwMediaReadOpenRef = 0;
		s_ptDevData->m_dwMediaWriteOpenRef = 0;
		mutex_init(&s_ptDevData->m_tMediaReadOpenMutex);
		mutex_init(&s_ptDevData->m_tMediaWriteOpenMutex);
		mutex_init(&s_ptDevData->m_tMediaInMutex);
		mutex_init(&s_ptDevData->m_tMediaOutMutex);
		init_waitqueue_head(&s_ptDevData->m_tMediaInWait);
		init_waitqueue_head(&s_ptDevData->m_tMediaOutWait);

		s_ptDevData->m_pbMediaInUrb = usb_alloc_urb(0, GFP_KERNEL);
		if (!s_ptDevData->m_pbMediaInUrb)
		{
			LOG_CRIT("media read urb alloc failed");
			nRetval = -ENOMEM;
			goto error;
		}

		s_ptDevData->m_pbMediaOutUrb = usb_alloc_urb(0, GFP_KERNEL);
		if (!s_ptDevData->m_pbMediaOutUrb)
		{
			LOG_CRIT("media write urb alloc failed");
			nRetval = -ENOMEM;
			goto error;
		}

		if (!gs_ptMediaReadShmPage)
		{
			LOG_CRIT("media read shm_page is not alloced before");
			nRetval = -ENODEV;
			goto error;
		}
		s_ptDevData->m_pbMediaInBuf = (unsigned char*)page_address(gs_ptMediaReadShmPage);

		if (!gs_ptMediaWriteShmPage)
		{
			LOG_CRIT("media write shm_page is not alloced before");
			nRetval = -ENODEV;
			goto error;
		}
		s_ptDevData->m_pbMediaOutBuf = (unsigned char*)page_address(gs_ptMediaWriteShmPage);

		usb_set_intfdata(i_ptIntf, s_ptDevData);
		//register media interface
		nRetval = usb_register_dev(i_ptIntf, &cimodule_mediaclass);
		if (nRetval)
		{
			LOG_CRIT("get minor failed media interface");
			usb_set_intfdata(i_ptIntf, NULL);
			goto error;
		}
		s_ptDevData->m_isMediaIntfReg = TRUE;

		LOG_CRIT("media interface register succeessfully");

		s_ptDevData = NULL;
	}
	else
	{
		LOG_CRIT("unknown interface, class=%d, subclass=%d, protocol=%d",
			i_ptIntf->cur_altsetting->desc.bInterfaceClass,
			i_ptIntf->cur_altsetting->desc.bInterfaceSubClass,
			i_ptIntf->cur_altsetting->desc.bInterfaceProtocol);
		nRetval = -ENODEV;
		goto error;
	}

	return 0;

error:
	if (s_ptDevData && s_ptDevData->m_isCmdIntfReg)
	{
		mutex_lock(&s_ptDevData->m_tCmdInMutex);
		mutex_lock(&s_ptDevData->m_tCmdOutMutex);
		usb_set_intfdata(s_ptDevData->m_ptCmdIntf, NULL);
		usb_deregister_dev(s_ptDevData->m_ptCmdIntf, &cimodule_commandclass);
		s_ptDevData->m_ptCmdIntf = NULL;
		LOG_CRIT("command interface release succeessfully");
		mutex_unlock(&s_ptDevData->m_tCmdOutMutex);
		mutex_unlock(&s_ptDevData->m_tCmdInMutex);
	}
	if (s_ptDevData && s_ptDevData->m_isMediaIntfReg)
	{
		mutex_lock(&s_ptDevData->m_tMediaInMutex);
		mutex_lock(&s_ptDevData->m_tMediaOutMutex);
		usb_set_intfdata(s_ptDevData->m_ptMediaIntf, NULL);
		usb_deregister_dev(s_ptDevData->m_ptMediaIntf, &cimodule_mediaclass);
		s_ptDevData->m_ptMediaIntf = NULL;
		LOG_CRIT("media interface release succeessfully");
		mutex_unlock(&s_ptDevData->m_tMediaOutMutex);
		mutex_unlock(&s_ptDevData->m_tMediaInMutex);
	}

	if (s_ptDevData)
	{
		kref_put(&s_ptDevData->m_tKref, cimodule_delete);
		s_ptDevData = NULL;
	}

	return nRetval;
}

static void cimodule_disconnect(struct usb_interface *i_ptIntf)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptIntf)
	{
		LOG_ERR("invalid parameter");
		return;
	}

	ptDevData = usb_get_intfdata(i_ptIntf);
	if (!ptDevData)
	{
		LOG_ERR("failed to get intfdata");
		return;
	}

	if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == COMMAND_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == COMMAND_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == COMMAND_INTF_POTOCOL)
	{
		if (ptDevData->m_isCmdIntfReg)
		{
			mutex_lock(&ptDevData->m_tCmdInMutex);
			mutex_lock(&ptDevData->m_tCmdOutMutex);
			usb_set_intfdata(ptDevData->m_ptCmdIntf, NULL);
			usb_deregister_dev(ptDevData->m_ptCmdIntf, &cimodule_commandclass);
			ptDevData->m_ptCmdIntf = NULL;
			LOG_CRIT("command interface deregister succeessfully");
			mutex_unlock(&ptDevData->m_tCmdOutMutex);
			mutex_unlock(&ptDevData->m_tCmdInMutex);
		}
	}
	else if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == MEDIA_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == MEDIA_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == MEDIA_INTF_POTOCOL)
	{
	 	if (ptDevData->m_isMediaIntfReg)
	 	{
			mutex_lock(&ptDevData->m_tMediaInMutex);
			mutex_lock(&ptDevData->m_tMediaOutMutex);
			usb_set_intfdata(ptDevData->m_ptMediaIntf, NULL);
			usb_deregister_dev(ptDevData->m_ptMediaIntf, &cimodule_mediaclass);
			ptDevData->m_ptMediaIntf = NULL;
			LOG_CRIT("media interface deregister succeessfully");
			mutex_unlock(&ptDevData->m_tMediaOutMutex);
			mutex_unlock(&ptDevData->m_tMediaInMutex);
		}
	}

	kref_put(&ptDevData->m_tKref, cimodule_delete);
}

static int cimodule_pre_reset(struct usb_interface *i_ptIntf)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptIntf)
	{
		LOG_ERR("invalid parameter");
		return -ENODEV;
	}

	ptDevData = usb_get_intfdata(i_ptIntf);
	if (!ptDevData)
	{
		LOG_ERR("failed to get intfdata");
		return -ENODEV;
	}

	//Make sure no active URB during the reset
	if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == COMMAND_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == COMMAND_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == COMMAND_INTF_POTOCOL)
	{
		if (ptDevData->m_isCmdIntfReg)
		{
			mutex_lock(&ptDevData->m_tCmdInMutex);
			mutex_lock(&ptDevData->m_tCmdOutMutex);
			if (atomic_read(&ptDevData->m_pbCmdInUrb->use_count))
			{
				LOG_INFO("unlink the command read urb");
				usb_unlink_urb(ptDevData->m_pbCmdInUrb);
			}
			if (atomic_read(&ptDevData->m_pbCmdOutUrb->use_count))
			{
				LOG_INFO("unlink the command write urb");
				usb_unlink_urb(ptDevData->m_pbCmdOutUrb);
			}
			LOG_INFO("cmd interface pre reset success");
		}
	}
	else if (ptDevData->m_isMediaIntfReg
		&& i_ptIntf->cur_altsetting->desc.bInterfaceClass == MEDIA_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == MEDIA_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == MEDIA_INTF_POTOCOL)
	{
		if (ptDevData->m_isMediaIntfReg)
		{
			mutex_lock(&ptDevData->m_tMediaInMutex);
			mutex_lock(&ptDevData->m_tMediaOutMutex);
			if (atomic_read(&ptDevData->m_pbMediaInUrb->use_count))
			{
				LOG_INFO("unlink the media read urb");
				usb_unlink_urb(ptDevData->m_pbMediaInUrb);
			}
			if (atomic_read(&ptDevData->m_pbMediaOutUrb->use_count))
			{
				LOG_INFO("unlink the media write urb");
				usb_unlink_urb(ptDevData->m_pbMediaOutUrb);
			}
			LOG_INFO("media interface pre reset success");
		}
	}

	return 0;
}

static int cimodule_post_reset(struct usb_interface *i_ptIntf)
{
	struct usb_cimodule *ptDevData = NULL;

	if (!i_ptIntf)
	{
		LOG_ERR("invalid parameter");
		return -ENODEV;
	}

	ptDevData = usb_get_intfdata(i_ptIntf);
	if (!ptDevData)
	{
		LOG_ERR("failed to get intfdata");
		return -ENODEV;
	}

	if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == COMMAND_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == COMMAND_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == COMMAND_INTF_POTOCOL)
	{
		if (ptDevData->m_isCmdIntfReg)
		{
			mutex_unlock(&ptDevData->m_tCmdInMutex);
			mutex_unlock(&ptDevData->m_tCmdOutMutex);
			LOG_INFO("cmd interface post reset success");
		}
	}
	else if (i_ptIntf->cur_altsetting->desc.bInterfaceClass == MEDIA_INTF_CLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceSubClass == MEDIA_INTF_SUBCLASS
		&& i_ptIntf->cur_altsetting->desc.bInterfaceProtocol == MEDIA_INTF_POTOCOL)
	{
		if (ptDevData->m_isMediaIntfReg)
		{
			mutex_unlock(&ptDevData->m_tMediaInMutex);
			mutex_unlock(&ptDevData->m_tMediaOutMutex);
			LOG_INFO("media interface post reset success");
		}
	}

	return 0;
}

static struct usb_driver cimodule_driver = {
	.name = "usbcimodule",
	.id_table = cimodule_table,
	.probe = cimodule_probe,
	.disconnect = cimodule_disconnect,
	.pre_reset = cimodule_pre_reset,
	.post_reset = cimodule_post_reset,
};

static int __init usb_cimodule_init(void)
{
	gs_ptCmdShmPage = alloc_pages(GFP_KERNEL, USB_CIMODULE_COMMAND_MAX_PAGE_ORDER);
	if (!gs_ptCmdShmPage)
	{
		LOG_CRIT("shm_page alloc failed for command interface");
		return -ENOMEM;
	}

	gs_ptMediaReadShmPage = alloc_pages(GFP_KERNEL, USB_CIMODULE_MEDIA_MAX_PAGE_ORDER);
	if (!gs_ptMediaReadShmPage)
	{
		LOG_CRIT("shm_page alloc failed for media read");
		return -ENOMEM;
	}

	gs_ptMediaWriteShmPage = alloc_pages(GFP_KERNEL, USB_CIMODULE_MEDIA_MAX_PAGE_ORDER);
	if (!gs_ptMediaWriteShmPage)
	{
		LOG_CRIT("shm_page alloc failed for media write");
		return -ENOMEM;
	}
	LOG_INFO("shm_pages alloc success");

	return usb_register(&cimodule_driver);
}
module_init(usb_cimodule_init);

static void __exit usb_cimodule_exit(void)
{
	if (gs_ptCmdShmPage)
	{
		free_pages((unsigned long)page_address(gs_ptCmdShmPage), USB_CIMODULE_COMMAND_MAX_PAGE_ORDER);
		gs_ptCmdShmPage = NULL;
	}
	if (gs_ptMediaReadShmPage)
	{
		free_pages((unsigned long)page_address(gs_ptMediaReadShmPage), USB_CIMODULE_MEDIA_MAX_PAGE_ORDER);
		gs_ptMediaReadShmPage = NULL;
	}
	if (gs_ptMediaWriteShmPage)
	{
		free_pages((unsigned long)page_address(gs_ptMediaWriteShmPage), USB_CIMODULE_MEDIA_MAX_PAGE_ORDER);
		gs_ptMediaWriteShmPage = NULL;
	}
	LOG_INFO("shm_pages free success");

	usb_deregister(&cimodule_driver);
}
module_exit(usb_cimodule_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB CI MODULE DRIVER");
