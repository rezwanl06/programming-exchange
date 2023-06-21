#ifndef PE_COMMON_H
#define PE_COMMON_H

#define _POSIX_SOURCE
#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define FIFO_EXCHANGE "/tmp/pe_exchange_"
#define FIFO_TRADER "/tmp/pe_trader_"
#define FEE_PERCENTAGE 100
#define LIMIT 9999999
#define BUFFER_SIZE 100
#define SMALL_BUFFER_SIZE 20
#define PRODUCT_BUFFER_SIZE 17
#define MARKET_OPEN "MARKET OPEN;"
#define EXCHANGE_FIFO "/tmp/pe_exchange_"
#define TRADER_FIFO "/tmp/pe_trader_"
#define ACCEPT "ACCEPTED"
#define ORDERBOOK "\t--ORDERBOOK--"
#define POSITIONS "\t--POSITIONS--"
#define INVALID "INVALID;"
#define FILL "FILL"
#define AMEND "AMENDED"
#define CANCEL "CANCELLED"


typedef struct trader { // Struct to store traders
    char trader_binary[BUFFER_SIZE];
    int order_id;
    int trader_id;
    int trader_fd;
    int exchange_fd;
    pid_t pid;
    int status; // Determines whether trader is active or disconnected - 1 if active, 0 if disconnected
    long int* profits;  // Calculate profit for each product
    int* quantity;  // Calculate quantity for each product
    struct trader* next;
    struct trader* prev;
    struct order* order_list;
    long int* values;
} trader;

typedef struct order {
    char product_name[PRODUCT_BUFFER_SIZE];
    int order_type; // 0 for buy, 1 for sell
    int cost;
    int amount;
    int trader_id;
    pid_t trader_pid;   // to send signal when matching orders
    int order_id;
    struct order* next;
    struct order* prev;
} order;


typedef struct product {
    char product_name[PRODUCT_BUFFER_SIZE];
    int index;
    int buy_level;
    int sell_level;
    order* buy_orders;
    order* sell_orders;
} product;


#endif
