/**
 * queue.c
 *
 * This file contains the implementation of the job queue
 */
#include <ucontext.h>
#include <pthread.h>

// Constants
#define MAX_NUM_NODES 1000
#define MAX_NUM_COND_VARS 1000
#define MAX_NUM_MUTEX_VARS 1000


// TCB(Thread control Block)
typedef struct TCB {
	pthread_t thread_id;
	ucontext_t thread_context;
} TCB;

// The Node for queue functionality in schedular
typedef struct Node {
	TCB  * thread_cb;
	struct Node * next;
	struct Node * prev;
	struct Node * join_list; // this is a list of all the threads joining on this thread
} Node;


// Array of linked lists(map) for conditional variable queues(size of the max number set)
// Trade off: We are alocating this memory for improved speed when adding threads to the cond. var waiting queues
struct Node *condVarMap[MAX_NUM_COND_VARS];
struct Node *mutexVarMap[MAX_NUM_MUTEX_VARS];

// Buffer for join/exit vals
int joinVals[MAX_NUM_NODES];

// The Schedular Struct
typedef struct Schedular {

	struct Node * head; // The current executing context
	struct Node * tail; // Back of the queue
	int size;
	int maxSize;

	// Vals for thread lib
	int action;
	pthread_t numCreated;
	pthread_t join_id;
	ucontext_t sched_context;

	// Vals for synchronization
	int nextCondId; // Id of the next cond. var in the cond. var map
	int currCondVarId;  // Id of the cond. var under operation

	int nextMutexId; // Id of the next mutex n the mutex var map
	int currMutexVarId;  // Id of the mutex var under operation
} Schedular;


// Add a job to the queue
void addThread(pthread_t *thread, Schedular * s, TCB * block) {
	//fprintf(stdout,"addJob\n");

	// Add thread to ready queue if not full 
	if (canCreateThread(s)) {

		Node * temp = (Node *) malloc(sizeof(Node));

		// Thrad ID of the block
		block->thread_id = ++s->numCreated;
		*thread = s->numCreated;
		
		temp->thread_cb = block;
		temp->next = NULL;
		temp->join_list = NULL;

		// The first job added to the list
		if (s->head == NULL) {
			s->head = temp;
			s->tail = temp;
			s->head->prev = NULL;

		} else {
			s->tail->next = temp;
			s->tail->next->prev = s->tail;
			s->tail = temp;
		}

		// Increment the size of the schedular queue
		s->size++;

		//printf("Created new thread.\n");
		printReadyQueue(s);

	}
}


// Run the next thread in the ready queue
void runNextThread(Schedular * s) {
	//printf("rn1\n");

	// if there is more than one node on the ready queue, move the head to the back
	if(s->tail != s->head) { 
		//printf("rn2\n");

		// Add current TCB to the back of queue
		s->tail->next = s->head;
		s->tail->next->prev = s->tail;

		// Set the new head 
		s->head = s->head->next;
		s->head->prev = NULL;

		// Set the tail correctly
		s->tail = s->tail->next;
		s->tail->next = NULL;
		//printf("rn3\n");
	}
	//printf("rn4\n");

	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	//printf("Yielded.\n");
	printReadyQueue(s);

	// Change context to new TCB context
	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);


}


// Exit the current running thread
void currExit(Schedular * s) {

	
	Node * temp = s->head->join_list;


	// Add list of joins from current TCB to back of ready queue
	while (temp != NULL) {

		//printf("adding back to ready queue\n");

		// Add temp to the back of the queue
		s->tail->next = temp;
		s->tail->next->prev = s->tail;

		// Set temp to the next node in the joining list
		temp = temp->next;

		// Set tail to the end of ready queue
		s->tail = s->tail->next;
		s->tail->next = NULL;

	}

	
	temp = s->head; 

	// Set the next thread in the ready queue to the head
	if (s->head == s->tail) {
		s->head = NULL;
		s->tail = NULL;
	} else {
		s->head = s->head->next;
		s->head->prev = NULL;
	}

	// delete the memory of this node
	free(temp); 

	// Decrement the size of the queue
	s->size--;
	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	//printf("Exited thread.\n");
	printReadyQueue(s);

	// Unless the last thread has exited, swap back to user mode
	if (s->head != NULL) {
		// Change context to new TCB context
		swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);
	} 

}

// Find node with TCB thread_id == id
Node * findTarget(Node * root, pthread_t id) {

	// Check if we hit a dead end
	if (root == NULL) {
		//printf("root null\n");
		return NULL;
	}

	// Check if we have the correct node
	if (root->thread_cb->thread_id == id) {
		//printf("returning root\n");
		return root;
	}

	// Recursively search for the correct node
	Node* n = findTarget(root->next,id);
	Node* j = findTarget(root->join_list,id);

	// Return recursive result
	return (j==NULL) ? n : j;


}

// Look through the mutex and cond. var queues to find the joining thread
Node * findTargetInMaps(Schedular * s, pthread_t id) {

	Node * temp;
	int i,j;

	// Search through cond. var map for the thread
	for (i=0; i<s->nextCondId; i++) {
		temp = condVarMap[i];

		if (temp == NULL) continue;
		while(temp != NULL) {
			if (temp->thread_cb->thread_id == id) return temp;
			temp = temp->next;
		}
	}

	// Search through mutex for the thread
	for (j=0; j<s->nextMutexId; j++) {
		temp = mutexVarMap[j];

		if (temp == NULL) continue;
		while(temp != NULL) {
			if (temp->thread_cb->thread_id == id) return temp;
			temp = temp->next;
		}
	}
}

// Join current running thread to another thread
void join(Schedular * s) {

	// Find the thread we are joing on
	Node * temp = findTarget(s->head, s->join_id);

	if (temp == NULL) findTargetInMaps(s,s->join_id);

	if (temp != NULL) {

		//printf("temp not null\n");

		// Add current TCB to back of its joining queue
		if (temp->join_list == NULL) {
			//printf("jo1\n");
			temp->join_list = s->head; 
			temp = temp->join_list;
		} else {
			//printf("jo2\n");
			temp = temp->join_list;
			while (temp->next != NULL) temp = temp->next;
			temp->next = s->head;
			temp = temp->next;
		}

		// Set head of ready queue to current
		s->head = s->head->next;

		// Check for deadlock
		if(s->head==NULL) {
			//printf("Deadlock achieved!\nExiting now....\n");
			exit(0);
		}

		s->head->prev = NULL;

		// Make the end of the join list NULL
		temp->next = NULL;

		// If a thread terminates, this calls pthread exit for it 
		s->action = 0;

		//if (s->head->join_list == NULL) printf("joinlist is null\n");

		//printf("Thread join.\n");
		printReadyQueue(s);

		// Change context to current TCB context
		swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);


	} else {
		//printf("temp is null\n");
		// If a thread terminates, this calls pthread exit for it 
		s->action = 0;

		// Change context to current TCB context
		swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);
	}
}

// Add the current thread to the correct conditional variable queue
void waitOnCond (Schedular *s) {

	// Get the first node in the queue 
	Node * temp = condVarMap[s->currCondVarId];
	//printf("wc1\n");
	// Add the current thread to the back of the list
	if (temp == NULL) {
		//printf("wc1.5: %d\n",s->currCondVarId);
		condVarMap[s->currCondVarId] = s->head;
		//printf("wc2\n");
		temp = s->head;
	} else {
		//printf("wc2.5\n");
		printReadyQueue(s);
		while(temp->next != NULL) temp = temp->next;
		//printf("wc3\n");
		temp->next = s->head;
		temp = temp->next;
	}

	//printf("%d\n",temp->thread_cb->thread_id);

	// Set head of ready queue to current
	//printf("wc4\n");
	s->head = s->head->next;

	// Check for deadlock
	if(s->head==NULL) {
		//printf("Deadlock achieved!\nExiting now....\n");
		exit(0);
	}
	//printf("wc5\n");
	s->head->prev = NULL;

	// Make the end of the join list NULL
	temp->next = NULL;

	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	// Change context to current TCB context
	//printf("%d\n",s->head->thread_cb->thread_id);

	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);

}

// Take the head of the cond. var queue and put it back on the ready queue
void sig(Schedular *s) {

	// Get the head of the queue
	Node * temp = condVarMap[s->currCondVarId];

	if (temp != NULL) {
		
		addToReadyTail(temp,s,0);
	}

	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	// Change context to current TCB context
	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);
}

// Add all threads waiting on the cond. variable back on the ready queue
void broadcast (Schedular *s) {

	// Get the head of the queue
	Node * temp = condVarMap[s->currCondVarId];

	// Add all threads to the back of the ready queue
	while (temp != NULL) {
		
		addToReadyTail(temp,s,0);

		temp = condVarMap[s->currCondVarId];
	}

	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	// Change context to current TCB context
	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);
}

void lock(Schedular *s) {

	// Get the first node in the queue 
	Node * temp = mutexVarMap[s->currMutexVarId];

	// Add the current thread to the back of the list
	if (temp == NULL) {
		mutexVarMap[s->currMutexVarId] = s->head;
		temp = s->head;
	} else {
		while(temp->next != NULL) temp = temp->next;
		temp->next = s->head;
		temp = temp->next;
	}

	// Set head of ready queue to current
	s->head = s->head->next;

	// Check for Deadlock
	if(s->head==NULL) {
		//printf("Deadlock achieved!\nExiting now....\n");
		exit(0);
	}

	s->head->prev = NULL;

	// Make the end of the join list NULL
	temp->next = NULL;

	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;

	//printf("Just Locked.\n");
	printReadyQueue(s);
	//printf("%d\n",s->head->thread_cb->thread_id);
	// Change context to current TCB context
	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);

}

void unlock(Schedular *s) {
	// Get the head of the queue
	//printf("u0: %d\n",s->currMutexVarId);
	Node * temp = mutexVarMap[s->currMutexVarId];
	//printf("u1\n");
	if (temp != NULL) {
		//printf("u2\n");
		addToReadyTail(temp,s,1);
	}
	//printf("u3\n");
	// If a thread terminates, this calls pthread exit for it 
	s->action = 0;
	//printf("u4\n");

	//printf("Unlocked.\n");
	printReadyQueue(s);

	// Change context to current TCB context
	swapcontext(&s->sched_context,&s->head->thread_cb->thread_context);
}

// Adds a node from a cond. var queue to the back of the ready queue
void addToReadyTail(Node* n,Schedular *s, int isLock) {

	// Add this to the back of the ready queue
	s->tail->next = n;
	s->tail->next->prev = s->tail;
	s->tail = n;

	// Set the head of the queue to the next value
	if (isLock) {
		mutexVarMap[s->currMutexVarId] = n->next;
	} else {
		condVarMap[s->currCondVarId] = n->next;
	}

	// End of the list points to NULL
	s->tail->next = NULL;
}

void printReadyQueue(Schedular *s) {

	Node * temp = s->head;
	//printf("Printing Queue:\n");
	while (temp!=NULL) {
		//printf("%d\n",temp->thread_cb->thread_id);
		temp = temp->next;
	}
	//printf("NULL\n");

}

// Have we reached the maximum number of threads
int canCreateThread(Schedular * s) {
	//fprintf(stdout,"canAddJob\n");
	return(s->size < s->maxSize);
}

// Does the schedular have any threads to run
int isEmpty(Schedular * s) {
  //fprintf(stdout,"isJobAvailable\n");
  return(s->head == NULL);
}
