/*
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __RISCV32_PPU_SOC_H_
#define __RISCV32_PPU_SOC_H_

#include <soc_common.h>
#include <devicetree.h>

/* GPIO Interrupts */
#define PPU_GPIO_0_IRQ           (0)
#define PPU_GPIO_1_IRQ           (1)
#define PPU_GPIO_2_IRQ           (2)
#define PPU_GPIO_3_IRQ           (3)
#define PPU_GPIO_4_IRQ           (4)
#define PPU_GPIO_5_IRQ           (5)
#define PPU_GPIO_6_IRQ           (6)
#define PPU_GPIO_7_IRQ           (7)
#define PPU_GPIO_8_IRQ           (8)
#define PPU_GPIO_9_IRQ           (9)
#define PPU_GPIO_10_IRQ          (10)
#define PPU_GPIO_11_IRQ          (11)
#define PPU_GPIO_12_IRQ          (12)
#define PPU_GPIO_13_IRQ          (13)
#define PPU_GPIO_14_IRQ          (14)
#define PPU_GPIO_15_IRQ          (15)
#define PPU_GPIO_16_IRQ          (16)
#define PPU_GPIO_17_IRQ          (17)
#define PPU_GPIO_18_IRQ          (18)
#define PPU_GPIO_19_IRQ          (19)
#define PPU_GPIO_20_IRQ          (20)
#define PPU_GPIO_21_IRQ          (21)
#define PPU_GPIO_22_IRQ          (22)
#define PPU_GPIO_23_IRQ          (23)
#define PPU_GPIO_24_IRQ          (24)
#define PPU_GPIO_25_IRQ          (25)
#define PPU_GPIO_26_IRQ          (26)
#define PPU_GPIO_27_IRQ          (27)
#define PPU_GPIO_28_IRQ          (28)
#define PPU_GPIO_29_IRQ          (29)
#define PPU_GPIO_30_IRQ          (30)
#define PPU_GPIO_31_IRQ          (31)

/* UART Configuration */
#define PPU_UART_0_LINECFG           0x1

/* GPIO Configuration */
#define PPU_GPIO_0_BASE_ADDR         0x70002000

/* Clock controller. */
#define PRCI_BASE_ADDR               0x44000000

/* Timer configuration */
#define RISCV_MTIME_BASE             0x70014000
#define RISCV_MTIMECMP_BASE          0x70014008

/* lib-c hooks required RAM defined variables */
#define RISCV_RAM_BASE               CONFIG_SRAM_BASE_ADDRESS
#define RISCV_RAM_SIZE               KB(CONFIG_SRAM_SIZE)

#endif /* __RISCV32_PPU_SOC_H_ */
