#include <stdio.h>
#include <errno.h>

int main(int argc, char *argv[]) {
	if (argc != 3) {
		printf("USAGE: file2c filename.ext arrayname\n");
		return 0;
	}
	FILE *in = fopen(argv[1], "rb");
	if (in == NULL) {
		printf("Error opening %s: %s\n", argv[1], strerror(errno));
		return -1;
	}
	printf("static const unsigned char %s[] = {", argv[2]);
	int col = 0;
	unsigned char byte;
	while (fread(&byte, 1, 1, in) == 1) {
		if (col == 0)
			printf("\n\t");
		printf("0x%.2x, ", byte);
		if (++col == 16)
			col = 0;
	}
	fclose(in);
	printf("\n};\n");
	return 0;
}
