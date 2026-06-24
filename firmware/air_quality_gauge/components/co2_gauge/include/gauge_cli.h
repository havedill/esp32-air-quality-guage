#pragma once

#include <stdbool.h>

void gauge_cli_print_help(void);
void gauge_cli_handle_line(char *line);

bool gauge_log_is_enabled(void);
void gauge_log_set(bool enabled);
