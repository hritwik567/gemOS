#include "kvstore.h"
main()
{
    int total=10000, ctr;
    for(ctr=1; ctr<=total; ++ctr){
        char value[32];
        char key[32];
        sprintf(key, "CS330###%d", ctr);
        sprintf(value, "I am %d. Registered to CS330.", ctr);
        // for(int i=0;i<35*(1<<12);i++){
        //     value[i] = 'A';
        // }
        if(put_key(key, value, 32) < 0)
             printf("Create error\n");
    }
}