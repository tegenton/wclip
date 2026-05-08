#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"

int
main(int argc, char *argv[]) {
	int mode, opt;

	mode = -1;
	while ((opt = getopt(argc, argv, "io")) != -1) {
		switch (opt) {
			case 'i':
				mode = 0;
				break;
			case 'o':
				mode = 1;
				break;
			default:
				fprintf(stderr, "Usage: %s [-i|-o]\n", argv[0]);
				return EXIT_FAILURE;
		}
	}
	printf("%d\n", mode);
	return EXIT_SUCCESS;
}
