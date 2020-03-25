#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define LOCALHOST "127.0.0.1"


typedef struct Neighbour Neighbour;
typedef struct Depot Depot;
typedef struct Resource Resource;
typedef struct DeferredTask DeferredTask;

/*
 * Exit statuses for the depot
 */
typedef enum {
    BAD_ARG_NUM = 1,
    BAD_NAME = 2,
    BAD_QUANTITY = 3
} DepotStatus;

/*
 * Types of message that can be sent between depots
 */
typedef enum {
    CONNECT = 1,
    IM = 2,
    DELIVER = 3,
    WITHDRAW = 4,
    TRANSFER = 5,
    DEFER = 6,
    EXECUTE = 7,
    INVALID = 8
} MessageType;

/*
 * Stores details of a neighbour
 */
struct Neighbour {
    char* neighbourName;
    FILE* toNeighbour;
    FILE* fromNeighbour;
    Depot* depot;
    pthread_t threadID;
    char* port;
};

/*
 * Stores details of a resource
 */
struct Resource {
    int quantity;
    char* resourceName;
};

/*
 * Stores details of a depot
 */
struct Depot {
    char* depotName;

    char* port;
    
    int numResources;
    Resource* resources;

    int numNeighbours;
    Neighbour* neighbours;

    int numDeferredTasks;
    DeferredTask* deferredTasks;
};

/*
 * Stores details of a deferred task   
 */
struct DeferredTask {
    unsigned key;
    int numTasks;
    char** tasks;
};

/* Functions for initialising, exiting and printing depot */
void init_depot(int argc, char** argv, Depot* depot);
void exit_depot(DepotStatus status);
void print_info(Depot* depot);

/* Functions for starting server and handling connections */
void start_server(Depot* depot);
void* conn_handler(void* param);

/* Functions for handling messages */
void add_neighbour(Depot* depot, Neighbour* neighbour, char* imMessage);
void add_resource(Depot* depot, char* deliverMessage);
void withdraw_resource(Depot* depot, char* withdrawMessage);
void handle_defer_message(Depot* depot, char* deferMessage);
void handle_execute(Depot* depot, char* executeMessage);
void handle_connect_message(Depot* depot, char* connectMessage);
void handle_transfer_message(Depot* depot, char* transferMessage);

/* Functions for parsing information from messages */
bool parse_deliver_withdraw_message(char* message, int* quantity, 
        char** name); 
bool parse_im_message(char* imMessage, char** port, char** name);
bool parse_defer_message(char* deferMessage, unsigned* key, char** task);
bool parse_execute_message(char* executeMessage, unsigned* key);
bool parse_connect_message(char* connectMessage, char** port);
bool parse_transfer_message(char* transferMessage, int* quantity, char** name,
        char** dest);

/* Signal handling functions */
void init_sig(Depot* depot);
void* handle_signals(void* arg);

/* Comparator functions for sorting */
int comp_resources(const void* v1, const void* v2);
int comp_neighbours(const void* v1, const void* v2);

/* Helper functions */
char* read_line(FILE* file, size_t size);
bool contains_bad_char(char* argument);
MessageType determine_message_type(char* message);

