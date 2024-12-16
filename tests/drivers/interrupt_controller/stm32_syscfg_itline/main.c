/**
 * Copyright (c) 2024, Etienne Raquin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"

#define SLEEP_TIME K_MSEC(1000)

#define DT_COMPAT_SYSCFG st_stm32_syscfg

/* Number of enabled SYSCFG IT lines */
#define ITLINE_EN_NB DT_CHILD_NUM_STATUS_OKAY(DT_INST(0, DT_COMPAT_SYSCFG))

typedef void (*ISR)(const void *);

struct itline_info {
	const struct device *dev;
	const char *labels[10U];
	uint8_t irq;
	uint8_t isr_table_offset;
};

struct irq_lvl2_info {
	const struct device *dev;
	const char *labels[10U];
	uint16_t irq;
	const struct device *dev_itparent;
};

#define IRQ_INDEX_TO_OFFSET(i, base) (base + i * CONFIG_MAX_IRQ_PER_AGGREGATOR)
#define CAT_2ND_LVL_LIST(i, base)    IRQ_INDEX_TO_OFFSET(i, base)
static const uint8_t itline_isr_table_offset_list[] = {LISTIFY(CONFIG_NUM_2ND_LEVEL_AGGREGATORS,
	CAT_2ND_LVL_LIST, (,), CONFIG_2ND_LVL_ISR_TBL_OFFSET)};

#define BUILD_ITLINE_INFO(node_id)                                                                 \
	{                                                                                          \
		.dev = DEVICE_DT_GET(node_id),                                                     \
		.labels = DT_NODELABEL_STRING_ARRAY(node_id),                                      \
		.irq = (uint16_t)DT_IRQN(node_id),                                                 \
	}
/* clang-format off */
static struct itline_info itline_info_list[] = {
	DT_FOREACH_CHILD_STATUS_OKAY_SEP(DT_INST(0, DT_COMPAT_SYSCFG), BUILD_ITLINE_INFO, (,))};
/* clang-format on */

#define BUILD_IRQLVL2_INFO(node_id, _, idx)                                                        \
	{                                                                                          \
		.dev = DEVICE_DT_GET(node_id),                                                     \
		.labels = DT_NODELABEL_STRING_ARRAY(node_id),                                      \
		.irq = (uint16_t)DT_IRQN_BY_IDX(node_id, idx),                                     \
		.dev_itparent = DEVICE_DT_GET(DT_IRQ_INTC_BY_IDX(node_id, idx)),                   \
	},
#define BUILD_IRQLVL2_INFO_LIST(node_id)                                                           \
	COND_CODE_1(                                                                               \
		IS_EQ(DT_NUM_IRQS(node_id), 1),                                                    \
		(BUILD_IRQLVL2_INFO(node_id, _, 0)),                                               \
		(DT_FOREACH_PROP_ELEM(node_id, interrupt_names,                                    \
			BUILD_IRQLVL2_INFO))                                                       \
)
#define BUILD_IRQLVL2_INFO_LIST_IF_IRQLVL2(node_id)                                                \
	COND_CODE_1(                                                                               \
		IS_EQ(DT_IRQ_LEVEL(node_id), 2),                                                   \
		(BUILD_IRQLVL2_INFO_LIST(node_id)),                                                \
		()                                                                                 \
)
static const struct irq_lvl2_info irq_lvl2_info_list[] = {
	DT_N_S_soc_FOREACH_CHILD_STATUS_OKAY(BUILD_IRQLVL2_INFO_LIST_IF_IRQLVL2)};

int main(void)
{
	ISR syscfg_itline_isr = _sw_isr_table[itline_info_list[0U].irq].isr;
	struct _isr_table_entry *entry = NULL;

	printf("Start\n");

	ARRAY_FOR_EACH(itline_info_list, idx) {
		itline_info_list[idx].isr_table_offset = itline_isr_table_offset_list[idx];
		entry = &_sw_isr_table[itline_info_list[idx].irq];

		printf("IT line %u/%u\n", idx + 1U, ARRAY_SIZE(itline_info_list));
		printf("  dev = 0x%08lX\n", (uintptr_t)itline_info_list[idx].dev);
		printf("    name = %s\n", itline_info_list[idx].dev->name);
		printf("  label = %s\n", itline_info_list[idx].labels[0U]);
		printf("  irq = %u\n", itline_info_list[idx].irq);
		printf("  isr_table_offset = %u\n", itline_info_list[idx].isr_table_offset);

		__ASSERT((entry->isr == syscfg_itline_isr), "IT line %u ISR setup error",
			 itline_info_list[idx].irq);
		printf("  ISR setup OK\n");

		__ASSERT((entry->arg == itline_info_list[idx].dev), "IT line %u arg setup error",
			 itline_info_list[idx].irq);
		printf("  arg setup OK\n");
	}

	ARRAY_FOR_EACH(irq_lvl2_info_list, idx) {
		const struct irq_lvl2_info *irq_lvl2_info = &irq_lvl2_info_list[idx];
		uint8_t irq_lvl1 = irq_parent_level_2(irq_lvl2_info->irq);
		uint8_t irq_lvl2 = irq_from_level_2(irq_lvl2_info->irq);
		uint8_t itline_idx = 0xFF;
		struct itline_info *itline_info = NULL;

		printf("IRQ lvl2 %u/%u\n", idx + 1U, ARRAY_SIZE(irq_lvl2_info_list));
		printf("  dev = 0x%08lX\n", (uintptr_t)irq_lvl2_info->dev);
		printf("    name = %s\n", irq_lvl2_info->dev->name);
		printf("  label = %s\n", irq_lvl2_info->labels[0U]);
		printf("  irq = 0x%04X (%u:%u)\n", irq_lvl2_info->irq, irq_lvl1, irq_lvl2);
		printf("  IT parent dev = 0x%08lX\n", (uintptr_t)irq_lvl2_info->dev_itparent);

		ARRAY_FOR_EACH(itline_info_list, itline_idx_tmp) {
			struct itline_info *itline_info = &itline_info_list[itline_idx_tmp];

			if ((itline_info->dev == irq_lvl2_info->dev_itparent) &&
			    (itline_info->irq == irq_lvl1)) {
				itline_idx = itline_idx_tmp;
				break;
			}
		}

		__ASSERT((itline_idx != 0xFF), "IRQ lvl2 %u:%u IT parent not found", irq_lvl1,
			 irq_lvl2);

		itline_info = &itline_info_list[itline_idx];
		__ASSERT((_sw_isr_table[itline_info->isr_table_offset + irq_lvl2].arg ==
			  irq_lvl2_info->dev),
			 "IRQ lvl2 %u:%u arg setup error", irq_lvl1, irq_lvl2);
		printf("  arg setup OK\n");
	}

	while (1) {
		printf("Loop it\n");
		k_sleep(SLEEP_TIME);
	}
}
