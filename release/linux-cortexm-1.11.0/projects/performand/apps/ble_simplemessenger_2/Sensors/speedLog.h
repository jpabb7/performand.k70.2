/*
 * speedLog.h
 *
 *   Created on: Nov 18, 2013
 *       Author: Rasmus Koldsø
 * Organisation: University of Southern Denmark - MCI
 */

static long Log_serviceHdls[] = {0x004D, 0x0030};
static int  Log_nServiceHdls = 2;

int Log_initialize(void);
int Log_parseData(char* data, int *i, char* mm_str);
