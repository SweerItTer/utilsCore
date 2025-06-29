// #define Debug 
// #define Info 
#define Error(fmt, ...) \
    printf("\033[31m[ERROR] %s:%d %s(): " fmt "\033[0m\n", \
           __FILE__, __LINE__, __func__, ##__VA_ARGS__)



