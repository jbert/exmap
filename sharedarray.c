/*
 * Shared lib to test different access patterns and sharing
 */
#include "sharedarray.h"
#include <stdio.h>

typedef unsigned char BigArr[ARRAY_SIZE] __attribute__ ((aligned(PAGE_SIZE)));

/* Uninitialised stuff will go into .bss and get special zero-page
 * treatment */
BigArr uninit_donttouch;
BigArr uninit_readme;
BigArr uninit_writeme;
BigArr uninit_readhalf;

BigArr donttouch = { 1 };
BigArr readme = { 2 };
BigArr writeme = { 3 };
BigArr readhalf = { 4 };

int do_shared_array_work(void)
{
	int i;
	int sum = 0;
	int interactive = 0;
	
	if (interactive) { printf("pre reading> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE; ++i) {
		sum += readme[i];
	}
	if (interactive) { printf("done reading, pre writing> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE; ++i) {
		writeme[i] = i;
	}
	if (interactive) { printf("done writing, pre half-read> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE / 2; ++i) {
		sum += readhalf[i];
	}

	if (interactive) { printf("pre uinit reading> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE; ++i) {
		sum += uninit_readme[i];
	}
	if (interactive) { printf("done reading, pre uinit writing> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE; ++i) {
		uninit_writeme[i] = i;
	}
	if (interactive) { printf("done writing, pre uinit half-read> "); getchar(); }
	for (i = 0; i < ARRAY_SIZE / 2; ++i) {
		sum += uninit_readhalf[i];
	}
	
	if (interactive) { printf("done> "); getchar(); }

	
	return 0;
}
