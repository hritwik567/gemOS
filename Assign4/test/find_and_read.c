#include "kvstore.h"
main()
{
    int total=2000, ctr;
    for(ctr=1; ctr<=total; ++ctr){
         char value[32];
        char key[32];
        sprintf(key, "CS330###%d", ctr);
        if(get_key(key, value) < 0)
             printf("Get error\n");
            printf("For key = %s value=%s\n", key, value);
    }
}
