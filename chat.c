
#include "chat.h"

/*
--- References: ---
https://www.quora.com/Why-is-buffer-size-set-in-a-C-program
https://stackoverflow.com/questions/1662909/undefined-reference-to-pthread-create-in-linux
https://stackoverflow.com/a/21120806/7586504
https://www.youtube.com/watch?v=eQOaaDA92SI
https://www.youtube.com/watch?v=P6Z5K8zmEmc
https://stackoverflow.com/a/26339161/7586504
https://stackoverflow.com/a/4760758/7586504
https://man7.org/linux/man-pages/man3/getaddrinfo.3.html
https://www.youtube.com/watch?v=esXw4bdaZkc
*/


#define MAX_BUFFER_LEN  1024
#define MAX_LIST_COUNT 100
static const char END_OF_PROGRAM = '!';


// to signal program has ended
static bool terminateChat;
// socket id to get messages on
static int socketId;        

// stores information about who it was that sent the packet
static struct sockaddr_in clientAddress;

static struct List* receiveList;
static struct List* sendList;

// our 4 threads
static pthread_t keyboardThread;
static pthread_t sendThread;
static pthread_t displayThread;
static pthread_t receiveThread;


// condition variables for synchronization
static pthread_cond_t sendCond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t receiveCond = PTHREAD_COND_INITIALIZER;

// mutexes solve the critical section problem
static pthread_mutex_t sendMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t receiveMutex = PTHREAD_MUTEX_INITIALIZER;


//// -------- Helper function -------- ////
// check if another user has ended the chat
static bool isChatTerminated(){
    if(terminateChat){
        return true;
    }
    return false;
}

// check if the message contains the symbol !
static bool checkMessageForEnd(char* buffer){
    if(buffer[0] == END_OF_PROGRAM && buffer[1] == '\n'){
        return true;
    }
    return false;
}


// wait for user inputs from the keyborad
void* keyboardInput(void* arg){
	char* buffer = (char*) malloc(MAX_BUFFER_LEN * sizeof(char));
	while(true){
        if(isChatTerminated()){
            break;
        }

		// fgets reads inputs from stream and stores it in buffer
		if(fgets(buffer, MAX_BUFFER_LEN, stdin) == NULL){
            free(buffer);
            fputs("[ERROR]: failed to get keyboard input!", stderr);
            exit(1);
        }

		// entering critical section
		pthread_mutex_lock(&sendMutex);

		// if send list is full, wait untill there is space
        if(List_count(sendList) == MAX_LIST_COUNT){
            // make the thread wait
            pthread_cond_wait(&sendCond, &sendMutex);
        }
        pthread_mutex_unlock(&sendMutex); 

        pthread_mutex_lock(&sendMutex);
		List_append(sendList, buffer);

        if(List_count(sendList) > 0) {
            // signal that the send list is not empty
            pthread_cond_signal(&sendCond);
        }

		// end of critical section
        pthread_mutex_unlock(&sendMutex);       

        // check if message contains the symbol !
        if(checkMessageForEnd(buffer)){
            break;
        }
	}
    pthread_exit(NULL);
}


// send message over network through the socket using UDP
void* sendMessage(void* arg){
    char* buffer;
    while(true){
        pthread_mutex_lock(&sendMutex);
        if(List_count(sendList) == 0){
            // wait until there is a message in the list
            pthread_cond_wait(&sendCond, &sendMutex);
        }
        pthread_mutex_unlock(&sendMutex);
        
        pthread_mutex_lock(&sendMutex);
        buffer = (char*) List_trim(sendList);
        if(List_count(sendList) > 0){
            // signal threads that there is a message in the list
            pthread_cond_signal(&sendCond); 
        }
        pthread_mutex_unlock(&sendMutex); 

        // send a reply to the client
        if (sendto(socketId, buffer, MAX_BUFFER_LEN, 0, (struct sockaddr *) &clientAddress, 
                                                        sizeof(struct sockaddr_in)) == -1){
            fputs("[ERROR]: cannot send message\n", stderr);
            close(socketId);
            pthread_cond_signal(&receiveCond); 
            pthread_cancel(receiveThread);
            free(buffer);
            break;
        }
        
        if(checkMessageForEnd(buffer) || isChatTerminated()){
            terminateChat = true;
            pthread_cond_signal(&receiveCond); 
            pthread_cancel(receiveThread);
            free(buffer);
            close(socketId);
            break;
        }
    }
    pthread_exit(NULL);
}

// // display data to the user screen
void* displayScreen(void* arg){
    char* buffer;
    while(true){
        // lock the mutex because we are accessing the critical section
        pthread_mutex_lock(&receiveMutex);
        
        if(List_count(receiveList) == 0){
            pthread_cond_wait(&receiveCond, &receiveMutex);
        }
        
        buffer = (char*) List_trim(receiveList); 
        pthread_mutex_unlock(&receiveMutex); 

        pthread_mutex_lock(&receiveMutex);
        // signal that the receive list is not empty
        if(List_count(receiveList) > 0){
            pthread_cond_signal(&receiveCond);
        } 
        pthread_mutex_unlock(&receiveMutex); 

        if(isChatTerminated()){
            break;
        }

        if(checkMessageForEnd(buffer)){
            terminateChat = true;
            break;
        }
        fputs("Guest: ", stdout);
        fputs(buffer, stdout);
    }
    pthread_cancel(sendThread);
    pthread_cancel(keyboardThread);
    pthread_exit(NULL);
}


void* receiveMessage(void* arg){    
    socklen_t socklen = sizeof(clientAddress);
    
    char* buffer[MAX_BUFFER_LEN];
    char* message;
    while(true){
        ssize_t size;
        if((size = recvfrom(socketId, buffer, MAX_BUFFER_LEN, 0, 
                (struct sockaddr*) &clientAddress, &socklen)) > 0){

            pthread_mutex_lock(&receiveMutex); 
            
            // check if list is full, if yes then wait
            if(List_count(receiveList) == MAX_LIST_COUNT){
                pthread_cond_wait(&receiveCond, &receiveMutex);
            }
            pthread_mutex_unlock(&receiveMutex); 

            pthread_mutex_lock(&receiveMutex); 
            message = malloc(sizeof(char) * size);
            memcpy(message, buffer, size);
            pthread_mutex_unlock(&receiveMutex);

            pthread_mutex_lock(&receiveMutex); 
            List_append(receiveList, message); 

            if(List_count(receiveList) < MAX_LIST_COUNT){
                // signal that there is space to accept a new message
                pthread_cond_signal(&receiveCond);
            }
        
            pthread_mutex_unlock(&receiveMutex); 
            
            if(isChatTerminated()){
                free(message);
                break;
            }

            if(checkMessageForEnd(message)){
                fputs("[INFO]: Your guest ended the chat!\n", stdout);
                break;
            }            
        }
    }
    pthread_cancel(receiveThread);       
    close(socketId);
    pthread_exit(NULL);
}


//// -------- Helper functions -------- ////

// encapsulate function details from client code
static int setupConnection(char* argv[]){
    terminateChat = false;
    char* port = argv[1];

    // specify an address for the socket
    struct sockaddr_in serverAddress; 
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(atoi(port));
    memset(&serverAddress.sin_zero, 0, 8);

    // create a socket for UDP and bind the socket to our port
    socketId = socket(AF_INET, SOCK_DGRAM, 0);
    bind(socketId, (struct sockaddr *) &serverAddress, sizeof(struct sockaddr_in));

    // setup client address info
    struct hostent *addressInfo = gethostbyname(argv[2]);
    int clientPort = atoi(argv[3]);
    clientAddress.sin_family = AF_INET;
    clientAddress.sin_port = ntohs(clientPort);
    clientAddress.sin_addr = *(struct in_addr*)addressInfo->h_addr;
    memset(&clientAddress.sin_zero, '\0', 8);

    return 0;
}

static void createLists(){
    sendList = List_create();
    receiveList = List_create();
}

static void createThreads(){
    pthread_create(&keyboardThread, NULL, keyboardInput, NULL);  
    pthread_create(&sendThread, NULL, sendMessage, NULL);
    pthread_create(&displayThread, NULL, displayScreen, NULL);
    pthread_create(&receiveThread, NULL, receiveMessage, NULL);
}

static void joinThreads(){
    pthread_join(keyboardThread, NULL);
    pthread_join(sendThread, NULL);
    pthread_join(displayThread, NULL);
    pthread_join(receiveThread, NULL);
}

static void cleanUpMemory(){
    printf("[INFO]: Chat Terminated.\n");

    List_free(sendList, NULL);             
    pthread_mutex_destroy(&sendMutex);     
    List_free(receiveList, NULL);          
    pthread_mutex_destroy(&receiveMutex);   

    pthread_cond_destroy(&sendCond);        
    pthread_cond_destroy(&receiveCond);    

    close(socketId);
    pthread_exit(NULL); 
}

int main(int argc, char* argv[]){
    if(setupConnection(argv) != 0){
        printf("[ERROR]: setting up connection failed!\n");
    }
    printf("[INFO]: Starting Chat.\n");
    createLists();
    createThreads();
    joinThreads();
    cleanUpMemory();  
    exit(1);
}
