/*
 * Based on arch/arm/mm/init.c
 *
 * Copyright (C) 1995-2005 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/cache.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/sort.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/efi.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>

#include <asm/boot.h>
#include <asm/fixmap.h>
#include <asm/kasan.h>
#include <asm/kernel-pgtable.h>
#include <asm/memory.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/tlb.h>
#include <asm/alternative.h>

/*
 * We need to be able to catch inadvertent references to memstart_addr
 * that occur (potentially in generic code) before arm64_memblock_init()
 * executes, which assigns it its actual value. So use a default value
 * that cannot be mistaken for a real physical address.
 */
s64 memstart_addr __ro_after_init = -1;
phys_addr_t arm64_dma_phys_limit __ro_after_init;
#if defined(CONFIG_MP_MMA_UMA_WITH_NARROW) || defined(CONFIG_MP_ASYM_UMA_ALLOCATION)
extern u64 mma_dma_zone_size;
#endif
#ifdef CONFIG_MSTAR_CHIP
#ifdef CONFIG_PSTORE_RAM
#include <linux/pstore_ram.h>
extern struct ramoops_platform_data ramoops_data;
#endif
#endif

#ifdef CONFIG_MP_CMA_PATCH_CMA_DEFAULT_BUFFER_LIMITTED_TO_LX0
extern unsigned long lx_mem_addr;
#endif

#ifdef CONFIG_BLK_DEV_INITRD
static int __init early_initrd(char *p)
{
	unsigned long start, size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		initrd_start = start;
		initrd_end = start + size;
	}
	return 0;
}
early_param("initrd", early_initrd);
#endif

#ifdef CONFIG_KEXEC_CORE
/*
 * reserve_crashkernel() - reserves memory for crash kernel
 *
 * This function reserves memory area given in "crashkernel=" kernel command
 * line parameter. The memory reserved is used by dump capture kernel when
 * primary kernel is crashing.
 */
static void __init reserve_crashkernel(void)
{
	unsigned long long crash_base, crash_size;
	int ret;

	ret = parse_crashkernel(boot_command_line, memblock_phys_mem_size(),
				&crash_size, &crash_base);
	/* no crashkernel= or invalid value specified */
	if (ret || !crash_size)
		return;

	crash_size = PAGE_ALIGN(crash_size);

	if (crash_base == 0) {
		/* Current arm64 boot protocol requires 2MB alignment */
		crash_base = memblock_find_in_range(0, ARCH_LOW_ADDRESS_LIMIT,
				crash_size, SZ_2M);
		if (crash_base == 0) {
			pr_warn("cannot allocate crashkernel (size:0x%llx)\n",
				crash_size);
			return;
		}
	} else {
		/* User specifies base address explicitly. */
		if (!memblock_is_region_memory(crash_base, crash_size)) {
			pr_warn("cannot reserve crashkernel: region is not memory\n");
			return;
		}

		if (memblock_is_region_reserved(crash_base, crash_size)) {
			pr_warn("cannot reserve crashkernel: region overlaps reserved memory\n");
			return;
		}

		if (!IS_ALIGNED(crash_base, SZ_2M)) {
			pr_warn("cannot reserve crashkernel: base address is not 2MB aligned\n");
			return;
		}
	}
	memblock_reserve(crash_base, crash_size);

	pr_info("crashkernel reserved: 0x%016llx - 0x%016llx (%lld MB)\n",
		crash_base, crash_base + crash_size, crash_size >> 20);

	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
}

static void __init kexec_reserve_crashkres_pages(void)
{
#ifdef CONFIG_HIBERNATION
	phys_addr_t addr;
	struct page *page;

	if (!crashk_res.end)
		return;

	/*
	 * To reduce the size of hibernation image, all the pages are
	 * marked as Reserved initially.
	 */
	for (addr = crashk_res.start; addr < (crashk_res.end + 1);
			addr += PAGE_SIZE) {
		page = phys_to_page(addr);
		SetPageReserved(page);
	}
#endif
}
#else
static void __init reserve_crashkernel(void)
{
}

static void __init kexec_reserve_crashkres_pages(void)
{
}
#endif /* CONFIG_KEXEC_CORE */

#ifdef CONFIG_CRASH_DUMP
static int __init early_init_dt_scan_elfcorehdr(unsigned long node,
		const char *uname, int depth, void *data)
{
	const __be32 *reg;
	int len;

	if (depth != 1 || strcmp(uname, "chosen") != 0)
		return 0;

	reg = of_get_flat_dt_prop(node, "linux,elfcorehdr", &len);
	if (!reg || (len < (dt_root_addr_cells + dt_root_size_cells)))
		return 1;

	elfcorehdr_addr = dt_mem_next_cell(dt_root_addr_cells, &reg);
	elfcorehdr_size = dt_mem_next_cell(dt_root_size_cells, &reg);

	return 1;
}

/*
 * reserve_elfcorehdr() - reserves memory for elf core header
 *
 * This function reserves the memory occupied by an elf core header
 * described in the device tree. This region contains all the
 * information about primary kernel's core image and is used by a dump
 * capture kernel to access the system memory on primary kernel.
 */
static void __init reserve_elfcorehdr(void)
{
	of_scan_flat_dt(early_init_dt_scan_elfcorehdr, NULL);

	if (!elfcorehdr_size)
		return;

	if (memblock_is_region_reserved(elfcorehdr_addr, elfcorehdr_size)) {
		pr_warn("elfcorehdr is overlapped\n");
		return;
	}

	memblock_reserve(elfcorehdr_addr, elfcorehdr_size);

	pr_info("Reserving %lldKB of memory at 0x%llx for elfcorehdr\n",
		elfcorehdr_size >> 10, elfcorehdr_addr);
}
#else
static void __init reserve_elfcorehdr(void)
{
}
#endif /* CONFIG_CRASH_DUMP */
/*
 * Return the maximum physical address for ZONE_DMA32 (DMA_BIT_MASK(32)). It
 * currently assumes that for memory starting above 4G, 32-bit devices will
 * use a DMA offset.
 */
#if defined(CONFIG_MP_MMA_UMA_WITH_NARROW) || defined(CONFIG_MP_ASYM_UMA_ALLOCATION)
phys_addr_t __init max_zone_dma_phys(void)
#else
static phys_addr_t __init max_zone_dma_phys(void)
#endif
{
	phys_addr_t offset = memblock_start_of_DRAM() & GENMASK_ULL(63, 32);
	return min(offset + (1ULL << 32), memblock_end_of_DRAM());
}

#ifdef CONFIG_NUMA

static void __init zone_sizes_init(unsigned long min, unsigned long max)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES]  = {0};

#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = PFN_DOWN(max_zone_dma_phys());
#endif
	max_zone_pfns[ZONE_NORMAL] = max;

	free_area_init_nodes(max_zone_pfns);
}

#else
#ifdef CONFIG_MP_PLATFORM_PHY_ADDRESS_MORE_THAN_2G_SET_MOVABLE
extern u64 linux_memory3_address,linux_memory3_length;
#endif
static void __init zone_sizes_init(unsigned long min, unsigned long max)
{
	struct memblock_region *reg;
	unsigned long zone_size[MAX_NR_ZONES], zhole_size[MAX_NR_ZONES];
	unsigned long max_dma = min;

	memset(zone_size, 0, sizeof(zone_size));

	/* 4GB maximum for 32-bit only capable devices */
#ifdef CONFIG_ZONE_DMA32
	max_dma = PFN_DOWN(arm64_dma_phys_limit);
	zone_size[ZONE_DMA32] = max_dma - min;
#endif
	zone_size[ZONE_NORMAL] = max - max_dma;

	memcpy(zhole_size, zone_size, sizeof(zhole_size));

	for_each_memblock(memory, reg) {
		unsigned long start = memblock_region_memory_base_pfn(reg);
		unsigned long end = memblock_region_memory_end_pfn(reg);

		if (start >= max)
			continue;

#ifdef CONFIG_ZONE_DMA32
		if (start < max_dma) {
			unsigned long dma_end = min(end, max_dma);
			zhole_size[ZONE_DMA32] -= dma_end - start;
		}
#endif
		if (end > max_dma) {
			unsigned long normal_end = min(end, max);
			unsigned long normal_start = max(start, max_dma);
#ifdef CONFIG_MP_PLATFORM_PHY_ADDRESS_MORE_THAN_2G_SET_MOVABLE
                        zhole_size[ZONE_MOVABLE] -= normal_end - normal_start;
#else
                        zhole_size[ZONE_NORMAL] -= normal_end - normal_start;
#endif
		}
	}

#ifdef CONFIG_MP_PLATFORM_PHY_ADDRESS_MORE_THAN_2G_SET_MOVABLE
        if (linux_memory3_length != 0) {
                printk("(yjdbg) apply zone mapping\n");
                zone_size[ZONE_NORMAL] = 0x80000;
                zhole_size[ZONE_NORMAL] = 0x80000;
                zone_size[ZONE_NORMAL] += (linux_memory3_address >> 12) - 0x180000;
                zhole_size[ZONE_NORMAL] += (linux_memory3_address >> 12) - 0x180000;
                zone_size[ZONE_MOVABLE] = linux_memory3_length >> 12;
                zhole_size[ZONE_MOVABLE] = 0x0;
        }

        pr_err("max_dma: 0x%llx\n", max_dma);
        printk("zone_sizes_init: zone_size[DMA]=0x%x,zone_size[normal]=0x%x,zone_size[move]=0x%x\n",zone_size[ZONE_DMA32],zone_size[ZONE_NORMAL],zone_size[ZONE_MOVABLE]);
        printk("zone_sizes_init: zhole_size[DMA]=0x%x,zhole_size[normal]=0x%x,zhole_size[move]=0x%x\n",zhole_size[ZONE_DMA32],zhole_size[ZONE_NORMAL],zhole_size[ZONE_MOVABLE]);
#endif

        free_area_init_node(0, zone_size, min, zhole_size);
}

#endif /* CONFIG_NUMA */

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
int pfn_valid(unsigned long pfn)
{
	phys_addr_t addr = pfn << PAGE_SHIFT;

	if ((addr >> PAGE_SHIFT) != pfn)
		return 0;
	return memblock_is_map_memory(addr);
}
EXPORT_SYMBOL(pfn_valid);
#endif

#ifndef CONFIG_SPARSEMEM
static void __init arm64_memory_present(void)
{
}
#else
static void __init arm64_memory_present(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		int nid = memblock_get_region_node(reg);

		memory_present(nid, memblock_region_memory_base_pfn(reg),
				memblock_region_memory_end_pfn(reg));
	}
}
#endif

static phys_addr_t memory_limit = PHYS_ADDR_MAX;
#ifdef CONFIG_MP_DEBUG_TOOL_MEMORY_USAGE_TRACE
extern phys_addr_t arm_lowmem_limit;
void reserve_page_trace_mem(phys_addr_t beg,phys_addr_t end);
#endif

/*
 * Limit the memory size that was specified via FDT.
 */
static int __init early_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = memparse(p, &p) & PAGE_MASK;
	pr_notice("Memory limited to %lldMB\n", memory_limit >> 20);

	return 0;
}
early_param("mem", early_mem);

static int __init early_init_dt_scan_usablemem(unsigned long node,
		const char *uname, int depth, void *data)
{
	struct memblock_region *usablemem = data;
	const __be32 *reg;
	int len;

	if (depth != 1 || strcmp(uname, "chosen") != 0)
		return 0;

	reg = of_get_flat_dt_prop(node, "linux,usable-memory-range", &len);
	if (!reg || (len < (dt_root_addr_cells + dt_root_size_cells)))
		return 1;

	usablemem->base = dt_mem_next_cell(dt_root_addr_cells, &reg);
	usablemem->size = dt_mem_next_cell(dt_root_size_cells, &reg);

	return 1;
}

static void __init fdt_enforce_memory_region(void)
{
	struct memblock_region reg = {
		.size = 0,
	};

	of_scan_flat_dt(early_init_dt_scan_usablemem, &reg);

	if (reg.size)
		memblock_cap_memory_range(reg.base, reg.size);
}

#ifdef CONFIG_ZRAM_OVER_GENPOOL
extern unsigned long long carveout_start;
extern unsigned long long carveout_end;
extern phys_addr_t linux_memory2_address, linux_memory2_length;
extern phys_addr_t linux_memory3_address, linux_memory3_length;
extern phys_addr_t linux_memory4_address, linux_memory4_length;
#include <linux/of_reserved_mem.h>

unsigned long long asym_dram_size = 0x0;
static int __init asym_dram_model(char *str)
{
	asym_dram_size = (unsigned long long)simple_strtol(str, NULL, 16);
	pr_info("[ASYM_DRAM] asym_dram_size = 0x%x\n", asym_dram_size);
	return 0;
}
early_param("DRAM_ASYM_SIZE", asym_dram_model);

unsigned int disable_zram_over_genpool = 0;
static int __init setup_disable_zram_over_genpool(char *str)
{
	get_option(&str, &disable_zram_over_genpool);
	pr_info("[ASYM_DRAM] disable_zram_over_genpool = %d\n", disable_zram_over_genpool);
	return 0;
}
early_param("disable_zram_over_genpool", setup_disable_zram_over_genpool);

static void __init get_low_bandwidth_zram_region(
	phys_addr_t *zram_start, phys_addr_t *zram_end, size_t z_size)
{
	phys_addr_t lx_start = 0;
	size_t lx_size = 0;

	/* get last LXn */
	if (linux_memory4_address != 0) {
		lx_start = linux_memory4_address;
		lx_size = linux_memory4_length;
	} else if (linux_memory3_address != 0) {
		lx_start = linux_memory3_address;
		lx_size = linux_memory3_length;
	} else if (linux_memory2_address != 0) {
		lx_start = linux_memory2_address;
		lx_size = linux_memory2_length;
	}

	/* reserve from the tail of LXn */
	*zram_start = lx_start + lx_size - z_size;
	*zram_end = lx_start + lx_size;
}

void __init reserve_zram_genpool(void)
{
	phys_addr_t start = 0, end = 0;
	phys_addr_t base = 0, align = 0;
	size_t size;
	int nomap;
	int ret;

	#ifdef CONFIG_ZRAM_RESERVED_DISKSIZE
	size = CONFIG_ZRAM_RESERVED_DISKSIZE;
	#else
	size = 0;
	#endif
	align = PAGE_SIZE;
	nomap = 0;

	get_low_bandwidth_zram_region(&start, &end, size);
	if (start <= 0 || end <= 0) {
		printk(KERN_WARNING "%s: no low-handwidth dram region\n", __func__);
		return;
	}

	ret = early_init_dt_alloc_reserved_memory_arch(size,
			align, start, end, nomap, &base);
	if (ret != 0) {
		printk(KERN_ALERT "%s: allocated memory for zram/genpool node fail\n", __func__);
		return;
	}
	printk("%s: allocated memory for zram/genpool node: base %pa, size %ld MiB\n", __func__, &base, (unsigned long)size / SZ_1M);

	carveout_start = base;
	carveout_end = carveout_start + size;
	printk("%s: allocated memory for zram/genpool node: base 0x%llx, end 0x%llx\n", __func__, carveout_start, carveout_end);
}
#endif

void __init arm64_memblock_init(void)
{
	const s64 linear_region_size = -(s64)PAGE_OFFSET;

	/* Handle linux,usable-memory-range property */
	fdt_enforce_memory_region();

	/* Remove memory above our supported physical address size */
	memblock_remove(1ULL << PHYS_MASK_SHIFT, ULLONG_MAX);

	/*
	 * Ensure that the linear region takes up exactly half of the kernel
	 * virtual address space. This way, we can distinguish a linear address
	 * from a kernel/module/vmalloc address by testing a single bit.
	 */
	BUILD_BUG_ON(linear_region_size != BIT(VA_BITS - 1));

	/*
	 * Select a suitable value for the base of physical memory.
	 */
	memstart_addr = round_down(memblock_start_of_DRAM(),
				   ARM64_MEMSTART_ALIGN);

	/*
	 * Remove the memory that we will not be able to cover with the
	 * linear mapping. Take care not to clip the kernel which may be
	 * high in memory.
	 */
	memblock_remove(max_t(u64, memstart_addr + linear_region_size,
			__pa_symbol(_end)), ULLONG_MAX);
	if (memstart_addr + linear_region_size < memblock_end_of_DRAM()) {
		/* ensure that memstart_addr remains sufficiently aligned */
		memstart_addr = round_up(memblock_end_of_DRAM() - linear_region_size,
					 ARM64_MEMSTART_ALIGN);
		memblock_remove(0, memstart_addr);
	}

	/*
	 * Apply the memory limit if it was set. Since the kernel may be loaded
	 * high up in memory, add back the kernel region that must be accessible
	 * via the linear mapping.
	 */
	if (memory_limit != PHYS_ADDR_MAX) {
		memblock_mem_limit_remove_map(memory_limit);
		memblock_add(__pa_symbol(_text), (u64)(_end - _text));
	}

	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && initrd_start) {
		/*
		 * Add back the memory we just removed if it results in the
		 * initrd to become inaccessible via the linear mapping.
		 * Otherwise, this is a no-op
		 */
#if (MP_PLATFORM_ARM_64bit_BOOTARGS_NODTB == 1)
		u64 base = __pa(initrd_start) & PAGE_MASK;
		u64 size = PAGE_ALIGN(__pa(initrd_end)) - base;
#else
		u64 base = initrd_start & PAGE_MASK;
		u64 size = PAGE_ALIGN(initrd_end) - base;
#endif
		/*
		 * We can only add back the initrd memory if we don't end up
		 * with more memory than we can address via the linear mapping.
		 * It is up to the bootloader to position the kernel and the
		 * initrd reasonably close to each other (i.e., within 32 GB of
		 * each other) so that all granule/#levels combinations can
		 * always access both.
		 */
		if (WARN(base < memblock_start_of_DRAM() ||
			 base + size > memblock_start_of_DRAM() +
				       linear_region_size,
			"initrd not fully accessible via the linear mapping -- please check your bootloader ...\n")) {
			initrd_start = 0;
		} else {
			memblock_remove(base, size); /* clear MEMBLOCK_ flags */
			memblock_add(base, size);
			memblock_reserve(base, size);
		}
	}

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		extern u16 memstart_offset_seed;
		u64 range = linear_region_size -
			    (memblock_end_of_DRAM() - memblock_start_of_DRAM());

		/*
		 * If the size of the linear region exceeds, by a sufficient
		 * margin, the size of the region that the available physical
		 * memory spans, randomize the linear region as well.
		 */
		if (memstart_offset_seed > 0 && range >= ARM64_MEMSTART_ALIGN) {
			range /= ARM64_MEMSTART_ALIGN;
			memstart_addr -= ARM64_MEMSTART_ALIGN *
					 ((range * memstart_offset_seed) >> 16);
		}
	}

	/*
	 * Register the kernel text, kernel data, initrd, and initial
	 * pagetables with memblock.
	 */
	memblock_reserve(__pa_symbol(_text), _end - _text);
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
#if (MP_PLATFORM_ARM_64bit_BOOTARGS_NODTB == 1)
		memblock_reserve(__virt_to_phys(initrd_start), initrd_end - initrd_start);
#else
		memblock_reserve(initrd_start, initrd_end - initrd_start);

		/* the generic initrd code expects virtual addresses */
		initrd_start = __phys_to_virt(initrd_start);
		initrd_end = __phys_to_virt(initrd_end);

#endif
	}
#endif

#if defined(CONFIG_MP_PLATFORM_ARM_64bit_PORTING)
	/*
	 * Reserve the page tables.  These are already in use,
	 * and can only be in node 0.
	 */
	//FIXME
	memblock_reserve(__pa(swapper_pg_dir),
		(__pa_symbol(swapper_pg_end) - __pa_symbol(swapper_pg_dir)));
	memblock_reserve(__pa(idmap_pg_dir), IDMAP_DIR_SIZE);
#endif
#ifdef CONFIG_MP_DEBUG_TOOL_MEMORY_USAGE_TRACE
        reserve_page_trace_mem(PHYS_OFFSET, arm_lowmem_limit);
#endif

	early_init_fdt_scan_reserved_mem();

	/* 4GB maximum for 32-bit only capable devices */
	if (IS_ENABLED(CONFIG_ZONE_DMA32)) {
#if defined(CONFIG_MP_MMA_UMA_WITH_NARROW) || defined(CONFIG_MP_ASYM_UMA_ALLOCATION)
		if (mma_dma_zone_size > 0) {
			arm64_dma_phys_limit = mma_dma_zone_size + memblock_start_of_DRAM();
			pr_info("szie 0x%llx limit 0x%lx",
				mma_dma_zone_size, (unsigned long)arm64_dma_phys_limit);
                } else {
			arm64_dma_phys_limit = max_zone_dma_phys();
			pr_info("\033[35m MMAP Need Set DMA ZONE SIZE !!!\033[m\n");
		}
#else
		arm64_dma_phys_limit = max_zone_dma_phys();
#endif
	} else {
		arm64_dma_phys_limit = PHYS_MASK + 1;
	}

	reserve_crashkernel();

	reserve_elfcorehdr();

	high_memory = __va(memblock_end_of_DRAM() - 1) + 1;

#if defined(CONFIG_MP_CMA_PATCH_CMA_DEFAULT_BUFFER_LIMITTED_TO_LX0)
	/*
	 * this patch limit cma default buffer at LX_MEM(LX0)
	 * original case, we only find default cma_buffer @ whole lowmem
	 * (usually @ the backend of lowmem)
	 */
	pr_info("\033[35m %s(%d) find cma_default buffer at only LX0\033[m\n",
			__func__, __LINE__);
	dma_contiguous_reserve(min((lx_mem_addr+lx_mem_size),
				(unsigned long)arm64_dma_phys_limit));
#else
	dma_contiguous_reserve(arm64_dma_phys_limit);
#endif


#ifdef CONFIG_MSTAR_CHIP
	/* Reserve 16K for put magic Key,new magic mechanism*/
	memblock_reserve(PHYS_OFFSET, 16 * 1024);

#ifdef CONFIG_PSTORE_RAM
	/* Reserve this to do ramoops (the region is defined @ dts).
	 * The ramoops_data is defined @ fs/pstore/ram.c, you can change the setting.
	 */
	memblock_reserve(ramoops_data.mem_address, ramoops_data.mem_size);
#endif
#endif
#ifdef CONFIG_ZRAM_OVER_GENPOOL
	if (!disable_zram_over_genpool && asym_dram_size) {
		reserve_zram_genpool();
		pr_info("%s: zram_over_genpool enable!\n", __func__);
	} else {
		pr_info("%s: zram_over_genpool disable!\n", __func__);
	}
#endif

	memblock_allow_resize();
}

void __init bootmem_init(void)
{
	unsigned long min, max;

	min = PFN_UP(memblock_start_of_DRAM());
	max = PFN_DOWN(memblock_end_of_DRAM());

	early_memtest(min << PAGE_SHIFT, max << PAGE_SHIFT);

	max_pfn = max_low_pfn = max;

	arm64_numa_init();
	/*
	 * Sparsemem tries to allocate bootmem in memory_present(), so must be
	 * done after the fixed reservations.
	 */
	arm64_memory_present();

	sparse_init();
	zone_sizes_init(min, max);

	memblock_dump_all();
}

#ifndef CONFIG_SPARSEMEM_VMEMMAP
static inline void free_memmap(unsigned long start_pfn, unsigned long end_pfn)
{
	struct page *start_pg, *end_pg;
	unsigned long pg, pgend;

	/*
	 * Convert start_pfn/end_pfn to a struct page pointer.
	 */
	start_pg = pfn_to_page(start_pfn - 1) + 1;
	end_pg = pfn_to_page(end_pfn - 1) + 1;

	/*
	 * Convert to physical addresses, and round start upwards and end
	 * downwards.
	 */
	pg = (unsigned long)PAGE_ALIGN(__pa(start_pg));
	pgend = (unsigned long)__pa(end_pg) & PAGE_MASK;

	/*
	 * If there are free pages between these, free the section of the
	 * memmap array.
	 */
	if (pg < pgend)
		free_bootmem(pg, pgend - pg);
}

/*
 * The mem_map array can get very big. Free the unused area of the memory map.
 */
static void __init free_unused_memmap(void)
{
	unsigned long start, prev_end = 0;
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		start = __phys_to_pfn(reg->base);

#ifdef CONFIG_SPARSEMEM
		/*
		 * Take care not to free memmap entries that don't exist due
		 * to SPARSEMEM sections which aren't present.
		 */
		start = min(start, ALIGN(prev_end, PAGES_PER_SECTION));
#endif
		/*
		 * If we had a previous bank, and there is a space between the
		 * current bank and the previous, free it.
		 */
		if (prev_end && prev_end < start)
			free_memmap(prev_end, start);

		/*
		 * Align up here since the VM subsystem insists that the
		 * memmap entries are valid from the bank end aligned to
		 * MAX_ORDER_NR_PAGES.
		 */
		prev_end = ALIGN(__phys_to_pfn(reg->base + reg->size),
				 MAX_ORDER_NR_PAGES);
	}

#ifdef CONFIG_SPARSEMEM
	if (!IS_ALIGNED(prev_end, PAGES_PER_SECTION))
		free_memmap(prev_end, ALIGN(prev_end, PAGES_PER_SECTION));
#endif
}
#endif	/* !CONFIG_SPARSEMEM_VMEMMAP */

/*
 * mem_init() marks the free areas in the mem_map and tells us how much memory
 * is free.  This is done after various parts of the system have claimed their
 * memory after the kernel image.
 */
void __init mem_init(void)
{
#if defined(CONFIG_MP_MMA_UMA_WITH_NARROW) || defined(CONFIG_MP_ASYM_UMA_ALLOCATION)
	if (swiotlb_force == SWIOTLB_FORCE ||
		max_pfn > (max_zone_dma_phys() >> PAGE_SHIFT))
		swiotlb_init(1);
#else
	if (swiotlb_force == SWIOTLB_FORCE ||
		max_pfn > (arm64_dma_phys_limit >> PAGE_SHIFT))
		swiotlb_init(1);
#endif
	else
		swiotlb_force = SWIOTLB_NO_FORCE;

	set_max_mapnr(pfn_to_page(max_pfn) - mem_map);

#ifndef CONFIG_SPARSEMEM_VMEMMAP
	free_unused_memmap();
#endif
	/* this will put all unused low memory onto the freelists */
	free_all_bootmem();

	kexec_reserve_crashkres_pages();

	mem_init_print_info(NULL);

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can be
	 * detected at build time already.
	 */
#ifdef CONFIG_COMPAT
	BUILD_BUG_ON(TASK_SIZE_32			> TASK_SIZE_64);
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
	/*
	 * Make sure we chose the upper bound of sizeof(struct page)
	 * correctly when sizing the VMEMMAP array.
	 */
	BUILD_BUG_ON(sizeof(struct page) > (1 << STRUCT_PAGE_MAX_SHIFT));
#endif

	if (PAGE_SIZE >= 16384 && get_num_physpages() <= 128) {
		extern int sysctl_overcommit_memory;
		/*
		 * On a machine this small we won't get anywhere without
		 * overcommit, so turn it on by default.
		 */
		sysctl_overcommit_memory = OVERCOMMIT_ALWAYS;
	}
}

void free_initmem(void)
{
	free_reserved_area(lm_alias(__init_begin),
			   lm_alias(__init_end),
			   0, "unused kernel");
	/*
	 * Unmap the __init region but leave the VM area in place. This
	 * prevents the region from being reused for kernel modules, which
	 * is not supported by kallsyms.
	 */
	unmap_kernel_range((u64)__init_begin, (u64)(__init_end - __init_begin));
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd __initdata;

void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	if (!keep_initrd) {
		free_reserved_area((void *)start, (void *)end, 0, "initrd");
		memblock_free(__virt_to_phys(start), end - start);
	}
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif

/*
 * Dump out memory limit information on panic.
 */
static int dump_mem_limit(struct notifier_block *self, unsigned long v, void *p)
{
	if (memory_limit != PHYS_ADDR_MAX) {
		pr_emerg("Memory Limit: %llu MB\n", memory_limit >> 20);
	} else {
		pr_emerg("Memory Limit: none\n");
	}
	return 0;
}

static struct notifier_block mem_limit_notifier = {
	.notifier_call = dump_mem_limit,
};

static int __init register_mem_limit_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &mem_limit_notifier);
	return 0;
}

#ifdef CONFIG_MP_PLATFORM_PHY_ADDRESS_MORE_THAN_2G_SET_MOVABLE_DEBUG
void testAddrTranslation(void)
{
    printk("testAddrTranslation\n");
    printk("%llx\n", __phys_to_virt(0x20200000));
    printk("%llx\n", __phys_to_virt(0x53400000));
    printk("%llx\n", __phys_to_virt(0x180000000UL));

    printk("%llx\n", __virt_to_phys(__phys_to_virt(0x20200000)));
    printk("%llx\n", __virt_to_phys(__phys_to_virt(0x53400000)));
    printk("%llx\n", __virt_to_phys(__phys_to_virt(0x180000000UL)));

    printk("%llx\n", __virt_to_phys(0xFFFFFFC000000000UL));
    printk("%llx\n", __virt_to_phys(0xFFFFFFC025200000UL));
    printk("%llx\n", __virt_to_phys(0xFFFFFFC06F800000UL));

    printk("%llx\n", PHYS_PFN(0x20200000));
    printk("%llx\n", PHYS_PFN(0x53400000));
    printk("%llx\n", PHYS_PFN(0x180000000L));

    printk("%llx\n", PFN_PHYS(PHYS_PFN(0x20200000)));
    printk("%llx\n", PFN_PHYS(PHYS_PFN(0x53400000)));
    printk("%llx\n", PFN_PHYS(PHYS_PFN(0x180000000UL)));

    printk("%llx\n", page_to_phys(phys_to_page(0x20200000)));
    printk("%llx\n", page_to_phys(phys_to_page(0x53400000)));
    printk("%llx\n", page_to_phys(phys_to_page(0x180000000UL))); 

    printk("test virt_to_page\n");
    printk("%llx\n", page_to_virt(virt_to_page(__phys_to_virt((0x20200000)))));
    printk("%llx\n", page_to_virt(virt_to_page(__phys_to_virt((0x53400000)))));
    printk("%llx\n", page_to_virt(virt_to_page(__phys_to_virt((0x180000000UL))))); 

}
#endif
__initcall(register_mem_limit_dumper);
