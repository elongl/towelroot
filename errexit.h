#define ERR_EXIT(msg)       \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    }
