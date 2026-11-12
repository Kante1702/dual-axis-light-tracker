/*
 * filter.h
 * Filtrácia meraní: priemer za 20 ms, EMA
 * Zodpovedá: člen 2
 */
#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <stdbool.h>

void filter_init(void);

#endif /* FILTER_H */
