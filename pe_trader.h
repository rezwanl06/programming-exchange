#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define MAX_BUY_LIMIT 1000

trader* auto_trader;

void auto_trader_signal_handler(int signum, siginfo_t* sig_info, void* context);

#endif
