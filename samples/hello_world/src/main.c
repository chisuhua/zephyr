/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

// extern void* get_gem5api_vector(char *api);

typedef void (*zephyr_api_mem_access)(bool, uint32_t, bool, long int , bool);
// typedef void* (*gem5api_vector)[];
void* (*gem5api_vector)[10];

void main(void)
{

    // gem5api_vector = (gem5api_vector)get_gem5api_vector(NULL);
    zephyr_api_mem_access mem_access = (zephyr_api_mem_access)(*gem5api_vector)[0];
    // zephyr_api_mem_access mem_access2 = (zephyr_api_mem_access)(*gem5api_vector)[1];

	printk("Hello World Start! %s\n", CONFIG_BOARD);
	k_sleep(K_MSEC(100));
    int cnt = 0;
    while(cnt < 10) {
        cnt++;
	    printk("%d:Hello World!\n", cnt);
        mem_access(false, 1000, false, 0x10000 + cnt, false);
        // mem_access2(false, 1000, false, 0x10000, false);
    }
    posix_exit(0);
	// printf("Hello World in printf! %s\n", CONFIG_BOARD);
}
