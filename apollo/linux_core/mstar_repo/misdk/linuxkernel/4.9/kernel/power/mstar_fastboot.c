#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/rtc.h>
#include <linux/lzo.h>
#include <asm/atomic.h>

#include "power.h"

#define CRC_CHECK 1
#if CRC_CHECK
#include <linux/crc32.h>
unsigned long crcval=0;
#endif

/* for this format, startpfn and endpfn also be remembered*/
#define LZO_HEADER	(sizeof(unsigned long)+2*sizeof(unsigned long))

/* Number of pages/bytes we'll compress at one time. */
#define LZO_UNC_PAGES	32
#define LZO_UNC_SIZE	(LZO_UNC_PAGES * PAGE_SIZE)

/* Number of pages/bytes we need for compressed data (worst case). */
#define LZO_CMP_PAGES	DIV_ROUND_UP(lzo1x_worst_compress(LZO_UNC_SIZE) + \
			             LZO_HEADER, PAGE_SIZE)
#define LZO_CMP_SIZE	(LZO_CMP_PAGES * PAGE_SIZE)

extern void fastboot_prepare_read_imgdata(struct snapshot_handle *handle);
extern int fastboot_read_imgpgs_conti(struct snapshot_handle *handle,
        unsigned char* outbuf, int maxpages, unsigned long *startpfn);
extern void fastboot_put_char(char ch);

DEFINE_SPINLOCK(serial_printf_lock);
static void SerialPrintChar(char ch)
{
    unsigned int tmpch=ch;
    if(tmpch=='\n')
    {
        tmpch='\r';
        fastboot_put_char(tmpch);
        tmpch='\n';
    }
    fastboot_put_char(tmpch);
}
static void SerialPrintStr(char *p)
{
    int nLen=strlen(p);
    int i;
    for(i=0;i<nLen;i++)
    {
        SerialPrintChar(p[i]);
    }
}
static void SerialPrintStrAtomic(char *p)
{
    u_long flag;
    spin_lock_irqsave(&serial_printf_lock,flag);
    SerialPrintStr(p);
    spin_unlock_irqrestore(&serial_printf_lock,flag);
}
int vSerialPrintf(const char *fmt, va_list args)
{
    char tmpbuf[500];
    int nLen;
    nLen=vscnprintf(tmpbuf, 500, fmt, args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerialPrintStr(tmpbuf);
    return nLen;
}
int vSerialPrintfAtomic(const char *fmt, va_list args)
{
    char tmpbuf[500];
    int nLen;
    nLen=vscnprintf(tmpbuf, 500, fmt, args);
    if(nLen<=0)
    {
        nLen=0;
    }
    else if(nLen>=500)
    {
        nLen=500-1;
    }
    tmpbuf[nLen]=0;
    SerialPrintStrAtomic(tmpbuf);
    return nLen;
}
int SerialPrintf(char *fmt,...)
{
    int nLen;
    va_list args;
    va_start(args, fmt);
    nLen=vSerialPrintf(fmt, args);
    va_end(args);
    return nLen;
}
int SerialPrintfAtomic(char *fmt,...)
{
    int nLen;
    va_list args;
    va_start(args, fmt);
    nLen=vSerialPrintfAtomic(fmt, args);
    va_end(args);
    return nLen;
}

struct partition_handle{
    struct block_device *ph_dev;
};
static void partion_end_bio(struct bio *bio, int err)
{
    const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		ClearPageUptodate(page);
		printk(KERN_ALERT "Read-error on swap-device (%u:%u:%Lu)\n",
				imajor(bio->bi_bdev->bd_inode),
				iminor(bio->bi_bdev->bd_inode),
				(unsigned long long)bio->bi_sector);
	} else {
		SetPageUptodate(page);
	}
	unlock_page(page);
	bio_put(bio);
}
static int partition_submit_page(struct partition_handle* handle, int rw, pgoff_t page_off,
                                            struct page *page)
{
    struct bio *bio;

	bio = bio_alloc(__GFP_WAIT | __GFP_HIGH, 1);
	if (!bio)
		return -ENOMEM;
	bio->bi_sector = page_off * (PAGE_SIZE >> 9);
	bio->bi_bdev = handle->ph_dev;
	bio->bi_end_io = partion_end_bio;

	if (bio_add_page(bio, page, PAGE_SIZE, 0) < PAGE_SIZE) {
		printk(KERN_ERR "FASTBOOT: Adding page to bio failed at %ld\n",
			page_off);
		bio_put(bio);
		return -EFAULT;
	}

	lock_page(page);
	bio_get(bio);

	submit_bio(rw | REQ_SYNC, bio);
	wait_on_page_locked(page);
	if (rw == READ)
		bio_set_pages_dirty(bio);
	bio_put(bio);
	return 0;
}
int partition_open(struct partition_handle* handle, const char *path)
{
    struct block_device *bd;
    handle->ph_dev = NULL;
    bd = blkdev_get_by_path(path, O_RDWR, NULL);
    if (IS_ERR(bd)) {
        printk(KERN_ERR "FASTBOOT: open blkdev error with %d\n",(int)bd);
        return PTR_ERR(bd);
    }
    handle->ph_dev = bd;
    return 0;
}
int partition_close(struct partition_handle* handle)
{
    if (handle->ph_dev){
        blkdev_put(handle->ph_dev, O_RDWR);
    }
    return 0;
}
unsigned long partition_size(struct partition_handle* handle)
{
    unsigned long ulsize=handle->ph_dev->bd_part->nr_sects;
    return (ulsize<<9);
}
int partition_write_page(struct partition_handle* handle, struct page *page, pgoff_t page_off)
{
    return partition_submit_page(handle, WRITE, page_off, page);
}

static u32 fastboot_imgsize=0;
static int fastboot_mode=0;
static char fastboot_part[50];
static int fastboot_setup(char *s)
{
    char tmp[100];
    int i;
    for (i=0; (*s)&&(i<(100-1)); i++,s++){
        tmp[i]=((*s)==',')?' ':(*s);
    }
    tmp[i]=0;
    if(2!=sscanf(tmp, "%d%49s",&fastboot_mode,fastboot_part)){
        printk(KERN_ERR "FASTBOOT: get fastboot arguments fail\n");
        fastboot_mode=0;
        fastboot_part[0]=0;
    }else{
        printk("FASTBOOT: bootmode=%d,bootpart=%s\n",fastboot_mode,fastboot_part);
    }
	return 1;
}

__setup("fastboot=", fastboot_setup);
int fastboot_getbootmode(void)
{
    return fastboot_mode;
}

#define FB_TM_STRLEN 40
struct fastboot_info{
    char sig[8];
    u32 version;
    u32 pagecount;
    u32 datapagecnt;
    u32 metapagecnt;
    u32 datasize;
    u32 phyent;
    u32 fmt;
    u32 crc;
    char tm[FB_TM_STRLEN];
    struct new_utsname	uts;
    u32		version_code;
};
int fastboot_init_header(void *info)
{
    struct fastboot_info *fbinfo=(struct fastboot_info*)info;
    struct timeval time;
    struct rtc_time tm;
    memset(fbinfo, 0, sizeof(struct fastboot_info));
    snprintf(fbinfo->sig, 8, "%s", "msfbimg");
    fbinfo->version = 0x00010000;
    fbinfo->pagecount = snapshot_get_image_size();
    fbinfo->datapagecnt = fastboot_get_cpy_pages();
    fbinfo->metapagecnt = fastboot_get_meta_pages();
    fbinfo->datasize = (fbinfo->datapagecnt+fbinfo->metapagecnt)*PAGE_SIZE;
    fbinfo->phyent = (u32)virt_to_phys(swsusp_arch_resume);
    fbinfo->fmt = 0;
    fbinfo->crc = 0;
    do_gettimeofday(&time);
    rtc_time_to_tm(time.tv_sec+time.tv_usec/1000000, &tm);
    snprintf(fbinfo->tm,FB_TM_STRLEN, "%5d:%02d:%02d-%02d:%02d:%02d",
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
              tm.tm_hour, tm.tm_min, tm.tm_sec);
    memcpy(&fbinfo->uts, init_utsname(), sizeof(struct new_utsname));
	fbinfo->version_code = LINUX_VERSION_CODE;
    return 0;
}

static int fastboot_save_image(struct partition_handle *handle,
                      struct snapshot_handle *snapshot,
                      unsigned int nr_to_write)
{
    unsigned int m;
    int ret;
    int error = 0;
    int nr_pages;
    struct timeval start;
    struct timeval stop;

    printk(KERN_INFO "FASTBOOT: Saving image data pages (%u pages) ...     ",
        nr_to_write);
    m = nr_to_write / 100;
    if (!m)
        m = 1;
    nr_pages = 0;
#if CRC_CHECK
    crcval = 0;
#endif
    do_gettimeofday(&start);
    do {
        ret = snapshot_read_next(snapshot);
        if (ret > 0) {
#if CRC_CHECK
            crcval = crc32(crcval, data_of(*snapshot), PAGE_SIZE);
#endif
            error = partition_write_page(handle,
                                virt_to_page(data_of(*snapshot)),(pgoff_t)(nr_pages+1));
            if (error)
                break;
            if (!(nr_pages % m))
                printk("\b\b\b\b%3d%%", nr_pages / m);
            nr_pages++;
        }
    } while (ret > 0);
    do_gettimeofday(&stop);
    if (!ret)
        ret = error;
    if (!ret)
        printk("\b\b\b\bdone\n");

    swsusp_show_speed(&start, &stop, nr_to_write, "Wrote");
    return ret;
}

static int fastboot_save_image_lzo(struct partition_handle *handle,
                      struct snapshot_handle *snapshot,
                      unsigned int nr_to_write)
{
    unsigned int m;
    int ret;
    int error = 0;
    int nr_pages, pageoff;
    struct timeval start;
    struct timeval stop;
    size_t off, unc_len, cmp_len;
    unsigned char *unc, *cmp, *wrk, *page;
    unsigned long stpfn,endpfn;

    page = (void *)__get_free_page(__GFP_WAIT | __GFP_HIGH);
    if (!page) {
        printk(KERN_ERR "FASTBOOT: Failed to allocate LZO page\n");
        return -ENOMEM;
    }

    wrk = vmalloc(LZO1X_1_MEM_COMPRESS);
    if (!wrk) {
        printk(KERN_ERR "FASTBOOT: Failed to allocate LZO workspace\n");
        free_page((unsigned long)page);
        return -ENOMEM;
    }

    unc = vmalloc(LZO_UNC_SIZE);
    if (!unc) {
        printk(KERN_ERR "FASTBOOT: Failed to allocate LZO uncompressed\n");
        vfree(wrk);
        free_page((unsigned long)page);
        return -ENOMEM;
    }

    cmp = vmalloc(LZO_CMP_SIZE);
    if (!cmp) {
        printk(KERN_ERR "FASTBOOT: Failed to allocate LZO compressed\n");
        vfree(unc);
        vfree(wrk);
        free_page((unsigned long)page);
        return -ENOMEM;
    }

    printk(KERN_INFO "FASTBOOT: Saving image data pages (%u pages) ...     ",
        nr_to_write);
    m = nr_to_write / 100;
    if (!m)
        m = 1;
    nr_pages = 0;
    pageoff = 1;
#if CRC_CHECK
    crcval = 0;
#endif
    fastboot_prepare_read_imgdata(snapshot);
    do_gettimeofday(&start);
    for (;;) {
        ret = fastboot_read_imgpgs_conti(snapshot, unc, LZO_UNC_SIZE/PAGE_SIZE,&stpfn);
        if (ret<=0)
            goto out_finish;
        endpfn = stpfn+ret-1;
        nr_pages += endpfn-stpfn+1;
        printk(KERN_CONT "\b\b\b\b%3d%%", nr_pages / m);

        unc_len = (endpfn-stpfn+1)*PAGE_SIZE;
        ret = lzo1x_1_compress(unc, unc_len,
                               cmp + LZO_HEADER, &cmp_len, wrk);
        if (ret < 0) {
            printk(KERN_ERR "FASTBOOT: LZO compression failed\n");
            break;
        }

        if (unlikely(!cmp_len ||
                     cmp_len > lzo1x_worst_compress(unc_len))) {
            printk(KERN_ERR "FASTBOOT: Invalid LZO compressed length\n");
            ret = -1;
            break;
        }

        *(unsigned long *)cmp = cmp_len;
        ((unsigned long *)(cmp+sizeof(unsigned long)))[0]=stpfn;
        ((unsigned long *)(cmp+sizeof(unsigned long)))[1]=endpfn;

#if CRC_CHECK
        crcval = crc32(crcval, cmp, (LZO_HEADER+cmp_len+PAGE_SIZE-1)/PAGE_SIZE*PAGE_SIZE);
#endif

        if (error){
            pageoff += (LZO_HEADER+cmp_len+PAGE_SIZE-1)/PAGE_SIZE;
            continue;
        }

        if ( (pageoff+(LZO_HEADER+cmp_len+PAGE_SIZE-1)/PAGE_SIZE)*PAGE_SIZE>partition_size(handle) ){
            pageoff += (LZO_HEADER+cmp_len+PAGE_SIZE-1)/PAGE_SIZE;
            error = -ENOSPC;
            continue;
        }

        /*
         * Given we are writing one page at a time to disk, we copy
         * that much from the buffer, although the last bit will likely
         * be smaller than full page. This is OK - we saved the length
         * of the compressed data, so any garbage at the end will be
         * discarded when we read it.
         */
        for (off = 0; off < LZO_HEADER + cmp_len; off += PAGE_SIZE) {
            memcpy(page, cmp + off, PAGE_SIZE);

            ret = partition_write_page(handle,
                                virt_to_page(page),(pgoff_t)(pageoff));
            pageoff++;
            if (ret)
                goto out_finish;
        }
    }
out_finish:
    do_gettimeofday(&stop);
    if (!ret)
        ret = error;
    if (!ret)
        printk("\b\b\b\bdone\n");

    fastboot_imgsize = (pageoff-1)*PAGE_SIZE;
    printk("\nFASTBOOT: final image size is 0x%08lx\n",pageoff*PAGE_SIZE);
    if (ret == -ENOSPC){
        printk(KERN_ERR "FASTBOOT: Partition not big enough\n");
    }
    swsusp_show_speed(&start, &stop, nr_to_write, "Wrote");
    vfree(cmp);
    vfree(unc);
    vfree(wrk);
    free_page((unsigned long)page);
    return ret;
}

int fastboot_write(unsigned int flags)
{
    struct partition_handle parthandle;
    struct snapshot_handle snapshot;
    struct fastboot_info * header;
    int error;

    if(sizeof(struct fastboot_info)>PAGE_SIZE){
        printk("Fastboot info size over than page size\n");
        return -1;
    }

    error = partition_open(&parthandle, fastboot_part);
    if (error<0){
        printk("FASTBOOT: open partition fail\n");
        return error;
    }

    memset(&snapshot, 0, sizeof(struct snapshot_handle));
    error = snapshot_read_next(&snapshot);
    if (error < PAGE_SIZE) {
        if (error >= 0)
            error = -EFAULT;
        printk("FASTBOOT: read first snapshot image fail\n");
        goto out;
    }
    header = (struct fastboot_info *)data_of(snapshot);

    error = 0;
    if (flags&SF_NOCOMPRESS_MODE){
        if ((header->pagecount*PAGE_SIZE) > partition_size(&parthandle)) {
            printk(KERN_ERR "FASTBOOT: Partition not big enough for %08lx(bytes) image\n",(header->pagecount*PAGE_SIZE));
            error = -ENOSPC;
            goto out;
        }
    }

    if (!error){
        if(flags&SF_NOCOMPRESS_MODE){
            error = fastboot_save_image(&parthandle, &snapshot, header->pagecount-1);
        }else{
            error = fastboot_save_image_lzo(&parthandle, &snapshot, fastboot_get_cpy_pages());
        }
        if (error)
            printk("FASTBOOT: save image fail\n");
    }
    if (!error){
        memset(&snapshot, 0, sizeof(struct snapshot_handle));
        error = snapshot_read_next(&snapshot);
        if (error < PAGE_SIZE) {
            if (error >= 0)
                error = -EFAULT;
            goto out;
        }
        header = (struct fastboot_info *)data_of(snapshot);
        if (!(flags&SF_NOCOMPRESS_MODE)){
            header->pagecount = header->datapagecnt+1;
            header->metapagecnt = 0;
            header->datasize = fastboot_imgsize;
            header->fmt = 1;
        }
#if CRC_CHECK
        header->crc = crcval;
        printk("FASTBOOT: image crc is %08lx\n",crcval);
#endif
        error = partition_write_page(&parthandle, virt_to_page(header), 0);
    }
 out:
    partition_close(&parthandle);
    return error;
}
void fastboot_reset_onresume(void)
{
    fastboot_mode=2;
    in_suspend=0;
}

