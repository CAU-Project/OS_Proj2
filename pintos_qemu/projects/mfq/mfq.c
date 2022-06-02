#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "devices/timer.h"
#include "projects/mfq/mfq.h"


void test_loop(void *aux)
{
    tid_t tid = thread_tid();
    /// TODO: make your own test
    while(1){
        int i =2;
        printf("[%s] test_loop in\n",thread_name());
        for(;;){
            if(2147483647 % i == 0){
                printf("find the prime");
                break;
            }
            i++;
            if(i %100000000==0){
                timer_msleep(300);
            }
        }
        timer_msleep(1000);
    }
}

void run_mfqtest(char **argv)
{   
    printf("[run_mfqtest] argv[1] : %s\n",argv[1]);
    int cnt;
	char *token, *save_ptr;

    enum intr_level old_level;
    old_level = intr_disable ();

    /// TODO: make your own test
	cnt = 0;
	for (token = strtok_r (argv[1], ":", &save_ptr); token != NULL; 
		token = strtok_r (NULL, ":", &save_ptr)) {

        char *subtoken, *save_ptr2, *name;
        int priority;

        subtoken = strtok_r (token, ".", &save_ptr2);
        name = &subtoken[1];
        printf("[run_mfqtest] name : %s\n", name);

        subtoken = strtok_r (NULL, ".", &save_ptr2);
        priority = atoi(subtoken);

        // you can create threads here
        // printf("[run_mfqtest] thread name: %s\n",name); 
        thread_create(name, priority, test_loop, NULL);

		cnt++;
	}
    intr_set_level (old_level);
    
    while (1) {
        timer_msleep(1000);
    }
}