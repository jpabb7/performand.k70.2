#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>

#include "../Common/MemoryMapping/memory_map.h"
#include "../Common/utils.h"

int runtime_count = 0;


int main(int argc, char **argv)
{
	FILE *fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	int runtime_count = 0;
	char buffer[512];
	char *bufptr; 
	int file_idx = 0;

	h_mmapped_file mapped_file;
	memset(&mapped_file, 0, sizeof(h_mmapped_file));

	mapped_file.filename = "imu";
	mapped_file.size = DEFAULT_FILE_LENGTH;

	//Prepare the mapped Memory file
	if((mm_prepare_mapped_mem(&mapped_file)) < 0) {
		printf("Error mapping IMU file.\n");
		return -1;	
	}

	runtime_count = read_rt_count();
	
	bufptr = buffer;
	while(1) {
		fp = fopen("/dev/imu", "rw");
		if (fp == NULL)
			printf("Error open IMU file");
	
		while ((read = getline(&line, &len, fp)) != -1) {
			memcpy(bufptr, line, read);	
			bufptr+=read;				
		}
		bufptr='\0';
		mm_append(buffer, &mapped_file);
		file_idx = write_log_file("imu", runtime_count, file_idx, buffer);
		fclose(fp);
		bufptr = buffer;
		usleep(100000);		
	}
  	return 1;
	
}

