#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fnmatch.h>
#include <pthread.h>
#include <signal.h>

int number_thread_live,number_file_found,number_thread ,if_finish;
pthread_mutex_t  lock;
pthread_mutex_t  if_finish_lock;
pthread_cond_t  count_threshold_cv;
char* text;
pthread_t* my_thread_array;	


//----------------------------------------------------------Queue definition and queue functions------------------------
// A linked list (LL) node to store a queue entry 


struct QNode {
    char * path;
    struct QNode* next;
};

// The queue, front stores the front node of LL and rear stores the 
// last node of LL 
struct Queue {
    struct QNode* prev;
};

// A utility function to create a new linked list node. 
struct QNode* newNode(char * dp)
{
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->path = dp;
    temp->next = NULL;
    return temp;
}
// A utility function to create an empty queue 
struct Queue* createQueue()
{
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->prev = NULL;
    return q;
}

// The function to add a key k to q 
void enQueue(struct Queue* q, char* dp)
{
	pthread_mutex_lock( &lock );
    struct QNode* temp = newNode(dp);
    struct QNode* temp1;
 
    if (q->prev == NULL) {
        q->prev = temp;
        pthread_mutex_unlock( &lock );
        return;
    }

    // Add the new node at the end of queue and change rear
    
    temp1=q->prev;
    q->prev=temp;
    temp->next=temp1;
    printf("push %s \n",dp);
    pthread_mutex_unlock( &lock );
}
char* deQueue(struct Queue* q)
{
	
	pthread_mutex_lock( &lock );
    // If queue is empty, return NULL. 
    if (q->prev == NULL){
    	 pthread_mutex_unlock( &lock );
        return NULL;
     }
    // Store previous front and move front one node ahead 
    struct QNode* temp = q->prev;
 
    while(temp->next!=NULL){
    	temp = temp->next;
    }
    //check if queue is empty 
   //***********
	char* buff = temp->path;
  
    struct QNode* temp1 = q->prev;
    
    if (temp1->next == NULL)
    {
   	 	q->prev = NULL;
    }
    else{
  
    while(temp1->next->next!=NULL){
    	temp1 = temp1->next;
    	}
    }
    
    temp1->next=NULL;
    free(temp);
    pthread_mutex_unlock( &lock );
    return buff;
}
//-------------------------------------------------------------------------------------------------------------





struct Queue* fifo;

void browse(const char* path,struct Queue* fifo,char* text){

	DIR *dir = opendir(path);
	struct dirent *entry;
	if (! dir) { 
		perror("error open directory");
		pthread_exit(NULL);
	 }

	 
	while((entry = readdir(dir)) != NULL){
		char* buff = (char*)malloc(sizeof(char)*(strlen(path)+strlen(entry->d_name)+2));
		sprintf(buff,"%s/%s",path,entry->d_name);
		if(strcmp(entry->d_name,"..") == 0 ||  strcmp(entry->d_name, ".") == 0){free(buff); continue;}
		if(entry->d_type==DT_DIR){
			enQueue(fifo,buff);
			pthread_cond_signal(&count_threshold_cv);
		}
		else
		{
			
			if(strstr(entry->d_name,text )!=NULL){
				
				__sync_fetch_and_add(&number_file_found, 1);
				printf("%s\n",buff);
			}
		}
		
	 }
	closedir(dir);
}


void handler_cancel(void *arg)
{
    pthread_mutex_t *pm = (pthread_mutex_t *)arg;
    pthread_mutex_unlock(pm);
}



void exit_all_thread(int my_thread){
	for(int i=0;i<number_thread;i++){
		if(my_thread_array[i]!=my_thread){
		pthread_cancel(my_thread_array[i]);
		}
	}
	
	return;
}

void handler(){
 	pthread_mutex_unlock( &lock );
	exit_all_thread(-1);
	printf("Search stopped, found %d files\n",number_file_found);
	pthread_cond_destroy(&count_threshold_cv);
	pthread_mutex_destroy(&lock);
	
}


void searching_thread(int a){
	pthread_cleanup_push(handler_cancel, (void *)&lock);
	signal(SIGINT, &handler);
	char* path=NULL;
	
	while(1)
	{
	
	 pthread_mutex_lock( &lock );
	
		while(fifo->prev==NULL)
		{
		 
			if(number_thread_live==0)
			{
				pthread_mutex_unlock(&lock);
				pthread_mutex_lock( &if_finish_lock );
				if(if_finish){
					pthread_mutex_unlock( &if_finish_lock );
					pthread_exit(0);
				}
				else{
				if_finish=1;
				pthread_mutex_unlock( &if_finish_lock );
				exit_all_thread(pthread_self());
				pthread_exit(0);
				}
			}
			else
			{
			pthread_mutex_unlock(&lock);
			pthread_cond_wait(&count_threshold_cv, &lock);
			pthread_testcancel();	
			}
		}
	pthread_mutex_unlock(&lock);
	pthread_testcancel();	
	
	path=deQueue(fifo);
	
	if(path==NULL) continue;
	__sync_fetch_and_add(&number_thread_live,1);
	sleep(2);
	browse(path,fifo,text);
	
	
	__sync_fetch_and_add(&number_thread_live, -1);
	}
	pthread_cleanup_pop(1);
}

//add function that open the mutex in cancel using push



int main(int argc,char* argv[]) {
	int rc;
	DIR *dir = opendir(argv[1]);
	if (! dir) {
		printf("Error in path\n");
		return 0;
	}
	closedir(dir);
	if(argc!=4){
		printf("Error in number argument\n");
		return 0;
	}
	if_finish=0;
	text=argv[2];
	fifo =createQueue();
	
	pthread_cond_init (&count_threshold_cv, NULL);
	rc = pthread_mutex_init( &lock, NULL );
	if(rc!=0) {perror("ERROR in pthread_mutex_init() \n");exit(1);}
	
	pthread_mutex_init(&if_finish_lock, NULL);
	
	number_thread=atoi(argv[3]);
	char* path=(char*)malloc(sizeof(char)*strlen(argv[1]));
	strcpy(path,argv[1]);
	enQueue(fifo,path);
	int thread;
	my_thread_array=(pthread_t*)malloc(sizeof(pthread_t)*(number_thread));
	for(int i=0;i<number_thread;i++){
		
		thread=pthread_create(&my_thread_array[i], NULL,(void*)searching_thread, NULL);
		
		if(thread!=0){
			perror("error create thread"); exit(1);
		}
		
	}

	for(int i=0;i<number_thread;i++){
		thread=pthread_join(my_thread_array[i],NULL);
		if(thread!=0){
			perror("error join thread"); exit(1);
		}
	}
	printf("Done searching, found %d files\n",number_file_found);
	return 0;
}
