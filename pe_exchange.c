/**
 * comp2017 - assignment 3
 * <your name> Rezwanul Mustafa
 * <your unikey> rmus4094
 */

#include "pe_exchange.h"

volatile sig_atomic_t trader_disconnected = 0;
volatile sig_atomic_t is_busy = 0;

// sigusr1 handler
void handle_sigusr1(int signum, siginfo_t* sig_info, void* context) {
	if (! is_busy) {
		is_busy = 1;
		present_trader = sig_info -> si_pid;
	}
}

// sigchld handler
void handle_sigchld(int signum, siginfo_t* sig_info, void* context) {
	if (! trader_disconnected) {
		trader_disconnected = 1;
		recently_disconnected_trader = sig_info -> si_pid;
	}
}


// Function to free all dynamic memory associated with traders
void free_all_traders() {
	trader* current_trader = traders_list;

	while (current_trader != NULL) {
		trader* tmp = current_trader;
		current_trader = current_trader -> next;

		// Free all associated dynamic memory
		free(tmp -> profits);
		free(tmp -> quantity);
		free_orders_list(tmp -> order_list);
		free(tmp);
	}
}

// Function to free the linked list of orders
void free_orders_list(order* head) {
	order* ord = head;
	while (ord != NULL) {
		order* tmp = ord;
		ord = ord -> next;
		free(tmp);
	}
}




// Function to clear all dynamic memory associated with the orderbook
void free_orderbook() {
	for (int i = 0; i < number_of_products; ++i) {
		free_orders_list(orderbook[i].buy_orders);
		free_orders_list(orderbook[i].sell_orders);
	}

	free(orderbook);
}

void print_invalid(char* message, int trader_id) {
	printf("%s [T%d] Parsing command: <", LOG_PREFIX, trader_id);
	char* token = strtok(message, ";");
	printf("%s", token);
	while (token != NULL) {
		token = strtok(NULL, ";");
		if (token == NULL) {
			printf(">\n");
		} else {
			printf(" %s", token);
		}
	}
}


// Helper function to send appropriate message
int send_amend_message(order* ord, trader* trd, int index, int flag) {
	if (flag == 0) {	// Price did not change, or no matches found, send market buy/sell to other traders
		char market_msg[BUFFER_SIZE] = {0};
		sprintf(market_msg, "MARKET %s %s %d %d;", (ord -> order_type == 0) ? "BUY": "SELL",
							ord -> product_name, ord -> amount, ord -> cost);
		
		trader* current_trader = traders_list;
		while (current_trader != NULL) {
			if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {
				write(current_trader -> exchange_fd, market_msg, strlen(market_msg));
			}
			current_trader = current_trader -> next;
		}

		current_trader = traders_list;
		while (current_trader != NULL) {
			if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {
				kill(current_trader -> pid, SIGUSR1);
			}
			current_trader = current_trader -> next;
		}
		return -1;
	} else {	// Price changed - now try to match against potential buyers/sellers
		int matched = 0;
		if (ord -> order_type == 0) {	// buy order, match against sell orders
			matched = match_order(ord, orderbook[index].sell_orders, index);
		} else {
			matched = match_order(ord, orderbook[index].buy_orders, index);
		}
		return matched;
	}
}

void confirm_amend(order* ord, trader* t) {
	// Send message to the trader that the order has been amended
	char msg[SMALL_BUFFER_SIZE] = {0};
	sprintf(msg, "%s %d;", AMEND, ord -> order_id);
	write(t -> exchange_fd, msg, strlen(msg));
	kill(t -> pid, SIGUSR1);
}


int check_valid_message(char* message, trader* t) {
	char line_copy[BUFFER_SIZE] = {0};
	strcpy(line_copy, message);
	
	//printf("%s\n", message);
	char* token = strchr(message, ';');
	if ((token == NULL) || (strlen(token) != 1)) {	// no semicolon or invalid format
		print_invalid(message, t -> trader_id);
		return 0;
	}
	char command[SMALL_BUFFER_SIZE] = {0};
	long int order_id, quantity, price;
	char product[PRODUCT_BUFFER_SIZE] = {0};

	int x = sscanf(line_copy, "%s %ld %s %ld %ld;", command, &order_id, product, &quantity, &price);
	if (x == 5) {	// Buy, sell
		// Accounting for when there are more than 5 arguments
		char* tok = strtok(message, " ;");
		for (int i = 0; i < 5; ++i) {
			tok = strtok(NULL, " ;");
			if (tok == NULL) {
				break;
			}
		}
		if (tok != NULL) {
			print_invalid(line_copy, t -> trader_id);
			return 0;
		}
		if ((strcmp("BUY", command) == 0) || (strcmp("SELL", command) == 0)) {
			printf("%s [T%d] Parsing command: <%s %ld %s %ld %ld>\n", 
				LOG_PREFIX, t -> trader_id, command, order_id, product, quantity, price);
			if ((order_id < 0) || (order_id > LIMIT) || (quantity < 1) || (quantity > LIMIT) 
				|| (price < 1) || (price > LIMIT)) {	// Numerical values not in range
				return 0;
			}

			if (order_id != t -> order_id) {	// Invalid order id
				return 0;
			}

			// Check product is being traded in market or not
			int found = 0;
			for (int i = 0; i < number_of_products; ++i) {
				if (strcasecmp(orderbook[i].product_name, product) == 0) {
					found = 1;
					break;
				}
			}
			return found;
		} else {	// no valid command
			print_invalid(line_copy, t -> trader_id);
			//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
			return 0;
		}
	} else if (x == 4) {	// amend
		x = sscanf(line_copy, "%s %ld %ld %ld;", command, &order_id, &quantity, &price);
		if ((x < 4) || (strcmp(command, "AMEND") != 0)) {	// Invalid order format or not amend order
			print_invalid(message, t -> trader_id);
			return 0;
		} else {
			// Accounting for when there are more than 4 arguments
			char* tok = strtok(message, " ;");
			for (int i = 0; i < 4; ++i) {
				tok = strtok(NULL, " ;");
				if (tok == NULL) {
					break;
				}
			}
			if (tok != NULL) {
				print_invalid(line_copy, t -> trader_id);
				return 0;
			}
			if ((order_id < 0) || (order_id > LIMIT) || (quantity < 1) || (quantity > LIMIT) 
				|| (price < 1) || (price > LIMIT)) {
					print_invalid(line_copy, t -> trader_id);
					return 0;
			}
			if (order_id > t -> order_id) {	// trader's order id less than given order id, so cannot find
				print_invalid(line_copy, t -> trader_id);
				return 0;
			}
			order* ord = NULL;
			int idx = 0;
			// Now try to search for the order
			for (int i = 0; i < number_of_products; ++i) {
				ord = orderbook[i].buy_orders;
				while (ord != NULL) {
					if ((ord -> trader_id == t -> trader_id) && (ord -> order_id == order_id)) {
						idx = i;
						break;
					}
					ord = ord -> next;
				}
				if (ord != NULL) {
					break;
				}
				ord = orderbook[i].sell_orders;
				while (ord != NULL) {
					if ((ord -> trader_id == t -> trader_id) && (ord -> order_id == order_id)) {
						idx = i;
						break;
					}
					ord = ord -> next;
				}
			}
			// If not found, order cannot be amended
			if (ord == NULL) {
				print_invalid(line_copy, t -> trader_id);
				//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
				return 0;
			}
			printf("%s [T%d] Parsing command: <%s %ld %ld %ld>\n", 
				LOG_PREFIX, t -> trader_id, command, order_id, quantity, price);
			// Update order details
			int prev_cost = ord -> cost;
			ord -> amount = quantity;
			ord -> cost = price;

			int matched = 0;
			// the price has changed, so update position of the order
			if (ord == orderbook[idx].buy_orders) {	// potentially update head of buy linked list
				// if cost increases, do nothing - buyers are sorted in descending order
				if (prev_cost < ord -> cost) {
					update_levels(orderbook[idx].buy_orders, &(orderbook[idx].buy_level));
					update_levels(orderbook[idx].sell_orders, &(orderbook[idx].sell_level));
					confirm_amend(ord, t);
					matched = send_amend_message(ord, t, idx, 1);	// Try to match
					if (! matched) {
						send_amend_message(ord, t, idx, 0);
					}
					return 1;
				}
				// cost has decreased - move ahead
				if (ord -> next == NULL) {	// only order in list - do nothing
					update_levels(orderbook[idx].buy_orders, &(orderbook[idx].buy_level));
					update_levels(orderbook[idx].sell_orders, &(orderbook[idx].sell_level));
					confirm_amend(ord, t);
					matched = send_amend_message(ord, t, idx, 1);	// Try to match
					if (! matched) {
						send_amend_message(ord, t, idx, 0);
					}
					return 1;
				} else {	// move up the list and add
					orderbook[idx].buy_orders = (orderbook[idx].buy_orders) -> next;
					orderbook[idx].buy_orders -> prev = NULL;
					ord -> next = ord -> prev = NULL;
					order* current_order = orderbook[idx].buy_orders;
					while (current_order -> next != NULL) {
						if (current_order -> cost < ord -> cost) {
							break;
						}
						current_order = current_order -> next;
					}
					if (ord -> cost > current_order -> cost) {	// add before
						ord -> next = current_order;
						ord -> prev = current_order -> prev;
						current_order -> prev -> next = ord;
						current_order -> prev = ord;
					} else {	// add after
						ord -> next = current_order -> next;
						ord -> prev = current_order;
						if (current_order -> next != NULL) {
							current_order -> next -> prev = ord;
						}
						current_order -> next = ord;
					}
				}
			} else if (ord == orderbook[idx].sell_orders) {	// potentially update head of sell linked list
				// if cost decreases, do nothing - sellers are sorted in ascending order
				if (prev_cost > ord -> cost) {
					update_levels(orderbook[idx].buy_orders, &(orderbook[idx].buy_level));
					update_levels(orderbook[idx].sell_orders, &(orderbook[idx].sell_level));
					confirm_amend(ord, t);
					matched = send_amend_message(ord, t, idx, 1);	// Try to match
					if (! matched) {
						send_amend_message(ord, t, idx, 0);
					}
					return 1;
				}
				// cost has increased - move ahead
				if (ord -> next == NULL) {	// only order in list - do nothing
					update_levels(orderbook[idx].buy_orders, &(orderbook[idx].buy_level));
					update_levels(orderbook[idx].sell_orders, &(orderbook[idx].sell_level));
					confirm_amend(ord, t);
					matched = send_amend_message(ord, t, idx, 1);	// Try to match
					if (! matched) {
						send_amend_message(ord, t, idx, 0);
					}
					return 1;
				} else {
					orderbook[idx].sell_orders = (orderbook[idx].sell_orders) -> next;
					orderbook[idx].sell_orders -> prev = NULL;
					ord -> next = ord -> prev = NULL;
					order* current_order = orderbook[idx].sell_orders;
					while (current_order -> next != NULL) {
						if (current_order -> cost > ord -> cost) {
							break;
						}
						current_order = current_order -> next;
					}
					if (ord -> cost < current_order -> cost) {	// add before
						ord -> next = current_order;
						ord -> prev = current_order -> prev;
						current_order -> prev -> next = ord;
						current_order -> prev = ord;
					} else {	// add after
						ord -> next = current_order -> next;
						ord -> prev = current_order;
						if (current_order -> next != NULL) {
							current_order -> next -> prev = ord;
						}
						current_order -> next = ord;
					}
				}
			} else {	// Update position accordingly
				order* next = ord -> next;
				order* prev = ord -> prev;
				if (next != NULL) {
					next -> prev = prev;
				}
				if (prev != NULL) {
					prev -> next = next;
				}
				ord -> next = ord -> prev = NULL;
				order* head = (ord -> order_type == 0) ? orderbook[idx].buy_orders: orderbook[idx].sell_orders;
				while (head -> next != NULL) {
					if (ord -> order_type == 0) {
						if (ord -> cost > head -> cost) {
							break;
						}
					} else {
						if (ord -> cost < head -> cost) {
							break;
						}
					}
					head = head -> next;
				}
				if (ord -> order_type == 0) {	// buy order
					if (head -> cost > ord -> cost) {	// add after current node
						ord -> next = head -> next;
						ord -> prev = head;
						if (head -> next != NULL) {
							(head -> next) -> prev = ord;
						}
						head -> next = ord;
					} else {	// add before current node
						ord -> next = head;
						ord -> prev = head -> prev;
						if (head -> prev != NULL) {
							(head -> prev) -> next = ord;
						}
						head -> prev = ord;
						if (head == orderbook[idx].buy_orders) {	// Update head
							order** h = &(orderbook[idx].buy_orders);
							*h = ord;
						}
					}
				} else {	// sell order
					if (head -> cost < ord -> cost) {	// add after current node
						ord -> next = head -> next;
						ord -> prev = head;
						if (head -> next != NULL) {
							(head -> next) -> prev = ord;
						}
						head -> next = ord;
					} else {	// add before current node
						ord -> next = head;
						ord -> prev = head -> prev;
						if (head -> prev != NULL) {
							(head -> prev) -> next = ord;
						}
						head -> prev = ord;
						if (head == orderbook[idx].sell_orders) {	// Update head
							order** h = &(orderbook[idx].sell_orders);
							*h = ord;
						}
					}
				}
			}
			update_levels(orderbook[idx].buy_orders, &(orderbook[idx].buy_level));
			update_levels(orderbook[idx].sell_orders, &(orderbook[idx].sell_level));
			confirm_amend(ord, t);
			matched = send_amend_message(ord, t, idx, 1);	// Try to match
			if (! matched) {
				send_amend_message(ord, t, idx, 0);  
			}
			return 1;
		}

	} else if (strncmp(line_copy, "CANCEL", 6) == 0) {	// cancel
		sscanf(line_copy, "%s %ld;", command, &order_id);
		if ((order_id >= t -> order_id) || (order_id < 0)) {	// Invalid order id
			print_invalid(message, t -> trader_id);
			//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
			return 0;
		} else {	// Valid order id, potentially
			// Accounting for when there are more than 2 arguments
			char* tok = strtok(message, " ;");
			for (int i = 0; i < 2; ++i) {
				tok = strtok(NULL, " ;");
				if (tok == NULL) {
					break;
				}
			}
			if (tok != NULL) {
				print_invalid(line_copy, t -> trader_id);
				return 0;
			}
			order* ord = NULL;
			int idx = 0;
			// Now try to search for the order
			for (int i = 0; i < number_of_products; ++i) {
				ord = orderbook[i].buy_orders;
				while (ord != NULL) {
					if ((ord -> trader_id == t -> trader_id) && (ord -> order_id == order_id)) {
						idx = i;
						break;
					}
					ord = ord -> next;
				}
				if (ord != NULL) {
					break;
				}
				ord = orderbook[i].sell_orders;
				while (ord != NULL) {
					if ((ord -> trader_id == t -> trader_id) && (ord -> order_id == order_id)) {
						idx = i;
						break;
					}
					ord = ord -> next;
				}
			}
			// If not found, order cannot be cancelled
			if (ord == NULL) {
				print_invalid(line_copy, t -> trader_id);
				//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
				return 0;
			}
			printf("%s [T%d] Parsing command: <%s %ld>\n", LOG_PREFIX, t -> trader_id, "CANCEL", order_id);
			int order_type = ord -> order_type;
			memset(product, 0, PRODUCT_BUFFER_SIZE);
			strcpy(product, ord -> product_name);
			// Order found, so remove it from the list
			if (ord == orderbook[idx].buy_orders) {	// Remove from head of buyer list
				orderbook[idx].buy_orders = orderbook[idx].buy_orders -> next;
				if (orderbook[idx].buy_orders != NULL) {
					orderbook[idx].buy_orders -> prev = NULL;
				}
				ord -> next = ord -> prev = NULL;
				free(ord);
			} else if (ord == orderbook[idx].sell_orders) {	// Remove from head of seller list
				orderbook[idx].sell_orders = orderbook[idx].sell_orders -> next;
				if (orderbook[idx].sell_orders != NULL) {
					orderbook[idx].sell_orders -> prev = NULL;
				}
				ord -> next = ord -> prev = NULL;
				free(ord);
			} else {	// Remove from list normally
				order* next = ord -> next;
				order* prev = ord -> prev;
				if (next != NULL) {
					next -> prev = prev;
				}
				if (prev != NULL) {
					prev -> next = NULL;
				}
				ord -> next = ord -> prev = NULL;
				free(ord);
			}
			if (order_type == 0) {
				update_levels(orderbook[idx].buy_orders, &orderbook[idx].buy_level);
			} else {
				update_levels(orderbook[idx].sell_orders, &orderbook[idx].sell_level);
			}
			char msg[SMALL_BUFFER_SIZE] = {0};
			sprintf(msg, "%s %ld;", CANCEL, order_id);
			write(t -> exchange_fd, msg, strlen(msg));
			kill(t -> pid, SIGUSR1);

			trader* trd = traders_list;
			char msg2[BUFFER_SIZE] = {0};
			if (order_type == 0) {
				sprintf(msg2, "MARKET BUY %s 0 0;", product);
			} else {
				sprintf(msg2, "MARKET SELL %s 0 0;", product);
			}

			//sprintf(msg2, "MARKET BUY %s 0 0;", product);
			while (trd != NULL) {
				if ((trd -> status != 0) && (trd -> trader_id != t -> trader_id)) {
					write(trd -> exchange_fd, msg2, strlen(msg2));
				}
				trd = trd -> next;
			}
			trd = traders_list;
			while (trd != NULL) {
				if ((trd -> status != 0) && (trd -> trader_id != t -> trader_id)) {
					kill(trd -> pid, SIGUSR1);
				}
				trd = trd -> next;
			}
			
			return 1;
		}
	} else {	// Invalid
		print_invalid(message, t -> trader_id);
		//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
		return 0;
	}
	print_invalid(message, t -> trader_id);
	//printf("%s [T%d] Parsing command: <%s>\n", LOG_PREFIX, t -> trader_id, strtok(message, ";"));
	return 0;	// Keeping this, otherwise compiler will complain
}


// If a trader puts in another order of the same product and same cost,
// find the order, and update it. Do not increment buy levels here
order* search_order(order* ord, order* order_list) {
	order* current_order = order_list;
	while (current_order != NULL) {
		if (strcmp(current_order -> product_name, ord -> product_name) == 0
			&& (current_order -> cost == ord -> cost)) {
				return current_order;
		}
		current_order = current_order -> next;
	}
	return NULL;
}


void print_orderbook() {
	printf("%s%s\n", LOG_PREFIX, ORDERBOOK);
	for (int i = 0; i < number_of_products; ++i) {
		printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", 
				LOG_PREFIX, orderbook[i].product_name, orderbook[i].buy_level, orderbook[i].sell_level);

		// Print sell orders
		order* ord = orderbook[i].sell_orders;
		if (ord != NULL) {
			while (ord -> next != NULL) {
				ord = ord -> next;
			}
			while (ord != NULL) {
				int amount = ord -> amount;
				int cost = ord -> cost;
				int repeat_orders = 1;
				ord = ord -> prev;
				while (ord != NULL) {
					if (ord -> cost != cost) {
						break;
					}
					amount += ord -> amount;
					++repeat_orders;
					ord = ord -> prev;
				}
				printf("%s\t\tSELL %d @ $%d (%d %s)\n", LOG_PREFIX, amount, cost,
						repeat_orders, (repeat_orders == 1) ? "order": "orders");
			}
		}
		

		// Print buy orders
		ord = orderbook[i].buy_orders;
		while (ord != NULL) {
			int amount = ord -> amount;
			int cost = ord -> cost;
			int repeat_orders = 1;
			ord = ord -> next;
			while (ord != NULL) {
				if (ord -> cost != cost) {
					break;
				}
				amount += ord -> amount;
				++repeat_orders;
				ord = ord -> next;
			}
			printf("%s\t\tBUY %d @ $%d (%d %s)\n", LOG_PREFIX, amount, cost,
					repeat_orders, (repeat_orders == 1) ? "order": "orders");
		}
	}

	// Print quantity and profits for each trader
	printf("%s%s\n", LOG_PREFIX, POSITIONS);
	trader* trd = traders_list;
	while (trd != NULL) {
		printf("%s\tTrader %d:", LOG_PREFIX, trd -> trader_id);
		for (int j = 0; j < number_of_products; ++j) {
			if (j != number_of_products - 1) {
				printf(" %s %d ($%ld),", orderbook[j].product_name, (trd -> quantity)[j], (trd -> profits)[j]);
			} else {
				printf(" %s %d ($%ld)\n", orderbook[j].product_name, (trd -> quantity)[j], (trd -> profits)[j]);
			}
		}
		trd = trd -> next;
	}
}

void process_order(order* buyer, order* seller, int index, int new_order) {
	long int price, fee;
	fee = price = 0;
	// update quantity and profits of the traders
	trader* buying_trader = traders_list;
	while (buying_trader != NULL) {
		if (buying_trader -> trader_id == buyer -> trader_id) {
			break;
		}
		buying_trader = buying_trader -> next;
	}

	trader* selling_trader = traders_list;
	while (selling_trader != NULL) {
		if (selling_trader -> trader_id == seller -> trader_id) {
			break;
		}
		selling_trader = selling_trader -> next;
	}
	// Send fill to buyer first, then sender
	char buyer_msg[BUFFER_SIZE] = {0};
	char seller_msg[BUFFER_SIZE] = {0};
	if (buyer -> amount <= seller -> amount) {	// seller has more or equal quantity than buyer
		seller -> amount -= buyer -> amount;
		price = (long int) buyer -> amount * 
				(long int) ((new_order == 0) ?  seller -> cost: buyer -> cost);	// use the price of buyer
		fee = price / FEE_PERCENTAGE;
		long int remainder = price % FEE_PERCENTAGE;
		if (remainder >= 50) {
			++fee;
		}
		total_fees += fee;

		printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n",
				LOG_PREFIX, (new_order == 0) ? seller -> order_id: buyer -> order_id,
				(new_order == 0) ? seller -> trader_id: buyer -> trader_id,
				(new_order == 0) ? buyer -> order_id: seller -> order_id,
				(new_order == 0) ? buyer -> trader_id: seller -> trader_id, price, fee);

		// Update profits and quantity for each trader
		// buyer pays the fee if the buyer is the new order, otherwise the fee is deducted from
		// the seller's profits
		(buying_trader -> profits)[index] -= ((new_order == 1) ? price: (price + fee));
		(buying_trader -> quantity)[index] += buyer -> amount;
		(selling_trader) -> profits[index] += ((new_order == 0) ? price: (price - fee));
		(selling_trader -> quantity)[index] -= buyer -> amount;

		sprintf(buyer_msg, "%s %d %d;", FILL, buyer -> order_id, buyer -> amount);
		sprintf(seller_msg, "%s %d %d;", FILL, seller -> order_id, buyer -> amount);

		buyer -> amount = 0;
	} else {	// seller has less quantity than buyer
		buyer -> amount -= seller -> amount;
		price = (long int) seller -> amount * 
				(long int) ((new_order == 0) ?  seller -> cost: buyer -> cost);
		fee = price / FEE_PERCENTAGE;
		long int remainder = price % FEE_PERCENTAGE;
		if (remainder >= 50) {
			++fee;
		}
		total_fees += fee;

		printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%ld, fee: $%ld.\n",
				LOG_PREFIX, (new_order == 0) ? seller -> order_id: buyer -> order_id,
				(new_order == 0) ? seller -> trader_id: buyer -> trader_id,
				(new_order == 0) ? buyer -> order_id: seller -> order_id,
				(new_order == 0) ? buyer -> trader_id: seller -> trader_id, price, fee);
		
		// Update profits and quantity for each trader
		(buying_trader -> profits)[index] -= ((new_order == 1) ? price: (price + fee));
		(buying_trader -> quantity)[index] += seller -> amount;
		(selling_trader) -> profits[index] += ((new_order == 0) ? price: (price - fee));
		(selling_trader -> quantity)[index] -= seller -> amount;

		sprintf(buyer_msg, "%s %d %d;", FILL, buyer -> order_id, seller -> amount);
		sprintf(seller_msg, "%s %d %d;", FILL, seller -> order_id, seller -> amount);

		seller -> amount = 0;
	}
	if (buying_trader -> status == 1) {
		write(buying_trader -> exchange_fd, buyer_msg, strlen(buyer_msg));
		kill(buying_trader -> pid, SIGUSR1);
	}
	if (selling_trader -> status == 1) {
		write(selling_trader -> exchange_fd, seller_msg, strlen(seller_msg));
		kill(selling_trader -> pid, SIGUSR1);
	}
	
}

// Function that updates the buy and sell levels for the orderbook
void update_levels(order* order_list, int* level) {
	order* ord = order_list;
	*level = 0;
	while (ord != NULL) {
		++(*level);
		int cost = ord -> cost;
		while (ord -> cost == cost) {
			ord = ord -> next;
			if (ord == NULL) {
				break;
			}
		}
	}
}


// Function to match buyer and seller
int match_order(order* ord, order* order_list, int index) {
	// Pair first instance of buyer with seller if possible
	order* paired_order = order_list;
	if (ord -> order_type == 0) {	// buyer, match with sellers
		while (paired_order != NULL) {
			if ((ord -> trader_id != paired_order -> trader_id) && (ord -> cost >= paired_order -> cost)) {
				break;
			}
			paired_order = paired_order -> next;
		}
	} else {	// seller, match with buyer
		// buyers are sorted in descending order, so if the first order cannot be matched,
		// none of the others can be matched
		if (paired_order -> cost < ord -> cost) {	
			return 0;
		}
		while (paired_order != NULL) {
			if ((ord -> trader_id != paired_order -> trader_id) && (ord -> cost <= paired_order -> cost)) {
				break;
			}
			if (ord -> cost > paired_order -> cost) {
				paired_order = NULL;
				break;
			}
			paired_order = paired_order -> next;
		}
	}
	// No pairings found, so terminate
	if (paired_order == NULL) {
		return 0;
	}

	// Start processing matches
	while (ord -> amount > 0) {
		if (ord -> order_type == 0) {	// new order is buyer
			// End of seller list or cannot match with anymore sellers
			if ((paired_order == NULL) || (paired_order -> cost > ord -> cost)) {
				break;
			}
			// Process orders when the trader id's do not match
			if (ord -> trader_id != paired_order -> trader_id) {
				process_order(ord, paired_order, index, 0);
			}
		} else {	// new order is seller
			// End of buyer list or cannot match with anymore buyers
			if ((paired_order == NULL) || (paired_order -> cost < ord -> cost)) {
				break;
			}
			if (ord -> trader_id != paired_order -> trader_id) {
				process_order(paired_order, ord, index, 1);
			}
		}
		if (paired_order -> amount == 0) {	// match from list has quantity 0
			//order** head = NULL;
			if (paired_order == orderbook[index].buy_orders) {	// head of buy orders
				orderbook[index].buy_orders = (orderbook[index].buy_orders) -> next;
				if (orderbook[index].buy_orders != NULL) {
					orderbook[index].buy_orders -> prev = NULL;
				}
				paired_order -> next = paired_order -> prev = NULL;
				free(paired_order);
				paired_order = orderbook[index].buy_orders;
				// head = &paired_order;
				// *head = (*head) -> next;
				// (*head) -> prev = NULL;
				// free(paired_order);
				// paired_order = *head;
				continue;
			} else if (paired_order == orderbook[index].sell_orders) {	// head of sell orders
				orderbook[index].sell_orders = (orderbook[index].sell_orders) -> next;
				if (orderbook[index].sell_orders != NULL) {
					orderbook[index].sell_orders -> prev = NULL;
				}
				paired_order -> next = paired_order -> prev = NULL;
				free(paired_order);
				paired_order = orderbook[index].sell_orders;
				continue;
			} else {	// update as normal
				order* prev = paired_order -> prev;
				order* next = paired_order -> next;
				if (next != NULL) {
					next -> prev = prev;
				}
				if (prev != NULL) {
					prev -> next = next;
				}
				order* tmp = paired_order;
				paired_order = paired_order -> next;
				tmp -> next = tmp -> prev = NULL;
				free(tmp);
				continue;
			}
		}
		paired_order = paired_order -> next;
	}

	// free new order if it was fulfilled
	if (ord -> amount == 0) {
		if (ord == orderbook[index].sell_orders) {	// remove new order from head of sell orders
			orderbook[index].sell_orders = (orderbook[index].sell_orders) -> next;
			if (orderbook[index].sell_orders != NULL) {
				(orderbook[index].sell_orders) -> prev = NULL;
			}
			ord -> next = ord -> prev = NULL;
			free(ord);
		} else if (ord == orderbook[index].buy_orders) {	// remove new ordr from head of buy orders
			orderbook[index].buy_orders = (orderbook[index].sell_orders) -> next;
			if (orderbook[index].buy_orders != NULL) {
				(orderbook[index].buy_orders) -> prev = NULL;
			}
			ord -> next = ord -> prev = NULL;
			free(ord);
		} else {	// remove normally
			order* next = ord -> next;
			order* prev = ord -> prev;
			if (next != NULL) {
				next -> prev = prev;
			}
			if (prev != NULL) {
				prev -> next = next;
			}
			ord -> next = ord -> prev = NULL;
			free(ord);
		}
	}
	// update buy and sell levels
	update_levels(orderbook[index].buy_orders, &(orderbook[index].buy_level));
	update_levels(orderbook[index].sell_orders, &(orderbook[index].sell_level));

	return 1;
}



// Process buy, sell, amend, cancel orders
void process_signal() {
	// Find the trader by pid
	trader* trd = traders_list;
	while (trd -> pid != present_trader) {
		trd = trd -> next;
	}

	// Read message from pipe
	char line[BUFFER_SIZE] = {0};
	char line_copy[BUFFER_SIZE] = {0};
	read(trd -> trader_fd, line, BUFFER_SIZE);
	strcpy(line_copy, line);

	// if (time == 35) {
	// 	printf("[t = 35] T[%d]: %s\n", trd -> trader_id, line_copy);
	// }

	int checker = check_valid_message(line, trd);
	if (! checker) {	// Send SIGUSR1 to trader saying that the order was invalid
		char msg[SMALL_BUFFER_SIZE] = {0};
		sprintf(msg, "%s", INVALID);
		write(trd -> exchange_fd, msg, strlen(INVALID));
		kill(trd -> pid, SIGUSR1);
	} else {
		char command[SMALL_BUFFER_SIZE] = {0};
		int order_id, quantity, price;
		char product[PRODUCT_BUFFER_SIZE] = {0};

		// Find trader
		trader* trd = traders_list;
		while (trd != NULL) {
			if (trd -> pid == present_trader) {
				break;
			}
			trd = trd -> next;
		}

		int x = sscanf(line_copy, "%s %d %s %d %d", command, &order_id, product, &quantity, &price);
		if (x == 5) {	// buy or sell
			int idx = 0;
			// Find index of the product from orderbook
			while (idx < number_of_products) {
				if (strcmp(orderbook[idx].product_name, product) == 0) {
					break;
				}
				++idx;
			}
			// Create and initialize order for orderbook
			order* ord = malloc(sizeof(order));
			ord -> next = ord -> prev = NULL;
			ord -> cost = price;
			ord -> amount = quantity;
			ord -> trader_id = trd -> trader_id;
			ord -> trader_pid = trd -> pid;
			ord -> order_id = trd -> order_id;
			strcpy(ord -> product_name, product);

			if (strcmp("BUY", command) == 0) {	// buy
				ord -> order_type = 0;

				order* existing_order = search_order(ord, orderbook[idx].buy_orders);
				if (existing_order == NULL) {	// new order
					++orderbook[idx].buy_level;	// increment buy level if a similar order has not been made by a trader
				}
				if (orderbook[idx].buy_orders == NULL) {	// add to empty list
					orderbook[idx].buy_orders = ord;
				} else {	// add in descending order
					order* current_order = orderbook[idx].buy_orders;
					while (current_order -> next != NULL) {
						if (current_order -> cost < ord -> cost) {
							break;
						}
						current_order = current_order -> next;
					}
					if (current_order -> cost > ord -> cost) {	// add after current node
						ord -> next = current_order -> next;
						ord -> prev = current_order;
						if (current_order -> next != NULL) {
							(current_order -> next) -> prev = ord;
						}
						current_order -> next = ord;
					} else {	// add before current node
						ord -> next = current_order;
						ord -> prev = current_order -> prev;
						if (current_order -> prev != NULL) {
							(current_order -> prev) -> next = ord;
						}
						current_order -> prev = ord;
						if (current_order == orderbook[idx].buy_orders) {	// Update head
							order** head = &(orderbook[idx].buy_orders);
							*head = ord;
						}
					}
				}

				char msg[SMALL_BUFFER_SIZE] = {0};
				sprintf(msg, "%s %d;", ACCEPT, trd -> order_id);
				// Increment trader id
				++(trd -> order_id);

				// Send SIGUSR1 to other traders with market buy message, and the trader that their order has been accepted
				char msg2[BUFFER_SIZE] = {0};
				sprintf(msg2, "MARKET BUY %s %d %d;", product, quantity, price);

				trader* current_trader = traders_list;
				while (current_trader != NULL) {
					if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {
						write(current_trader -> exchange_fd, msg2, strlen(msg2));
					} else if (trd -> trader_id == current_trader -> trader_id) {	// Write and send SIGUSR1 back to trader
						write(trd -> exchange_fd, msg, strlen(msg));
						kill(current_trader -> pid, SIGUSR1);
					}
					current_trader = current_trader -> next;
				}

				current_trader = traders_list;
				while (current_trader != NULL) {
					// send sigusr1 if trader not disconnected
					if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {	
						kill(current_trader -> pid, SIGUSR1);
					}
					current_trader = current_trader -> next;
				}

				// If there is at least one sell order, try to match
				if (orderbook[idx].sell_orders != NULL) {
					match_order(ord, orderbook[idx].sell_orders, idx);
				}
			} else {	// sell
				ord -> order_type = 1;

				order* existing_order = search_order(ord, orderbook[idx].sell_orders);
				if (existing_order == NULL) {	// new order
					++orderbook[idx].sell_level;	// increment buy level if a similar order has not been made by a trader
				}
				if (orderbook[idx].sell_orders == NULL) {	// add to empty list
					orderbook[idx].sell_orders = ord;
				} else {	// add in ascending order
					order* current_order = orderbook[idx].sell_orders;
					while (current_order -> next != NULL) {
						if (current_order -> cost > ord -> cost) {
							break;
						}
						current_order = current_order -> next;
					}
					if (current_order -> cost < ord -> cost) {	// add after current node
						ord -> next = current_order -> next;
						ord -> prev = current_order;
						if (current_order -> next != NULL) {
							(current_order -> next) -> prev = ord;
						}
						current_order -> next = ord;
					} else {	// add before current node
						ord -> next = current_order;
						ord -> prev = current_order -> prev;
						if (current_order -> prev != NULL) {
							(current_order -> prev) -> next = ord;
						}
						current_order -> prev = ord;
						if (current_order == orderbook[idx].sell_orders) {	// Update head
							order** head = &(orderbook[idx].sell_orders);
							*head = ord;
						}
					}
				}

				char msg[SMALL_BUFFER_SIZE] = {0};
				sprintf(msg, "%s %d;", ACCEPT, trd -> order_id);
				// Increment trader id
				++(trd -> order_id);

				// Send SIGUSR1 to other traders with market sell message, and the trader that their order has been accepted
				char msg2[BUFFER_SIZE] = {0};
				sprintf(msg2, "MARKET SELL %s %d %d;", product, quantity, price);

				trader* current_trader = traders_list;
				while (current_trader != NULL) {
					if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {
						write(current_trader -> exchange_fd, msg2, strlen(msg2));
					} else if (trd -> trader_id == current_trader -> trader_id) {	// Write and send SIGUSR1 back to trader
						write(trd -> exchange_fd, msg, strlen(msg));
						kill(current_trader -> pid, SIGUSR1);
					}
					current_trader = current_trader -> next;
				}

				current_trader = traders_list;
				while (current_trader != NULL) {
					if ((current_trader -> trader_id != trd -> trader_id) && (current_trader -> status != 0)) {	// send sigusr1 if trader not disconnected
						kill(current_trader -> pid, SIGUSR1);
					}
					current_trader = current_trader -> next;
				}

				// If there is at least one buy order, try to match
				if (orderbook[idx].buy_orders != NULL) {
					match_order(ord, orderbook[idx].buy_orders, idx);
				}
			}
		} else if (x == 4) {	// valid amend order
			// Everything is handled in the valid message checker, so this will remain blank
			
		} else if (x == 3) {	// valid cancel order
			// Everything is handled in the valid message checker, so this will remain blank
		}
		print_orderbook();
	}
}


// Open the trader binaries and launch their processes
void initialize_traders(int argc, char** argv) {
	// Variables to be used for creating the trader nodes in a linked list
	trader* prev_trader = traders_list;
	//trader* current_trader = traders_list;

	int trader_number = 0;

	for(int i = trader_number + 2; i < argc; ++i) {
		// Allocate memory for trader
		trader* current_trader = malloc(sizeof(trader));
		current_trader -> next = NULL;
		current_trader -> prev = prev_trader;
		if (prev_trader != NULL) {
			prev_trader -> next = current_trader;
		}
		current_trader -> status = 0;	// Set as inactive initially

		strcpy(current_trader -> trader_binary, argv[i]);
		current_trader -> trader_id = trader_number;
		current_trader -> order_id = 0;
		current_trader -> order_list = NULL;	// Set order list to be empty

		current_trader -> profits = calloc(number_of_products, sizeof(long int));
		current_trader -> quantity = calloc(number_of_products, sizeof(int));

		// Set up and open named pipes
		char exchange[BUFFER_SIZE];	// exchange fd
		char trader[BUFFER_SIZE];	// trader fd
		sprintf(exchange, "%s%d", FIFO_EXCHANGE, trader_number);
		sprintf(trader, "%s%d", FIFO_TRADER, trader_number);

		// Create named pipes for trader and exchange
		int pipe = mkfifo(exchange, 0777);
		if (pipe < 0) {
			printf("Failed to create named pipe for exchange %d\n", trader_number);
			exit(1);
		}
		printf("%s Created FIFO %s\n", LOG_PREFIX, exchange);

		pipe = mkfifo(trader, 0777);
		if (pipe < 0) {
			printf("Failed to create named pipe for trader %d\n", trader_number);
			exit(1);
		} 
		printf("%s Created FIFO %s\n", LOG_PREFIX, trader);

		// Fork processes
		int fd1, fd2;
		fd1 = fd2 = 0;

		pid_t pid = fork();
		if (pid < 0) {
			printf("Failed to fork for trader %d\n", trader_number);
			exit(1);
		} else if (pid == 0) {	// Child process
			printf("%s Starting trader %d (./bin/pex_test_trader)\n", LOG_PREFIX, trader_number);
			char num[SMALL_BUFFER_SIZE] = {0};
			sprintf(num, "%d", trader_number);
			execlp(argv[i], argv[i], num, NULL);
		} else {	// Parent process
			fd1 = open(exchange, O_WRONLY);
			if (fd1 < 0) {
				printf("Unable to open write pipe for trader %d\n", trader_number);
				exit(1);
			}
			printf("%s Connected to %s\n", LOG_PREFIX, exchange);

			fd2 = open(trader, O_RDONLY);
			if (fd2 < 0) {
				printf("Unable to open read pipe for trader %d\n", trader_number);
				exit(1);
			}
			printf("%s Connected to %s\n", LOG_PREFIX, trader);
			current_trader -> exchange_fd = fd1;
			current_trader -> trader_fd = fd2;
			current_trader -> pid = pid;

			++trader_number;
			if (traders_list == NULL) {
				traders_list = current_trader;
			}
			prev_trader = current_trader;
		}
	}
	number_of_traders = trader_number;

	// Write to all traders
	trader* current_trader = traders_list;
	while (current_trader != NULL) {
		int x = write(current_trader -> exchange_fd, MARKET_OPEN, strlen(MARKET_OPEN));
		if (x == -1) {
			printf("Failed to write to pipe for trader %d\n", current_trader -> trader_id);
			exit(1);
		}
		current_trader = current_trader -> next;
	}

	// Send SIGUSR1 to all traders
	current_trader = traders_list;
	while (current_trader != NULL) {
		kill(current_trader -> pid, SIGUSR1);
		current_trader -> status = 1;	// Trader received signal and should be active
		current_trader = current_trader -> next;
	}
}


// Open the products file and read how many products are being exchanged
void read_products_file(const char* filename) {
	FILE* fp = fopen(filename, "r");
	if (fp == NULL) {	// cannot open file
		printf("Unable to open product file %s", filename);
		exit(1);
	}

	printf("%s Starting\n", LOG_PREFIX);

	char line[PRODUCT_BUFFER_SIZE] = {0};
	fgets(line, PRODUCT_BUFFER_SIZE, fp);
	number_of_products = atoi(line);

	printf("%s Trading %d products:", LOG_PREFIX, number_of_products);

	// Initialize orderbook
	orderbook = malloc(sizeof(product) * number_of_products);

	for (int i = 0; i < number_of_products; ++i) {
		fscanf(fp, "%s\n", line);	// fscanf more convenient
		printf(" %s", line);

		// Copy and initialize data of each product in the orderbook
		strcpy(orderbook[i].product_name, line);
		orderbook[i].buy_level = orderbook[i].sell_level = 0;
		orderbook[i].index = i;
		orderbook[i].buy_orders = orderbook[i].sell_orders = NULL;

		memset(line, 0, PRODUCT_BUFFER_SIZE);
	}
	printf("\n");
	
	fclose(fp);
}


// Function to disconnect trader when SIGCHLD is received
void disconnect_trader() {
	trader* current_trader = traders_list;
	while (current_trader != NULL) {
		if (current_trader -> pid == recently_disconnected_trader) {
			break;
		}
		current_trader = current_trader -> next;
	}
	current_trader -> status = 0;	// Disconnect trader

	// Close and unlink pipes
	close(current_trader -> exchange_fd);
	close(current_trader -> trader_fd);

	char exchange[BUFFER_SIZE] = {0};
	char trader[BUFFER_SIZE] = {0};
	sprintf(trader, "%s%d", FIFO_TRADER, current_trader -> trader_id);
	sprintf(exchange, "%s%d", FIFO_EXCHANGE, current_trader -> trader_id);

	unlink(exchange);
	unlink(trader);

	current_trader -> exchange_fd = current_trader -> trader_fd = -1;
	printf("%s Trader %d disconnected\n", LOG_PREFIX, current_trader -> trader_id);
	--number_of_traders;	// Decrement number of traders
	trader_disconnected = 0;	// reset flag
}




int main(int argc, char **argv) {
	traders_list = NULL;
	number_of_products = 0;
	if (argc <= 2) {
		printf("Insufficient arguments!\n");
		return 1;
	}

	read_products_file(argv[1]);
	
	
	initialize_traders(argc, argv);

	// Sigaction to handle sigusr1 signals
	struct sigaction sigusr1_handler = {0};
	memset(&sigusr1_handler, 0, sizeof(sigusr1_handler));
	sigusr1_handler.sa_sigaction = handle_sigusr1;
	sigusr1_handler.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR1, &sigusr1_handler, NULL);

	//Sigaction to handle sigchld signals when child process terminates
	struct sigaction sigchld_handler = {0};
	memset(&sigchld_handler, 0, sizeof(sigusr1_handler));
	sigchld_handler.sa_sigaction = handle_sigchld;
	sigchld_handler.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &sigchld_handler, NULL);

	// Event loop that runs while at least one trader is active
	while (number_of_traders > 0) {
		if (trader_disconnected) {	// a trader disconnected
			disconnect_trader();
			trader_disconnected = 0;
		} else {
			if (is_busy) {
				process_signal();
				is_busy = 0;
			} else {
				pause();
			}
		}
	}


	// Free all traders
	free_all_traders();

	printf("%s Trading completed\n", LOG_PREFIX);
	printf("%s Exchange fees collected: $%ld\n", LOG_PREFIX, total_fees);

	free_orderbook();

	return 0;
}