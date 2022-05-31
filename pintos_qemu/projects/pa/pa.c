#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "projects/pa/pa.h"

void run_patest(char **argv)
{   
    /// TODO: make your own test
    

    while (1) {
        void * user_palloc = palloc_get_multiple(PAL_USER,15);
        palloc_get_status(PAL_USER);
        palloc_free_multiple(user_palloc,15);
        palloc_get_status(PAL_USER);

        void * user_palloc2 = palloc_get_multiple(PAL_USER,17);
        palloc_get_status(PAL_USER);
        palloc_free_multiple(user_palloc2,17);
        palloc_get_status(PAL_USER);

        void * user_palloc3 = palloc_get_multiple(PAL_USER,4);
        palloc_get_status(PAL_USER);
        palloc_free_multiple(user_palloc3,4);
        palloc_get_status(PAL_USER);

        timer_msleep(1000);
//        palloc_get_multiple(0,25);
//       palloc_get_status(0);
//        timer_msleep(1000);
    }
}