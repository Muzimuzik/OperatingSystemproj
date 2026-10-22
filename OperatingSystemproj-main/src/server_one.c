#include <stdio.h>
#include<unistd.h>
#include "net.h"

int main(void){
    int L = server_listen(5000);
    while(1){
        int c = server_accept(L);
        serve(c);

    }
}
