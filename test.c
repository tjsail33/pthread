#include <stdio.h>
#include <stdlib.h>

void * first_message() {
	printf("First\n");
	pthread_yield();
	printf("Third\n");
	int val = 1;
	pthread_exit(&val);
}

void * second_message() {
	printf("Second\n");
	pthread_yield();
	printf("Fourth\n");
	int val = 5;
	pthread_exit(&val);
}

void main(void) {

	pthread_t t1;
	pthread_t t2;


	pthread_create(&t1, NULL, &first_message, NULL);
	pthread_create(&t2, NULL, &second_message, NULL);
	printf("Starting...\n");
	int* val1;
	pthread_join(t1,(void**)&(val));
	printf("val from 1: %d\n",val);
	int* val2;
	pthread_join(t2,(void**)&(val));
	printf("val from 2: %d\n",val);
	printf("last action\n");
	exit(0);
}

