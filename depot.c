#include "depot.h"

// Pointer to depot to be used for signal handling
Depot* depotCpy;
// Mutex lock to ensure mutual exclusion
pthread_mutex_t depotLock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char** argv) {
    Depot depot;
    depotCpy = &depot;
    init_depot(argc, argv, &depot);
    init_sig(&depot);
    start_server(&depot);
    return 0;
}

/*
 * Initialise the depot given the command line arguments
 */
void init_depot(int argc, char** argv, Depot* depot) {
    if (argc % 2 == 1) {
        exit_depot(BAD_ARG_NUM);
    }

    if (strcmp(argv[1], "") == 0 || contains_bad_char(argv[1])) {
        exit_depot(BAD_NAME);
    } else {
        depot->depotName = argv[1];
    }

    depot->numResources = (argc - 2) / 2;
    depot->resources = (Resource*)malloc(sizeof(Resource) * 
            depot->numResources);

    for (int i = 0; i < depot->numResources; i++) {
        char* err;
        char* name;
        if (strcmp(argv[2 * i + 2], "") == 0 || contains_bad_char
                (argv[2 * i + 2])) {
            exit_depot(BAD_NAME);
        } else {
            name = argv[2 * i + 2];
        }
        int quantity = strtoul(argv[2 * i + 3], &err, 10);
        if (strlen(argv[2 * i + 3]) == 0) {
            exit_depot(BAD_QUANTITY);
        }
        if (*err != '\0' || quantity < 0) {
            exit_depot(BAD_QUANTITY);
        }
        depot->resources[i] = (Resource){.quantity = quantity, 
                .resourceName = name};
    }
    depot->numNeighbours = 0;
    depot->numDeferredTasks = 0;
}

/*
 * Exit the depot with the relevent status
 */
void exit_depot(DepotStatus status) {
    const char* messages[] = {"",
            "Usage: 2310depot name {goods qty}\n",
            "Invalid name(s)\n",
            "Invalid quantity\n"};
    fputs(messages[status], stderr);
    exit(status);
}

/*
 * Print the current resources and neighbours to stdout
 */
void print_info(Depot* depot) {
    fprintf(stdout, "Goods:\n");
    qsort(depot->resources, depot->numResources, sizeof(Resource), 
            comp_resources);
    for (int i = 0; i < depot->numResources; i++) {
        if (depot->resources[i].quantity != 0) {
            fprintf(stdout, "%s %d\n", depot->resources[i].resourceName, 
                    depot->resources[i].quantity);
        }
    }
    fprintf(stdout, "Neighbours:\n");
    qsort(depot->neighbours, depot->numNeighbours, sizeof(Neighbour), 
            comp_neighbours);
    for (int i = 0; i < depot->numNeighbours; i++) {
        fprintf(stdout, "%s\n", depot->neighbours[i].neighbourName);
    }
    fflush(stdout);
}

/*
 * Initialises a socket for incoming connections
 */
void start_server(Depot* depot) {
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(LOCALHOST, 0, &hints, &ai);
    int serv = socket(AF_INET, SOCK_STREAM, 0);
    bind(serv, (struct sockaddr*)ai->ai_addr, sizeof(struct sockaddr));
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    getsockname(serv, (struct sockaddr*)&ad, &len);
    printf("%u\n", ntohs(ad.sin_port));
    fflush(stdout);
    int portLength = snprintf(NULL, 0, "%d", ntohs(ad.sin_port));
    depot->port = (char*)malloc(portLength + 1);
    snprintf(depot->port, portLength + 1, "%d", ntohs(ad.sin_port));
    listen(serv, SOMAXCONN);
    int connFd;
    while (connFd = accept(serv, 0, 0), connFd >= 0) {
        pthread_mutex_lock(&depotLock);
        Neighbour* neighbour = (Neighbour*)malloc(sizeof(Neighbour));
        neighbour->depot = depot;
        int connFdIn = dup(connFd);
        neighbour->toNeighbour = fdopen(connFd, "w");
        neighbour->fromNeighbour = fdopen(connFdIn, "r");
        char* imMessage = read_line(neighbour->fromNeighbour, 10);
        if (determine_message_type(imMessage) == IM) {
            add_neighbour(depot, neighbour, imMessage);
        } else {
            fclose(neighbour->toNeighbour);
            fclose(neighbour->fromNeighbour);
        }
        fprintf(neighbour->toNeighbour, "IM:%s:%s\n", depot->port, 
                depot->depotName);
        fflush(neighbour->toNeighbour);

        pthread_create(&neighbour->threadID, NULL, conn_handler, 
                (void*) neighbour);
        pthread_mutex_unlock(&depotLock);
    }
}

/*
 * Connection handler for each connection to the depot. Reads and writes
 * messages between depots.
 */
void* conn_handler(void* param) {
    pthread_mutex_lock(&depotLock);
    Neighbour* neighbour = (Neighbour*)param;
    pthread_mutex_unlock(&depotLock);
    while(1) {
        char* msg = read_line(neighbour->fromNeighbour, 10);
        pthread_mutex_lock(&depotLock);
        if (determine_message_type(msg) == DELIVER) {
            add_resource(neighbour->depot, msg);
        }
        if (determine_message_type(msg) == WITHDRAW) {
            withdraw_resource(neighbour->depot, msg);
        }
        if (determine_message_type(msg) == DEFER) {
            handle_defer_message(neighbour->depot, msg);
        }
        if (determine_message_type(msg) == EXECUTE) {
            handle_execute(neighbour->depot, msg);
        }
        if (determine_message_type(msg) == CONNECT) {
            handle_connect_message(neighbour->depot, msg);
        }
        if (determine_message_type(msg) == TRANSFER) {
            handle_transfer_message(neighbour->depot, msg);
        }
        pthread_mutex_unlock(&depotLock);
    }
    pthread_exit(NULL);
}

/*
 * Adds the depot as described by the 'IM' message as a neighbour
 */
void add_neighbour(Depot* depot, Neighbour* neighbour, char* imMessage) {
    char* port;
    char* name;
    if (!parse_im_message(imMessage, &port, &name)) {
        fclose(neighbour->toNeighbour);
        fclose(neighbour->fromNeighbour);
    }
    neighbour->port = port;
    neighbour->neighbourName = name;
    if (depot->numNeighbours == 0) {
        depot->numNeighbours++;
        depot->neighbours = (Neighbour*)malloc(depot->numNeighbours * 
                sizeof(Neighbour));
        depot->neighbours[depot->numNeighbours - 1] = *neighbour;
    } else {
        depot->numNeighbours++;
        depot->neighbours = (Neighbour*)realloc(depot->neighbours, 
                depot->numNeighbours * sizeof(Neighbour));
        depot->neighbours[depot->numNeighbours - 1] = *neighbour;
    }
}

/*
 * Adds the resource as described by the 'Deliver' message to the depots list
 * of resources
 */
void add_resource(Depot* depot, char* deliverMessage) {
    int quantity;
    char* name;
    if (!parse_deliver_withdraw_message(deliverMessage, &quantity, &name)) {
        return;
    }
    Resource resource = (Resource){quantity, name};
    int depotContains = 0;
    for (int i = 0; i < depot->numResources; i++) {
        if (strcmp(resource.resourceName, depot->resources[i]
                .resourceName) == 0) {
            depotContains = 1;
            depot->resources[i].quantity += resource.quantity;
            break;
        }
    }

    if (depotContains == 0) {
        depot->numResources++;
        depot->resources = (Resource*)realloc(depot->resources,
                depot->numResources * sizeof(Resource));
        depot->resources[depot->numResources - 1] = resource;
    }
}

/*
 * Withdraws the resource as described by the 'Withdraw' message from the
 * depot
 */
void withdraw_resource(Depot* depot, char* withdrawMessage) {
    int quantity;
    char* name;
    if (!parse_deliver_withdraw_message(withdrawMessage, &quantity, &name)) {
        return;
    }
    Resource resource = (Resource){quantity, name};
    int depotContains = 0;
    for (int i = 0; i < depot->numResources; i++) {
        if (strcmp(resource.resourceName, depot->resources[i].resourceName) 
                == 0) {
            depotContains = 1;
            depot->resources[i].quantity -= resource.quantity;
            break;
        }
    }
    if (depotContains == 0) {
        depot->numResources++;
        depot->resources = (Resource*)realloc(depot->resources, 
                depot->numResources * sizeof(Resource));
        depot->resources[depot->numResources - 1] = (Resource){.resourceName 
                = resource.resourceName, .quantity = 0 - resource.quantity};
    }
}

/*
 * Handles deferred messages by adding the task to a list of pending tasks
 */
void handle_defer_message(Depot* depot, char* deferMessage) {
    unsigned key;
    char* task;
    if (!parse_defer_message(deferMessage, &key, &task)) {
        return;
    }
    int containsKey = 0;
    for (int i = 0; i < depot->numDeferredTasks; i++) {
        if (depot->deferredTasks[i].key == key) {
            containsKey = 1;
            depot->deferredTasks[i].numTasks++;
            depot->deferredTasks[i].tasks = (char**)realloc
                    (depot->deferredTasks[i].tasks, depot->deferredTasks[i]
                    .numTasks * sizeof(DeferredTask));
            depot->deferredTasks[i].tasks[depot->deferredTasks[i].numTasks 
                    - 1] = task;
            break;
        }
    }
    if (containsKey == 0) {
        DeferredTask deferredTask;
        deferredTask.numTasks = 1;
        deferredTask.key = key;
        deferredTask.tasks = (char**)malloc(sizeof(char*));
        deferredTask.tasks[deferredTask.numTasks - 1] = task;\
        if (depot->numDeferredTasks == 0) {
            depot->numDeferredTasks++;
            depot->deferredTasks = (DeferredTask*)malloc
                    (depot->numDeferredTasks * sizeof(DeferredTask));
        } else {
            depot->numDeferredTasks++;
            depot->deferredTasks = (DeferredTask*)realloc
                    (depot->deferredTasks, depot->numDeferredTasks * 
                    sizeof(DeferredTask));
        }
        depot->deferredTasks[depot->numDeferredTasks - 1] = deferredTask;
    }
}

/*
 * Executes the deferred task as described by the 'Execute' message
 */
void handle_execute(Depot* depot, char* executeMessage) {
    unsigned key;
    if (!parse_execute_message(executeMessage, &key)) {
        return;
    }
    for (int i = 0; i < depot->numDeferredTasks; i++) {
        if (depot->deferredTasks[i].key == key) {
            for (int j = 0; j < depot->deferredTasks[i].numTasks; j++) {
                if (determine_message_type(depot->deferredTasks[i].tasks[j]) 
                        == DELIVER) {
                    add_resource(depot, depot->deferredTasks[i].tasks[j]);
                }
                if (determine_message_type(depot->deferredTasks[i].tasks[j]) 
                        == WITHDRAW) {
                    withdraw_resource(depot, depot->deferredTasks[i].tasks[j]);
                }
                if (determine_message_type(depot->deferredTasks[i].tasks[j]) 
                        == TRANSFER) {
                    handle_transfer_message(depot, depot->deferredTasks[i].
                            tasks[j]);
                }
            }
            depot->deferredTasks[i].numTasks = 0;
            depot->deferredTasks[i].tasks = NULL;

        }
    }
}

/*
 * Handles incoming connect messages by connecting to the given depot
 */
void handle_connect_message(Depot* depot, char* connectMessage) {
    char* port;
    if (!parse_connect_message(connectMessage, &port)) {
        return;
    }
    bool containsPort = false;
    for (int i = 0; i < depot->numNeighbours; i++) {
        if (strcmp(port, depot->neighbours[i].port) == 0) {
            containsPort = true;
        }
    }
    if (containsPort == true) {
        return;
    }
    struct addrinfo* ai = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(LOCALHOST, port, &hints, &ai);
    int connFd = socket(AF_INET, SOCK_STREAM, 0);
    connect(connFd, (struct sockaddr*)ai->ai_addr, sizeof(struct sockaddr));

    Neighbour* neighbour = (Neighbour*)malloc(sizeof(Neighbour));
    neighbour->depot = depot;
    int connFdIn = dup(connFd);
    neighbour->toNeighbour = fdopen(connFd, "w");
    neighbour->fromNeighbour = fdopen(connFdIn, "r");
    fprintf(neighbour->toNeighbour, "IM:%s:%s\n", depot->port, 
            depot->depotName);
    fflush(neighbour->toNeighbour);
    char* imMessage = read_line(neighbour->fromNeighbour, 10);
    if (determine_message_type(imMessage) == IM) {
        add_neighbour(depot, neighbour, imMessage);
    } else {
        fclose(neighbour->toNeighbour);
        fclose(neighbour->fromNeighbour);
    }
    pthread_create(&neighbour->threadID, NULL, conn_handler, 
            (void*)neighbour);
}

/*
 * Transfer goods from the current depot to the depot as described by
 * the 'Transfer' message
 */
void handle_transfer_message(Depot* depot, char* transferMessage) {
    int quantity;
    char* name;
    char* dest;
    if (!parse_transfer_message(transferMessage, &quantity, &name, &dest)) {
        return;
    }
    int quantityLength = snprintf(NULL, 0, "%d", quantity);
    char* withdrawMessage = (char*)malloc(strlen(name) + quantityLength + 11);
    snprintf(withdrawMessage, strlen(name) + quantityLength + 11, 
            "Withdraw:%d:%s\n", quantity, name);
    withdraw_resource(depot, withdrawMessage);
    for (int i = 0; i < depot->numNeighbours; i++) {
        if (strcmp(dest, depot->neighbours[i].neighbourName) == 0) {
            fprintf(depot->neighbours[i].toNeighbour, "Deliver:%d:%s\n", 
                    quantity, name);
            fflush(depot->neighbours[i].toNeighbour);
        }
    }
}
                    
/*
 * Extract the relevent information from both the deliver and withdraw
 * message and return whether or not the message is valid
 */
bool parse_deliver_withdraw_message(char* message, int* quantity, 
        char** name) {
    if (strtok(message, ":") == NULL) {
        return false;
    }
    char* err;
    char* quantityToParse;
    if (!(quantityToParse = strtok(NULL, ":"))) {
        return false;
    }
    int quantityMessage = strtoul(quantityToParse, &err, 10);
    if (strlen(quantityToParse) == 0 || *err != '\0') {
        return false;
    }
    if (quantityMessage <= 0) {
        return false;
    }
    char* nameMessage;
    if (!(nameMessage = strtok(NULL, ":"))) {
        return false;
    }
    if (strtok(NULL, ":") != NULL) {
        return false;
    }
    *quantity = quantityMessage;
    *name = nameMessage;
    return true;
}

/*
 * Extract the relevent information from the IM message and return whether
 * or not the message is valid
 */
bool parse_im_message(char* imMessage, char** port, char** name) {
    if (strtok(imMessage, ":") == NULL) {
        return false;
    }
    char* portMessage;
    if (!(portMessage = strtok(NULL, ":"))) {
        return false;
    }
    char* nameMessage;
    if (!(nameMessage = strtok(NULL, ":"))) {
        return false;
    }
    if (strtok(NULL, ":") != NULL) {
        return false;
    }
    *port = portMessage;
    *name = nameMessage;
    return true;
}

/*
 * Extract the relevent information from the deferred message and return
 * whether or not the message is valid
 */
bool parse_defer_message(char* deferMessage, unsigned* key, char** task) {
    if (strtok(deferMessage, ":") == NULL) {
        return false;
    }
    char* err;
    char* keyToParse;
    if (!(keyToParse = strtok(NULL, ":"))) {
        return false;
    }
    unsigned keyMessage = strtoul(keyToParse, &err, 10);
    if (strlen(keyToParse) == 0 || *err != '\0') {
        return false;
    }
    char* taskMessage;
    if (!(taskMessage = strtok(NULL, ""))) {
        return false;
    }
    *key = keyMessage;
    *task = taskMessage;
    return true;
}

/*
 * Extract the relevent information from the execute message and return
 * whether or not the message is valid
 */
bool parse_execute_message(char* executeMessage, unsigned* key) {
    if (strtok(executeMessage, ":") == NULL) {
        return false;
    }
    char* err;
    char* keyToParse;
    if (!(keyToParse = strtok(NULL, ":"))) {
        return false;
    }
    unsigned keyMessage = strtoul(keyToParse, &err, 10);
    if (strlen(keyToParse) == 0 || *err != '\0') {
        return false;
    }
    if (strtok(NULL, ":") != NULL) {
        return false;
    }
    *key = keyMessage;
    return true;
}

/*
 * Extract the relevent information from the connect message and return
 * whether or not the message is valid
 */
bool parse_connect_message(char* connectMessage, char** port) {
    if (strtok(connectMessage, ":") == NULL) {
        return false;
    }
    char* portMessage;
    if (!(portMessage = strtok(NULL, ":"))) {
        return false;
    }
    if (strtok(NULL, ":") != NULL) {
        return false;
    }
    *port = portMessage;
    return true;
}

/*
 * Extract the relevent information from the transfer message and return
 * whether or not the message is valid
 */
bool parse_transfer_message(char* transferMessage, int* quantity, char** name,
        char** dest) {
    if (strtok(transferMessage, ":") == NULL) {
        return false;
    }
    char* err;
    char* quantityToParse;
    if (!(quantityToParse = strtok(NULL, ":"))) {
        return false;
    }
    int quantityMessage = strtoul(quantityToParse, &err, 10);
    if (strlen(quantityToParse) == 0 || *err != '\0') {
        return false;
    }
    char* nameMessage;
    if (!(nameMessage = strtok(NULL, ":"))) {
        return false;
    }
    char* destMessage;
    if (!(destMessage = strtok(NULL, ":"))) {
        return false;
    }
    if (strtok(NULL, ":") != NULL) {
        return false;
    }
    *quantity = quantityMessage;
    *name = nameMessage;
    *dest = destMessage;
    return true;
}

/*
 * Initialises the thread used for handling signals
 */
void init_sig(Depot* depot) {
    pthread_t tid;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, 0);
    pthread_create(&tid, 0, handle_signals, 0);
}

/*
 * Signal handler for catching SIGHUP and SIGPIPE
 */
void* handle_signals(void* arg) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    int sigNum;
    while (!sigwait(&set, &sigNum)) {
        if (sigNum == 1) {
            pthread_mutex_lock(&depotLock);
            print_info(depotCpy);
            pthread_mutex_unlock(&depotLock);
        }
    }
    return 0;
}

/*
 * Custom comparator for comparing resources
 */
int comp_resources(const void* v1, const void* v2) {
    Resource x1 = *((Resource*)v1);
    Resource x2 = *((Resource*)v2);
    return strcmp(x1.resourceName, x2.resourceName);
}

/*
 * Custom comparator for comparing neighbours
 */
int comp_neighbours(const void* v1, const void* v2) {
    Neighbour x1 = *((Neighbour*)v1);
    Neighbour x2 = *((Neighbour*)v2);
    return strcmp(x1.neighbourName, x2.neighbourName);
}

/*
 * Reads up until newline or EOF from the given file and stores as a string
 */
char* read_line(FILE* file, size_t size) {
    char* strResult;
    int ch;

    size_t length = 0;
    strResult = (char*)malloc(sizeof(char) * size);
    if (!strResult) {
        return strResult;
    }
    while(EOF != (ch = fgetc(file)) && ch != '\n') {
        strResult[length++] = ch;
        if (length == size) {
            strResult = (char*)realloc(strResult, sizeof(char) * (size += 16));
            if (!strResult) {
                return strResult;
            }
        }
    }
    strResult[length++] = '\0';
    return (char*)realloc(strResult, sizeof(char) * length);
}

/*
 * Check for whether or not a string contains illegal characters
 */
bool contains_bad_char(char* argument) {
    if (strpbrk(argument, " \n\r:")) {
        return true;
    }
    return false;
}

/*
 * Determines the message type of the given message
 */
MessageType determine_message_type(char* message) {
    if (strncmp(message, "Connect", 7) == 0) {
        return CONNECT;
    } else if (strncmp(message, "IM", 2) == 0) {
        return IM;
    } else if (strncmp(message, "Deliver", 7) == 0) {
        return DELIVER;
    } else if (strncmp(message, "Withdraw", 8) == 0) {
        return WITHDRAW;
    } else if (strncmp(message, "Defer", 5) == 0) {
        return DEFER;
    } else if (strncmp(message, "Execute", 7) == 0) {
        return EXECUTE;
    } else if (strncmp(message, "Transfer", 8) == 0) {
        return TRANSFER;
    }
    return INVALID;
}
