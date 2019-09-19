/*
 * CPU kernel entry/exit control
 *
 * Copyright (C) 2013 ARM Ltd.
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

#include <linux/acpi.h>
#include <linux/cache.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/string.h>
#include <asm/acpi.h>
#include <asm/cpu_ops.h>
#include <asm/smp_plat.h>

#ifdef CONFIG_MP_PLATFORM_ARM_64bit_PORTING
#include <uapi/linux/psci.h>
#include <asm/cacheflush.h>
#include "mdrv_types.h"
#include "mdrv_tee_general.h"
#endif

extern const struct cpu_operations smp_spin_table_ops;
extern const struct cpu_operations acpi_parking_protocol_ops;
extern const struct cpu_operations cpu_psci_ops;
#ifdef CONFIG_MP_PLATFORM_ARM_64bit_PORTING
extern const struct cpu_operations mstar_smp_spin_table;
extern uint32_t isPSCI;
extern uint32_t isPMU_SUPPORT;
extern char UTOPIA_MODE[10];
#endif

const struct cpu_operations *cpu_ops[NR_CPUS] __ro_after_init;

static const struct cpu_operations *dt_supported_cpu_ops[] __initconst = {
	&smp_spin_table_ops,
#if defined(CONFIG_MP_PLATFORM_ARM_64bit_PORTING)
	&mstar_smp_spin_table,
#endif
	&cpu_psci_ops,
	NULL,
};

static const struct cpu_operations *acpi_supported_cpu_ops[] __initconst = {
#ifdef CONFIG_ARM64_ACPI_PARKING_PROTOCOL
	&acpi_parking_protocol_ops,
#endif
#if defined(CONFIG_MP_PLATFORM_ARM_64bit_PORTING)
	&mstar_smp_spin_table,
#endif
	&cpu_psci_ops,
	NULL,
};

static const struct cpu_operations * __init cpu_get_ops(const char *name)
{
	const struct cpu_operations **ops;

	ops = acpi_disabled ? dt_supported_cpu_ops : acpi_supported_cpu_ops;

	while (*ops) {
		if (!strcmp(name, (*ops)->name))
			return *ops;

		ops++;
	}

	return NULL;
}

static const char *__init cpu_read_enable_method(int cpu)
{
	const char *enable_method;

	if (acpi_disabled) {
		struct device_node *dn = of_get_cpu_node(cpu, NULL);

		if (!dn) {
			if (!cpu)
				pr_err("Failed to find device node for boot cpu\n");
			return NULL;
		}

		enable_method = of_get_property(dn, "enable-method", NULL);
		if (!enable_method) {
			/*
			 * The boot CPU may not have an enable method (e.g.
			 * when spin-table is used for secondaries).
			 * Don't warn spuriously.
			 */
			if (cpu != 0)
				pr_err("%s: missing enable-method property\n",
					dn->full_name);
		}
	} else {
		enable_method = acpi_get_enable_method(cpu);
		if (!enable_method) {
			/*
			 * In ACPI systems the boot CPU does not require
			 * checking the enable method since for some
			 * boot protocol (ie parking protocol) it need not
			 * be initialized. Don't warn spuriously.
			 */
			if (cpu != 0)
				pr_err("Unsupported ACPI enable-method\n");
		}
	}

	return enable_method;
}


/*
 * Read a cpu's enable method and record it in cpu_ops.
 */
int __init cpu_read_ops(int cpu)
{
	const char *enable_method = cpu_read_enable_method(cpu);

#ifdef CONFIG_MP_PLATFORM_ARM_64bit_PORTING
	if(TEEINFO_TYPTE == SECURITY_TEEINFO_OSTYPE_OPTEE)
	{
		uint64_t BA;

		isPSCI = PSCI_RET_NOT_SUPPORTED;
		printk("\033[0;33;31m [CPU_OPS] %s %d %s\033[m\n",__func__,__LINE__,UTOPIA_MODE);
		if(strncmp(UTOPIA_MODE,"optee",5) == 0)
		{
			BA = virt_to_phys((uint32_t*)&isPSCI);

			__flush_dcache_area((uint32_t*)&isPSCI,sizeof(uint32_t));
#if 0
			__asm__ __volatile__(
				"ldr x0,=0xb200585b \n\t"
				"mov x1,%0 \n\t"
				"smc #0  \n\t"
				:
				: "r"(BA)
				: "x0","x1"
			);
#else
			struct arm_smccc_res_mstar input_param = {0};
			input_param.a0 = 0xb200585b;
			input_param.a1 = BA;
			isPSCI = tee_fast_call_cmd(&input_param);
#endif
			__flush_dcache_area((uint32_t*)&isPSCI,sizeof(uint32_t));
			printk("\033[0;33;31m [CPU_OPS] %s %d %x\033[m\n",__func__,__LINE__,isPSCI);
		}

		if(PSCI_RET_SUCCESS == isPSCI)
		{
			_ms_psci_ops_set();
			cpu_ops[cpu] = cpu_get_ops("psci");

			isPMU_SUPPORT = PSCI_RET_NOT_SUPPORTED;
			BA = virt_to_phys((uint32_t*)&isPMU_SUPPORT);
			__flush_dcache_area((uint32_t*)&isPMU_SUPPORT,sizeof(uint32_t));

			struct arm_smccc_res_mstar input_param = {0};
			input_param.a0 = 0x8400ee20;
			input_param.a1 = BA;
			isPMU_SUPPORT = tee_fast_call_cmd(&input_param);
			__flush_dcache_area((uint32_t*)&isPMU_SUPPORT,sizeof(uint32_t));
			printk("\033[0;33;31m [CPU_OPS] %s %d isPMU_SUPPORT = %x\033[m\n",__func__,__LINE__,isPMU_SUPPORT);

			if (!cpu_ops[cpu]) {
				pr_warn("Unsupported enable-method: %s\n", enable_method);
				return -EOPNOTSUPP;
			}
			return 0;
		}
	}
#endif

	if (!enable_method)
		return -ENODEV;

	cpu_ops[cpu] = cpu_get_ops(enable_method);
	if (!cpu_ops[cpu]) {
		pr_warn("Unsupported enable-method: %s\n", enable_method);
		return -EOPNOTSUPP;
	}

	return 0;
}
