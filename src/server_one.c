#include <stdio.h>
#include <unistd.h>
#include "net.h"
#include <string.h>

void serve(int c){

    char name[100];
    char reply[256];
    read_line (c,  name, sizeof(name));
    sleep(2);
    snprintf(reply, sizeof(reply), "Hello, %s!", name);
    write_line(c, reply, strlen(reply));
}

int main(void){
    int L = server_listen(8080);
    while(1){
        int c = server_accept(L);
        serve(c);

        close(c);        
    }

    return 0;

}

