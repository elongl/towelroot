#define errExit(msg)        \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    }