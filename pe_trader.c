#include "pe_trader.h"

volatile sig_atomic_t is_busy = 0;  // signal handler flag
volatile sig_atomic_t accepted = 0;   // Flag for waiting for accepted message
char message[BUFFER_SIZE] = {0};

void auto_trader_signal_handler(int signum, siginfo_t* sig_info, void* context) {
    if (auto_trader -> status == 0) {   // activate auto trader
        auto_trader -> status = 1;
        read(auto_trader -> exchange_fd, message, strlen(MARKET_OPEN));
        //printf("%s\n", message);
        return;
    }
    if (! is_busy) {    // if not busy, then set the is_busy flag to 1
        is_busy = 1;
    } else if (! accepted) {
        accepted = 1;
    }
}



int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        return 1;
    }

    // create auto trader
    auto_trader = (trader*) malloc(sizeof(trader));
    auto_trader -> order_id = 0;
    auto_trader -> status = 0;  // Start as disconnected
    auto_trader -> trader_id = atoi(argv[1]);

    // register signal handler
    struct sigaction auto_trader_sig;
    memset(&auto_trader_sig, 0, sizeof(auto_trader_sig));
    auto_trader_sig.sa_sigaction = auto_trader_signal_handler;
    sigaction(SIGUSR1, &auto_trader_sig, NULL);

    // connect to named pipes
    char exchange[BUFFER_SIZE];
    char trader[BUFFER_SIZE];

    sprintf(exchange, "%s%s", EXCHANGE_FIFO, argv[1]);
    sprintf(trader, "%s%s", TRADER_FIFO, argv[1]);

    auto_trader -> exchange_fd = open(exchange, O_RDONLY);
    if (auto_trader -> exchange_fd < 0) {   // could not open pipe
        printf("%s open() failed!", exchange);
        exit(1);
    }
    auto_trader -> trader_fd = open(trader, O_WRONLY);
    if (auto_trader -> trader_fd < 0) {   // could not open pipe
        printf("%s open() failed!", trader);
        exit(1);
    }

    
    // event loop:
    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)
    while (1) {
        if (! is_busy) {    // wait for signal
            pause();
        } else {
            // read message from pipe
            read(auto_trader -> exchange_fd, message, BUFFER_SIZE);
            //printf("%s\n", message);

            // Decode the message
            char market[SMALL_BUFFER_SIZE] = {0};
            char command[SMALL_BUFFER_SIZE] = {0};
            char product[SMALL_BUFFER_SIZE] = {0};
            int amount, cost, x;
            x = amount = cost = 0;

            x = sscanf(message, "%s %s %s %d %d;", market, command, product, &amount, &cost);

            if (x == 5) {   // buy or sell order
                if (amount >= MAX_BUY_LIMIT) {  // quantity exceeds limit, disconnect
                    break;
                }
                if (strcmp(command, "SELL") == 0) { // sell order
                    char response[BUFFER_SIZE] = {0};
                    sprintf(response, "BUY %d %s %d %d;", auto_trader -> order_id, product, amount, cost);

                    //printf("%s\n", response);

                    ++(auto_trader -> order_id);    // Increment order id
                    
                    memset(message, 0, BUFFER_SIZE);    // clear message buffer
                    
                    // Write to pipe and send sigusr1 to exchange
                    while(write(auto_trader -> trader_fd, response, strlen(response)) == -1);

                    // Keep sending SIGUSR1 and wait for accepted message
                    while (1) {
                        if (! accepted) {
                            kill(getppid(), SIGUSR1);
                            sleep(2);
                        } else {
                            accepted = 0;
                            break;
                        }
                    }
                    
                    // Accepted
                    read(auto_trader -> exchange_fd, message, BUFFER_SIZE);

                    memset(message, 0, BUFFER_SIZE);
                    is_busy = 0;    // task finished, no longer busy
                } else {
                    is_busy = 0;
                }

            } else if ((x == 2) || (x == 3)) {  // fill or accept order
                    memset(message, 0, BUFFER_SIZE);    // clear message buffer
                    is_busy = 0;
            }
        }
    }

    // close pipes
    close(auto_trader -> exchange_fd);
    close(auto_trader -> trader_fd);
    unlink(exchange);
    unlink(trader);

    free(auto_trader);

    return 0;
    
}
