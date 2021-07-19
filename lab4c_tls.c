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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

char scale_arg='F'; //"By default, temperatures should be reported in degrees Fahrenheit."
int period=1; //"defaulting to 1/second"
int ifLog=0;
int ifReport = 1;
FILE *file;

struct timespec ts1;

//refer to TA's slides
#define B 4275
#define R0 100000.0

char *id;
char *host_name;
int port = 0;
int sockfd;
struct hostent *server;
struct sockaddr_in serv_addr;
SSL *sslClient;
SSL_CTX *newContext;
char* buffer;

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
		tempVal = (int)(10.0 * tempVal + 0.5) / 10.0;
		if (tempVal >=10.0) {
			char temp[14];
			sprintf(temp, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, tempVal);
			SSL_write(sslClient, temp, sizeof(temp));
		}
		/*
		else if (tempVal == 10.0f) {
			char temp[14];
			sprintf(temp, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, tempVal);
			SSL_write(sslClient, temp, sizeof(temp));
		}*/
		else
		{
			char temp[13];
			sprintf(temp, "%02d:%02d:%02d %.1f\n", tm->tm_hour, tm->tm_min, tm->tm_sec, tempVal);
			SSL_write(sslClient, temp, sizeof(temp));
		}

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
	char temp[18];
	sprintf(temp, "%02d:%02d:%02d SHUTDOWN\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
	SSL_write(sslClient, temp, sizeof(temp));

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
					  {"id",1, NULL, 'i'},
					  {"host",1, NULL, 'h'},
					  {0, 0, 0, 0}
	};

	int optRet;
	while ((optRet = getopt_long(argc, argv, "splih", optArgs, NULL)) != -1)
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

		 case 'i':
			 id = optarg;
			 break;

		 case 'h':
			 host_name = malloc((strlen(optarg) + 1) * sizeof(char));
			 strcpy(host_name, optarg);
			 break;

		 default:
			fprintf(stderr, "Usage: [--scale=#] [--period=#] [--log=#][--id=#][--host=#][port#]\n");
			exit(1);

		}//switch end

	}//opt while end


	if (strlen(id) != 9)  //check id exist and valid
	{
		fprintf(stderr, "Error: wrong ID number!\n");
		exit(1);
	}

	if (strlen(host_name) == 0) //check host_name exist
	{
		fprintf(stderr, "Error: wrong host name!\n");
		exit(1);
	}

	if (!ifLog)//check log option
	{
		fprintf(stderr, "Error: miss log option!\n");
		exit(1);
	}

	port = atoi(argv[optind]);
	if (port <= 0)
	{
		fprintf(stderr, "Error: wrong port number!\n");
		exit(1);
	}

	//from slides:
	sockfd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET: IPv4, SOCK_STREAM: TCP connection
	server = gethostbyname(host_name); // convert host_name to IP addr
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET; //address is Ipv4
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length); //copy ip address from server to serv_addr
	serv_addr.sin_port = htons(port); //setup the port
	connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));  //initiate the connection to server

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	newContext = SSL_CTX_new(TLSv1_client_method()); //one context per server
	//Attach the SSL to a socket :
	sslClient = SSL_new(newContext);
	SSL_set_fd(sslClient, sockfd);	
	SSL_connect(sslClient);


	char idbuf[13];
	sprintf(idbuf, "ID=%s\n", id);
	SSL_write(sslClient, idbuf, sizeof(idbuf));
	fprintf(file, "ID=%s\n", id);

	int temp_ret = 0;
	mraa_aio_context temp_dev = mraa_aio_init(1); //"addressed as I/O pin #1"
	if (temp_dev == NULL)
	{
		fprintf(stderr, "Error: fail to initialize aio!");
		exit(1);
	}

	struct pollfd pollfds[1];
	pollfds[0].fd = sockfd;
	pollfds[0].events = POLLIN;


	clock_gettime(CLOCK_REALTIME, &ts1);

	buffer = malloc(sizeof(char));
	int j = 0;
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
			char tempBuf[256];
			memset(tempBuf, 0, 256 * sizeof(char));
			int count = SSL_read(sslClient, tempBuf, 256);
			if (count < 0)
			{
				fprintf(stderr, "Error: fail to read from socketfd!\n");
				exit(1);
			}
			int i;
			for (i = 0; i < count; i++)
			{
				//fprintf(file, "%s", buffer);
				if (tempBuf[i] == '\n')
				{
					buffer[j] = '\0';
					if (ifLog)
					{
						fprintf(file, "%s\n", buffer);
						fflush(file);
					}
					process_commands(buffer);

					//buffer[0] = '\0';
					free(buffer);
					j = 0;

					buffer = malloc(sizeof(char));
				}
				else
				{
					buffer = realloc(buffer, (j + 1) * sizeof(char));
					buffer[j] = tempBuf[i];
					j++;		
				}
			}
		
		}//ifpoll end

	}//while end

	SSL_shutdown(sslClient);
	SSL_free(sslClient);
	free(host_name);
	mraa_aio_close(temp_dev);
	fclose(file);
	return 0;
}
