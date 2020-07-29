/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

// extern void* get_gem5api_vector(char *api);

typedef uint32_t (*zephyr_api_mem_read)(uint64_t, bool/*=false*/);
typedef void (*zephyr_api_mem_write)(uint64_t, uint32_t, bool/*=false*/);
typedef void (*zephyr_api_exit_sim)();
// typedef void* (*gem5api_vector)[];
void* (*gem5api_vector)[10];

void main(void)
{
zephyr_api_mem_read mem_read = (zephyr_api_mem_read)(*gem5api_vector)[0];
zephyr_api_mem_write mem_write = (zephyr_api_mem_write)(*gem5api_vector)[1];
zephyr_api_exit_sim exit_sim = (zephyr_api_exit_sim)(*gem5api_vector)[2];


    // gem5api_vector = (gem5api_vector)get_gem5api_vector(NULL);

	printk("Hello World Start! %s\n", CONFIG_BOARD);
	k_sleep(K_MSEC(100));
    int cnt = 0;
    while(cnt < 10) {
        cnt++;
	    int32_t data = k_uptime_get_32() * cnt;
        // int32_t data = random(100);
        int64_t addr = 1000 + cnt*4;
	    printk("%d:Write data %x to addr %lx !\n", cnt, data, addr);
        mem_write(addr, data, false);
    }

    cnt = 0;
    while(cnt < 10) {
        cnt++;
        int64_t addr = 1000 + cnt*4;
        int32_t data = mem_read(addr, false);
	    printk("%d:Read the data %x from addr %lx\n", cnt, data, addr);
    }

    exit_sim();
    // posix_exit(0);
	// printf("Hello World in printf! %s\n", CONFIG_BOARD);
}
