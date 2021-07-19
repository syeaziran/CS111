/*NAME: Xuan Peng
  Email:syeaziran@g.ucla.edu
  ID:705185066
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <poll.h>
#include <mraa.h>

char scale_arg='F'; //"By default, temperatures should be reported in degrees Fahrenheit."
int period=1; //"defaulting to 1/second"
int ifLog=0;
int ifReport = 1;
FILE *file;

struct timespec ts1;

//refer to TA's slides
#define B 4275
#define R0 100000.0

float convert_temperature_reading(int reading)
{
	float R = 1023.0 / ((float)reading) - 1.0;
	R = R0 * R;

	float C = 1.0 / (log(R / R0) / B + 1 / 298.15) - 273.15;
	float F = (C * 9) / 5 + 32;
	if (scale_arg == 'C')
		return C;
	else
		return F;
}

void print_current_time(int reading)
{
	struct timespec ts;
	struct tm * tm;

	clock_gettime(CLOCK_REALTIME, &ts);
	if (ifReport && ts.tv_sec >= ts1.tv_sec + period)
	{
		tm = localtime(&(ts.tv_sec));
		float tempVal = convert_temperature_reading(reading);
		fprintf(stdout, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, tempVal);

		clock_gettime(CLOCK_REALTIME, &ts1);
		if (ifLog)
			fprintf(file, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, tempVal);
	}
}

void do_when_pushed() //"outputs (and logs) a final sample with the time and the string SHUTDOWN (instead of a temperature)"
{	
	struct timespec ts;
	struct tm * tm;
	clock_gettime(CLOCK_REALTIME, &ts);
	tm = localtime(&ts.tv_sec);
	fprintf(stdout, "%02d:%02d:%02d SHUTDOWN\n", tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (ifLog)
		fprintf(file, "%02d:%02d:%02d SHUTDOWN\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
	exit(0);
}


void process_commands(char *buffer)
{
	if (strcmp(buffer, "SCALE=F") == 0)
		scale_arg = 'F';
	else if (strcmp(buffer, "SCALE=C") == 0)
		scale_arg = 'C';
	else if (strncmp(buffer, "PERIOD=", 7) == 0)
		period = atoi(buffer + 7);
	else if (strcmp(buffer, "STOP") == 0)
		ifReport = 0;
	else if (strcmp(buffer, "START") == 0)
		ifReport = 1;
	else if (strncmp(buffer, "LOG", 3) == 0)
	{}
	else if (strcmp(buffer, "OFF") == 0)
		do_when_pushed();
	else
	{
		fprintf(stderr, "Error:wrong command input!\n");
		exit(1);
	}
}

int main(int argc, char* argv[]) 
{
	static struct option optArgs[] = {
					  {"scale",1, NULL, 's'},
					  {"period",1, NULL , 'p'},
					  {"log",1,  NULL, 'l'},
					  {0, 0, 0, 0}
	};

	int optRet;
	while ((optRet = getopt_long(argc, argv, "spl", optArgs, NULL)) != -1)
	{
		switch (optRet)
		{
		 case 's':
			if (optarg[0] == 'C' || optarg[0] == 'F')
				scale_arg = optarg[0];
			
			else
			{
				fprintf(stderr, "Usage: Unrecognized argument for --scale option!\n");
				exit(1);
			}
			break;

		 case 'p':
			period = atoi(optarg);
			break;

		 case 'l':
			ifLog = 1;
			file = fopen(optarg, "w+");
			if (file == NULL)
			{
				 fprintf(stderr, "Error: fail to open file for --log option!\n");
				 exit(1);
			}
			break;

		 default:
			fprintf(stderr, "Usage: [--scale=#] [--period=#] [--log=#]\n");
			exit(1);

		}//switch end

	}//opt while end

	mraa_gpio_context button;

	button = mraa_gpio_init(60);// "addressed as I/O pin #60"
	if (button == NULL)
	{
		fprintf(stderr, "Error: fail to initialize button!");
		exit(1);
	}
	mraa_gpio_dir(button, MRAA_GPIO_IN);

	mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &do_when_pushed, NULL);//if (push button is pressed)log and exit.

	int temp_ret = 0;
	mraa_aio_context temp_dev = mraa_aio_init(1); //"addressed as I/O pin #1"
	if (temp_dev == NULL)
	{
		fprintf(stderr, "Error: fail to initialize aio!");
		exit(1);
	}

	
	struct pollfd pollfds[1];
	pollfds[0].fd = 0;
	pollfds[0].events = POLLIN;


	clock_gettime(CLOCK_REALTIME, &ts1);
	while (1) 
	{
		temp_ret = mraa_aio_read(temp_dev);
		print_current_time(temp_ret);

	
		int pollRet = poll(pollfds, 1, 0);
		if (pollRet == -1)
		{
			fprintf(stderr, "Error:fail to poll!\n");
			exit(1);
		}

		if (pollfds[0].revents & POLLIN)
		{
			char bigBuf[256];
			memset(bigBuf, 0, 256 * sizeof(char));
			int count = read(0, bigBuf, 256);
			if (count < 0)
			{
				fprintf(stderr, "Error: fail to read from socketfd!\n");
				exit(1);
			}
			//char buffer[16];
			char *buffer = malloc(sizeof(char));
			int i;
			int j = 0;
			for (i = 0; i < count; i++)
			{
				//fprintf(file, "%s", buffer);
				if (bigBuf[i] == '\n')
				{
					buffer[j] = '\0';
					if (ifLog)
					{
						fprintf(file, "%s\n", buffer);
						fflush(file);
					}
					process_commands(buffer);
				//	buffer[0] = '\0';
					free(buffer);
					j = 0;

					buffer = malloc(sizeof(char));
				}
				else 
				{
					buffer = realloc(buffer, (j + 1) * sizeof(char));
					buffer[j] = bigBuf[i];
					j++;
				}
			}
		
		}//ifpoll end

	}//while end

	mraa_gpio_close(button);
	mraa_aio_close(temp_dev);
	fclose(file);
	return 0;
}
