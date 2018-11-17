#include "kvstore.h"
int main(int argc, char **argv)
{
   if(argv[1]&&argv[2]){
            if(rename_key(argv[1], argv[2]) < 0)
                  printf("Rename error\n");
    }else{
            printf("Usage: %s <key>\n", argv[0]);
            exit(-1);
    }
    return 0;
}
