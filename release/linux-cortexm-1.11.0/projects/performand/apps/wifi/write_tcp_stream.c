#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <math.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <time.h>
#include <pthread.h> 
#include <sys/ioctl.h>		/* ioctl */

#include "../Common/GPIO/gpio_api.h"
#include "gainspan/AT/AtCmdLib.h"

#include "../Common/MemoryMapping/memory_map.h"
#include "../Common/utils.h"


#include "kinetis_adc.h"


#define FILE_LENGTH 	0x1000	//1KB File memory

void *monitor_ConnStat();
void *sendData();
void *ledIndication();

bool clientConnected = 1;
char* gps_file_memory;
int gps_memfp;

char* imu_file_memory;
int imu_memfp;

char* ble_file_memory;
int ble_memfp;

int runtime_count=0, piv_rt_count=0,file_idx=0;

long mag_cal_values[6];
long acc_cal_values[6];

h_mmapped_file gps_mapped_file;
h_mmapped_file imu_mapped_file;

void register_sig_handler();
void sigint_handler(int sig);

int done;

void register_sig_handler()
{
	struct sigaction sia;

	bzero(&sia, sizeof sia);
	sia.sa_handler = sigint_handler;

	if (sigaction(SIGINT, &sia, NULL) < 0) {
		perror("sigaction(SIGINT)");
		exit(1);
	} 

	done = 0;
}

void sigint_handler(int sig)
{
	printf("Terminating TCP/IP stream.\n");
	done = 1;
}

float get_battery_voltage(void) {
	int file_desc, ret_val, i;
	struct kinetis_adc_request adc_req;

	//memset(&adc_req, 0, sizeof(strckinetis_adc_request));

	file_desc = open("/dev/adc", 0);
	if (file_desc < 0) {
		printf("Can't open device file /dev/adc");
		exit(-1);
	}

	ret_val = ioctl(file_desc, IOCTL_KINETISADC_ENABLE_ADC, 0);
	if (ret_val < 0) {
		printf("ioctl_kinetisadc_enable_adc failed:%d\n", ret_val);
		ret_val =-1;
	}

	adc_req.adc_module=0;
	adc_req.channel_id=3;

	//ioctl_kinetisadc_measure(file_desc, &adc_req);
	ret_val = ioctl(file_desc, IOCTL_KINETISADC_MEASURE, &adc_req);

	if (ret_val < 0) {
		printf("ioctl_kinetisadc_measure failed: %d\n", ret_val);
		//exit(-1);
	}

	debug(2, "ADC value (battery): %d\n", (int)adc_req.result);

	ret_val = ioctl(file_desc, IOCTL_KINETISADC_DISABLE_ADC, 0);
	if (ret_val < 0) {
		printf("ioctl_kinetisadc_disable_adc failed:%d\n", ret_val);
		ret_val =-1;
	}

	close(file_desc);

	return ((float)adc_req.result*0.0001875);
}

long get_cal(int mag)
{
	int i;
	FILE *f;
	char buff[32];
	static long val[6];

	if (mag) {
		f = fopen("./nand/magcal.txt", "r");
	
		if (!f) {
			printf("Default magcal.txt not found\n");
			return 0;
		}
	}
	else {
		f = fopen("./nand/accelcal.txt", "r");
	
		if (!f) {
			printf("Default accelcal.txt not found\n");
			return 0;
		}
	}		

	memset(buff, 0, sizeof(buff));
	
	for (i = 0; i < 6; i++) {
		if (!fgets(buff, 20, f)) {
			printf("Not enough lines in calibration file\n");
			break;
		}

		val[i] = atoi(buff);

		if (val[i] == 0) {
			printf("Invalid cal value: %s\n", buff);
			break;
		}
		
		if (mag) {
			mag_cal_values[i] = val[i];
		} else {
			acc_cal_values[i] = val[i];
		}	
	}

	fclose(f);

	return (long)&val;
}

int main(int argc, char **argv)
{
	pthread_t monitor_ConnStat_thread, sendData_thread, ledIndication_thread;
	pthread_attr_t thread_attr;

	//if(!gpio_export(LED_IND2) || !gpio_setDirection(LED_IND2,0))
	//	printf("ERROR: Exporting gpio port.");

	//*** IMPORTANT TO DO THIS FIRST ***
	GS_HAL_uart_set_comm_port("/dev/ttyS3");

	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
		
	register_sig_handler();

	//Prepare memory mapping for gps
	memset(&gps_mapped_file, 0, sizeof(h_mmapped_file));	
	gps_mapped_file.filename = "gps";
	gps_mapped_file.size = DEFAULT_FILE_LENGTH;
	if((mm_prepare_mapped_mem(&gps_mapped_file)) < 0) {
		printf("Error mapping %s file.\n",gps_mapped_file.filename);
		return -1;	
	}
	
	//Prepare memory mapping for imu
	memset(&imu_mapped_file, 0, sizeof(h_mmapped_file));	
	imu_mapped_file.filename = "imu";
	imu_mapped_file.size = DEFAULT_FILE_LENGTH;
	if((mm_prepare_mapped_mem(&imu_mapped_file)) < 0) {
		printf("Error mapping %s file.\n",imu_mapped_file.filename);
		return -1;	
	}


	runtime_count = read_rt_count();
	printf("Runtime count: %d\n",runtime_count);

	//pthread_create(&monitor_ConnStat_thread,&thread_attr,monitor_ConnStat,NULL);
	pthread_create(&sendData_thread,&thread_attr,sendData,NULL);
//	pthread_create(&ledIndication_thread,&thread_attr,ledIndication,NULL);

	pthread_join(sendData_thread,NULL);

	exit(1);
}



void *ledIndication() {
	while(1) {
		gpio_setValue(LED_IND2,1);
		usleep(100000);
		gpio_setValue(LED_IND2,0);
		usleep(500000);
	}
}

void *monitor_ConnStat()
{
	int i = 0;
	int constat = 0;
	printf("Starting connection monitor thread.\n");
	while (1) {
		constat = AtLib_ResponseHandle();
		printf("Connection status: %d\n", constat);
		switch(constat) {
			case HOST_APP_MSG_ID_TCP_SERVER_CLIENT_CONNECTION:
				printf("Client connected.\n");
				clientConnected=1;
				break;
			case HOST_APP_MSG_ID_DISCONNECT:
				printf("Client disconnected.\n");
				clientConnected=0;
				break;
			default:
						
				break;
		}
		usleep(500000);	
		fflush(stdout);
	}
	pthread_exit(NULL);
}

void write_alllog_file(char *name,int runtime_count, char *receiveMessage, int len) {
	FILE *file;
	int size = 0;
	char format[] = "/sdcard/datalog-%s" "%d.%d.txt";
	char filename[sizeof format+10];
	sprintf(filename,format, name, runtime_count, file_idx);	

	file = fopen(filename,"a+"); 
	fseek(file, 0, SEEK_END);
   	size = ftell(file);
	if(size > 5000000) {
		fclose(file);
		file_idx+=1;
		sprintf(filename,format, name, runtime_count, file_idx);
		file = fopen(filename,"a+");
	}

	fwrite(receiveMessage,1,len,file);

	fclose(file);
}

static void sendTCPStream(h_mmapped_file *mapped_file) {
	int fd=0,len=0, left=0, memptr=0;	
	struct flock fl = {F_WRLCK, SEEK_SET,   0,      0,     0 };
	
	//LOCK !!!	
	fl.l_pid = getpid();
	fd = open(mapped_file->filename, O_WRONLY);  
	fcntl(fd, F_SETLKW, &fl);

	len = mm_get_next_available(mapped_file,0);
	
	if(len > 0) {
		write_alllog_file("all", runtime_count, mapped_file->mem_ptr+2, len);

		if(len>1400) {
			//printf("backlog greater than 1400.\n");
			left=len;
			for(memptr=0; memptr<len; memptr+=1400) {	
				//printf("fmemsize: %d memptr: %d left: %d\n", fmemsize, memptr, left);
				GS_API_SendTcpData(1, (mapped_file->mem_ptr)+2+memptr, (left>1400) ? 1400 : left);
				left=left-1400;
			}
		} 
		else {
			GS_API_SendTcpData(1, (mapped_file->mem_ptr)+2, len);	
		}

		
		//Flush the file, point to beginning of file.
		memset(mapped_file->mem_ptr, '\0', sizeof(char)*FILE_LENGTH);
		asm("dsb");
		usleep(5000);
	}

	//UNLOCK!!!
	fl.l_type  = F_UNLCK;  			// tell it to unlock the region
	fcntl(fd, F_SETLK, &fl); 			// set the region to unlocked
	close(fd);
}

void send_cal_data(void) {
	char buffer[300];
	int len = 0;

	len = sprintf(buffer, "<imu_cal>\n\
<acc>\n\
<val1>%d</val1>\n\
<val2>%d</val2>\n\
<val3>%d</val3>\n\
<val4>%d</val4>\n\
<val5>%d</val5>\n\
<val6>%d</val6>\n\
</acc>\n\
<mag>\n\
<val1>%d</val1>\n\
<val2>%d</val2>\n\
<val3>%d</val3>\n\
<val4>%d</val4>\n\
<val5>%d</val5>\n\
<val6>%d</val6>\n\
</mag>\n\
</imu_cal>\n", 	acc_cal_values[0], 
		acc_cal_values[1],
		acc_cal_values[2],
		acc_cal_values[3],
		acc_cal_values[4],
		acc_cal_values[5],
 		mag_cal_values[0], 
		mag_cal_values[1],
		mag_cal_values[2],
		mag_cal_values[3],
		mag_cal_values[4],
		mag_cal_values[5]
);
	GS_API_SendTcpData(1, &buffer, len);

	return;
}

#define SAMPLE_PERIOD_US 500000

void *sendData() {
	FILE * fp;
	ssize_t read, len;
	char * line = NULL;
	char sensDatindex[100];
	struct timespec spec;
	long ms_before, ms_after, process_time=0;
	long s_before, s_after;

	//printf("Starting data service thread.\n");
	while (!done) {
		if(clientConnected == 1) {
			// Measure the time it takes to send out the 
			// data to maintain a stable sample rate
			//**********************************************
			clock_gettime(CLOCK_REALTIME, &spec);
			ms_before = (int)round(spec.tv_nsec / 1.0e6);
			s_before = spec.tv_sec; 

			//printf("[%d.%d]\n", spec.tv_sec, ms_before);		
			//**********************************************


			//*** Send one dataset
			len = sprintf(sensDatindex,"<dataset id=\"%d\">\n<timestamp>%d.%d</timestamp>\n\
<system>\n\
<bat_stat>%s</bat_stat>\n\
<bat_volt>%1.2f</bat_volt>\n\
<time>%s</time>\n\
</system>\n\n",	piv_rt_count,
		s_before,
		ms_before,
		" N/A ",
		(float)get_battery_voltage(),
		" N/A ");

			write_alllog_file("all", runtime_count, sensDatindex,len);		
			GS_API_SendTcpData(1, sensDatindex, strlen(sensDatindex));


			//if(piv_rt_count % 20 == 0)
			//	send_cal_data();

			sendTCPStream(&gps_mapped_file);	
			sendTCPStream(&imu_mapped_file);	


			//*** **** *** *** ***						
			len = sprintf(sensDatindex,"\n</dataset>\n");
			
			write_alllog_file("all", runtime_count, sensDatindex, len);

			GS_API_SendTcpData(1, sensDatindex, strlen(sensDatindex));
			
			piv_rt_count+=1;

			// How long did it take ?
			//**********************************************
			clock_gettime(CLOCK_REALTIME, &spec);
			ms_after = round(spec.tv_nsec / 1.0e6);	
			process_time = ((ms_after+(spec.tv_sec - s_before)*1000)- ms_before)*1000;
			//printf("[P: %d S: %d]\n", process_time, SAMPLE_PERIOD_US - process_time);
			//**********************************************
		}
		usleep(SAMPLE_PERIOD_US - process_time - 10000); //The 10000 is an experimental value :)
	}
	pthread_exit(NULL);
}