#ifndef S_TALK
#define S_TALK


#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "list.h"

#include "list.h"



// initialize all the lists
void start_chat();

// gets keyboard inputs
void* keyboard(void* item);

// sends message through socket
void* sender(void* item);

// outputs to screen
void* screen(void* item);

// receives from socket
void* receiver(void* item);

#endif