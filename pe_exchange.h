#ifndef PE_EXCHANGE_H
#define PE_EXCHANGE_H

#include "pe_common.h"

#define LOG_PREFIX "[PEX]"

trader* traders_list;
product* orderbook;

int number_of_traders;
int number_of_products;
long int total_fees;
pid_t recently_disconnected_trader; // pid of trader that disconnected recently
pid_t present_trader;   // pid of trader whose sigusr1 is being processed

// Function prototypes
void read_products_file(const char* filename);
void initialize_traders(int argc, char** argv);
void free_all_traders();
void free_orders_list(order* head);
void free_orderbook();
void update_levels(order* order_list, int* level);
int check_valid_message(char* message, trader* t);
void print_orderbook();
int match_order(order* ord, order* order_list, int index);
void process_order(order* buyer, order* seller, int index, int new_order);
order* search_order(order* ord, order* order_list);
void process_signal();

// Signal handlers
void handle_sigusr1(int signum, siginfo_t* sig_info, void* context);
void handle_sigchld(int signum, siginfo_t* sig_info, void* context);

#endif
