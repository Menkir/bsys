#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "buffer.h"
#include "prodcon-api.h"
#include <assert.h>
#include <time.h>
#include <math.h>
#include "busyloop.h"

//struct for thread arguments

typedef struct{
	double start;
	double stop;
	int delay;
	int upper,lower;
	int id;
} thread_args_t;

//Function prototypes
void* consumer(void* args);
void* producer(void* args);
char* convert(char* string);
void* observe(void* args);

void refreshConsumers();
int nextConsumer();
bool operationsLeft();
bool consumersAreDone();

void* observeProducers(void* args);
bool enoughProduced();
int nextProducer();
bool producersAreDone();
bool additionsLeft();
void refreshProducers();

//mutex and cv
pthread_cond_t cv, pv, currentProducer;
pthread_mutex_t mutex, mutexwait;

//global variables
Buffer buffer, inputBuffer;
time_t sec;
bool verbose = false;
char* output = NULL;
int busyLoopFactor = 0; // default	

int consumerThreads = 1;
pthread_cond_t *vars;
bool* accessConsumer; 
int operations = 0;

int producerThreads = 1;
pthread_cond_t *varsP;
bool* accessProducer;
int additions = 0;
int turn = 0;
bool complete = false;

int
main (int argc, char **argv)
{
	int status;
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&mutexwait, NULL);
	pthread_cond_init(&cv, NULL);	
	pthread_cond_init(&pv, NULL);
	pthread_cond_init(&currentProducer, NULL);

	int c; //argument
	char* input = NULL;
	
	int bufferRows = 20; //default
	int colsPerRows = 20; //default
	
	int delay = 0; // number of millisecs for the passvie sleep
	int upperBorder = 2; //default in sec
	int lowerBorder = 1; //default in sec
	
	srandom((unsigned int) sec);
	
	while ((c = getopt (argc, argv, "vhi:o:L:C:c:p:t:r:R:a:")) != -1)
		switch (c)
		  {
		  case 'v':
		  verbose = true;
			  break;
		  case 'h':
			printf("HELP\n"); // todo:
			printf ("Current Git HEAD commit number: \n");
			const char *gitversion = "git rev-parse HEAD";
			system (gitversion);
			  break;
		  case 'i':
			  input = optarg;
			  break;
		  case 'o':
			  output = optarg;
			  break;
		  case 'L':
			  bufferRows =  atoi(optarg);
			  assert(bufferRows >= 0);
			  break;
		  case 'C':
			  colsPerRows = atoi(optarg);
			  assert(colsPerRows >= 0);
			  break;
		  case 'c':
			  consumerThreads = atoi(optarg);
			  assert(consumerThreads > 0);
			  break;
		  case 'p':
		      producerThreads =  atoi(optarg);
			  assert(producerThreads > 0);
			  break;
		  case 't':
			  delay = atoi(optarg);
			  assert(delay >= 0);
			  break;
		  case 'r':
			  lowerBorder = atoi(optarg);
			  assert(lowerBorder >= 0);
			  break;
		  case 'R':
			  upperBorder = atoi(optarg);
			  assert(upperBorder >= 0);
			  assert(upperBorder > lowerBorder);
			  break;
		  case 'a':
			  busyLoopFactor = atoi(optarg);
			  assert(busyLoopFactor >= 0);
			  break;
		  case '?':
			  if (optopt == 'i')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'o')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'L')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'C')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'c')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'p')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 't')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'r')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'R')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);
			  else if (optopt == 'a')
				  fprintf (stderr,
					   "Option -%c requires an argument.\n",
					   optopt);

			  else if (isprint (optopt))
				  fprintf (stderr, "Unknown option `-%c'.\n",
					   optopt);
			  else
				  fprintf (stderr,
					   "Unknown option character `\\x%x'.\n",
					   optopt);
			  return 1;
		  default:
			  abort ();
		  }
		  
	
	varsP = (pthread_cond_t*) malloc(sizeof(pthread_cond_t) * producerThreads);
	for(int i = 0; i < producerThreads; ++i)
	{
		pthread_cond_init(&varsP[i], NULL);
	}
		  
	vars = (pthread_cond_t*) malloc(sizeof(pthread_cond_t) * consumerThreads);
	for(int i = 0; i < consumerThreads; ++i)
	{
		pthread_cond_init(&vars[i], NULL);
	}
	
	accessConsumer = (bool*) malloc(sizeof(bool) * consumerThreads);
	assert(accessConsumer != NULL);
	refreshConsumers();
	
	accessProducer = (bool*) malloc(producerThreads * sizeof(bool));
	assert(accessProducer != NULL);
	refreshProducers();
	
	initBuffer(&inputBuffer, bufferRows, colsPerRows);
	if(input != NULL)
	{
		printf("[READ FROM FILE]\n"); 
		readFile(&inputBuffer, input);
	}
	else
	{
		printf("[READ FROM STDIN]\n");
		printf("WARNING: DO NOT PASTE FROM CLIPBOARD\n"); 
		readStdin(&inputBuffer);
	}
	
	operations = inputBuffer.tail;
	pthread_t consumers[consumerThreads];
	pthread_t producers[producerThreads];
	pthread_t observer, observerP;
	thread_args_t argsProducer[producerThreads];
	thread_args_t argsConsumer[consumerThreads];
	
	initBuffer(&buffer, bufferRows, colsPerRows);

	pthread_create(&observer, NULL, observe, NULL);
	
	printf("%d Consumer startet ", consumerThreads);
	printIds(consumerThreads);
	for(int i = 0; i < consumerThreads; ++i)
	{
		argsConsumer[i].id = i;
		argsConsumer[i].upper = upperBorder;
		argsConsumer[i].lower = lowerBorder;
		pthread_create(&consumers[i], NULL, (void *(*)(void *))consumer, &argsConsumer[i]);
	}
	
	printf("%d Producer startet ", producerThreads);
	printIds(producerThreads);
	for(int i = 0; i < producerThreads; ++i)
	{
		argsProducer[i].start = round(i * ((double)inputBuffer.tail / producerThreads));
		argsProducer[i].stop = round((i+1) * ((double)inputBuffer.tail / producerThreads));
		argsProducer[i].delay = delay;
		argsProducer[i].id = i;
		pthread_create(&producers[i], NULL, (void *(*)(void *))producer, &argsProducer[i]);
	}
	
	pthread_create(&observerP, NULL, observeProducers, NULL);
	
	//joins
	
	for(int i = 0; i < producerThreads; ++i)
	{
		pthread_join(producers[i], NULL);
	}
	
	for(int i = 0; i < consumerThreads; ++i)
	{
		pthread_join(consumers[i], NULL);
	}
	
	pthread_join(observer,(void*) &status);
	pthread_join(observerP, NULL);
	
	destroyBuffer(&buffer);
	destroyBuffer(&inputBuffer);
	

	//free pointers
	free(vars);
	free(varsP);
	free(accessConsumer);
	free(accessProducer);
	return 0;
}

void* consumer(void* args)
{
	thread_args_t* arg = (thread_args_t*) args;
	while(true)
	{
		//printf("operations %d\n", operations);
		if(operationsLeft() == false)
		{
			pthread_cond_signal(&cv);
			printf("C%d finished\n", arg->id);
			break;
		}
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&vars[arg->id], &mutex);
		
		
		if(operationsLeft() == false)
		{
			pthread_mutex_unlock(&mutex);
			printf("C%d finished\n", arg->id);
			break;
		}
		
		accessConsumer[arg->id] = true;
	
		char* c = pop(&buffer);
		if(*c == '\0')
		{	
			pthread_mutex_unlock(&mutex);
			pthread_cond_signal(&pv);
			continue;
		}
		--operations;
		
		if(verbose == true)
			printf("C%d consumes %s\n",arg->id, c);
		
		c = convert(c);
		if(output != NULL)
		{
			FILE* file = fopen(output, "a");
			fputs(c, file);
			fputc('\n', file);
		}
		
		pthread_mutex_unlock(&mutex);

		time(&sec);	
		int s = 0;
		s = random () % (arg->upper + 1 - arg->lower) + arg->lower;
		if(busyLoopFactor != 0)
		{
		
			int asec, msec;
			asec = (busyLoopFactor / 1000) + s;
			msec = (busyLoopFactor - (asec*1000) + s*1000);
			s = busyLoop(asec, msec);
		}
		else
		{
			sleep (s);
			s*= 1000;
		}

		if(verbose==true)
			printf("C%d reports (%d) %s\n",arg->id, s, c); //needs id
		free(c);
		pthread_cond_signal(&pv);
	}
	pthread_exit(0);
}

void* producer(void* args)
{
	thread_args_t* arg = (thread_args_t*) args;
	
	pthread_mutex_lock(&mutex);
	while(turn != arg->id)
		pthread_cond_wait(&varsP[arg->id], &mutex);
	pthread_mutex_unlock(&mutex);
	
	pthread_mutex_lock(&mutex);
	for(int i = (int) arg->start; i  < (int)arg->stop; ++i)
	{
		
		//read from input
		char* input = pop(&inputBuffer);
		add(&buffer, input);
		++additions;
		accessProducer[arg->id] = true;
		if(verbose == true)
			printf("P%d produces %s\n", arg->id, input);
		free(input);

	}
	pthread_mutex_unlock(&mutex);
	if(arg->delay != 0)
		sleep(arg->delay/1000);
	complete = true;
	pthread_cond_signal(&cv);
	printf("P%d finished\n", arg->id);
	pthread_exit(0);
}

char* convert(char* string)
{
	for(int i = 0; ; ++i)
	{
		string[i] = toupper(string[i]);
		if(string[i] == '\0')
			break;
	}
	return string;
}

void* observe(void* arg)
{
	while(true)
	{
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cv, &mutex);
		pthread_mutex_unlock(&mutex);
			
		//release next consumer thread
		if(consumersAreDone() == true && operationsLeft() == true)
		{
			refreshConsumers();
		} 
				
		if(operationsLeft() == false)
			break;

		pthread_cond_signal(&vars[nextConsumer()]);

	}

	for(int i = 0; i < consumerThreads; ++i)
	{
		pthread_cond_signal(&vars[i]);
	}
	pthread_exit(0);	
}

void* observeProducers(void* args)
{
	int ctr = 0;
	while(true)
	{
				
		if(enoughProduced() == true)
			break;
			
		if(complete == true)
		{
			turn = nextProducer();
			pthread_cond_signal(&varsP[turn]);
			complete = false;
		}
		else
		{
			if(ctr == 0)
				pthread_cond_signal(&varsP[turn]);
			
			pthread_cond_signal(&currentProducer);
		}

		if(producersAreDone() == true && additionsLeft() == true)
		{
			refreshProducers();
		} 
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&pv, &mutex);
		pthread_mutex_unlock(&mutex);
		
	}
	
	while(operationsLeft() == true)
	{
		refreshConsumers();
		pthread_cond_signal(&vars[nextConsumer()]);
		
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&pv, &mutex);
		pthread_mutex_unlock(&mutex);
	}
	pthread_exit(0);
}

bool enoughProduced()
{
	if(additions == inputBuffer.tail)
		return true;
	return false;
}

int nextProducer()
{
	for(int i = 0; i < producerThreads; ++i)
	{
		if(accessProducer[i] == false)
			return i;
	}
	
	return -1;
}



bool producersAreDone()
{
	for(int i = 0; i < producerThreads; ++i)
	{
		if(accessProducer[i] == false)
			return false;
	}
	
	return true;
}

bool additionsLeft()
{
	if(additions == inputBuffer.tail)
		return false;
	
	return true;
}

void refreshProducers()
{
	for(int i = 0; i < producerThreads; ++i)
	{
		accessProducer[i] = false;
	}
}

bool consumersAreDone()
{
	for(int i = 0; i < consumerThreads; ++i)
	{
		if(accessConsumer[i] == false)
			return false;
	}
	
	return true;
}

bool operationsLeft()
{
	if(operations == 0)
		return false;
	
	return true;
}

int nextConsumer()
{
	for(int i = 0; i < consumerThreads; ++i)
	{
		if(accessConsumer[i] == false)
			return i;
	}
	
	return -1;
}

void refreshConsumers()
{
	for(int i = 0; i < consumerThreads; ++i)
	{
		accessConsumer[i] = false;
	}
}
