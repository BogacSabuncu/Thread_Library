#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <semaphore.h>

#define JB_BX 0
#define JB_SI 1
#define JB_DI 2
#define JB_BP 3
#define JB_SP 4
#define JB_PC 5

#define MAX_THREAD 128
#define MAX_SEMS 256
#define P_TIMER 50000
#define STACK_SIZE 32767

#define READY 1
#define RUNNING 2
#define EXITED 3
#define BLOCKED 4


static int ptr_mangle(int p);
void pthread_lock();
void pthread_unlock();
int pthread_join(pthread_t thread, void **value_ptr);
void next_runnable();
void schedule();
void alarm_handler(int signo);
void alarm_set();
void pthread_exit(void *v_ptr);
void pthread_firsttime ();
int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start_routine)(void *),void *arg);
pthread_t pthread_self();
int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);
void pthread_exit_wrapper();

int firsttime = 0;
int threads_index = 1;//thread total number of threads
int current = 0;//current thread
int next_thread = 0;//next threads
int running_counter = 0; //counts the running threads;
int running_threads = 0;//counts the running number of threads
int sem_index = 1;//index for sems
int next_sem = 0;//next sem

//initialize the status
//enum status{READY, RUNNING, EXITED};

//creating a TCB struct
struct TCB
{
    pthread_t thread_id;
    int status;
    jmp_buf thread_state;
    void *stack;
    pthread_t join_arry[MAX_THREAD];
    void *exit_value;

};

struct SEMA
{
    sem_t *sem_ptr;
    unsigned int sem_id;
    unsigned int sem_value;
    int sem_on;
    pthread_t sem_blocked[256];
    int sem_in;
    int sem_out;

};


struct TCB threads_arry[(MAX_THREAD)];
struct SEMA sema_arry[(MAX_SEMS)];


static int ptr_mangle(int p)
{
    unsigned int ret;
    asm(" movl %1, %%eax;\n"
    " xorl %%gs:0x18, %%eax;"
    " roll $0x9, %%eax;"
    " movl %%eax, %0;"
    : "=r"(ret)
    : "r"(p)
    : "%eax");
    return ret;
}

void pthread_exit_wrapper()
{
 unsigned int res;
 asm("movl %%eax, %0\n":"=r"(res));
 pthread_exit((void *) res);
}

void lock()
{
     sigset_t sig;
     sigemptyset(&sig);
     sigaddset(&sig, SIGALRM);
     sigprocmask(SIG_BLOCK, &sig, NULL);
}

void unlock()
{
     sigset_t sig;
     sigemptyset(&sig);
     sigaddset(&sig, SIGALRM);
     sigprocmask(SIG_UNBLOCK, &sig, NULL);
}

int pthread_join(pthread_t thread, void **value_ptr)
{
    lock();
    int k = 0;
    for(k; k< MAX_THREAD; k++)
    {
        if(threads_arry[k].thread_id == thread)
        {
            break;
        }
    }

    if(threads_arry[k].status != EXITED)
    {
        threads_arry[current].status = BLOCKED;//blocks the current thread

        int i = 0;
        for(i; i < MAX_THREAD; i++)
        {
            if(threads_arry[i].thread_id == thread)//checks if any of the threads in the array has that thread id
            {
                int j = 0;
                for(j; j < MAX_THREAD; j++)
                {
                    if(threads_arry[i].join_arry[j] == 0)//if the next value is 0
                    {
                        threads_arry[i].join_arry[j] = threads_arry[current].thread_id;//the value is now the thread id
                        break;//breaks the for loop
                    }
                }
            }
        }
        schedule();
    }

    if(value_ptr !=NULL)
    {
        *value_ptr = threads_arry[k].exit_value;
    }
    unlock();
    return 0;
}


void next_runnable()
{   
    //printf("next_runnable called\n");
    int counter_r = 0;
    while(counter_r < MAX_THREAD)
    {   
        //printf("counter: %d \n", counter_r);
        //printf("current: %d \n", current);
        counter_r++;
        current++;
        if (current == MAX_THREAD)
        {
            //printf("Round Robined %d \n", i);
            current = 0;
        }
        if(threads_arry[current].status == READY)
        {
            //printf("This is the next thread: %d\n", current);
            return;
        }
    }
    return;
}

void schedule()
{
    lock();
    if (setjmp(threads_arry[current].thread_state) == 0)
    {
        //printf("schedule called\n");
        next_runnable();
        longjmp(threads_arry[current].thread_state, 1);
        
    }
    else
    {
        unlock();
        return;
    }
}

void alarm_handler(int signo)
{
    schedule();
}

void alarm_set()
{
    struct sigaction sigact;
    sigact.sa_handler = alarm_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_NODEFER;
    sigaction(SIGALRM, &sigact, NULL);

    if (ualarm(P_TIMER, P_TIMER) < 0)
    {
        perror("schedule"); 
    }
}


void pthread_exit(void *v_ptr)
{
    lock();
    //printf("Thread %d: EXITED\n",(unsigned int) threads_arry[current].thread_id);
    if(threads_arry[current].join_arry[0] != 0)//if the join array is not empty
    {
        int i = 0;
        while(threads_arry[current].join_arry[i] != 0)  //while the next value is not 0
        {
            int j = 0;
            for(j; j<MAX_THREAD; j++)
            {
                if(threads_arry[j].thread_id = threads_arry[current].join_arry[i])//if the thread_id is equal to the thread id in join
                {
                    threads_arry[j].status = READY;//sets the status to ready
                    break;
                }
            }
            i++;
        }
    }

    threads_arry[current].exit_value = v_ptr;
    threads_arry[current].status = EXITED;

    free(threads_arry[current].stack);

    schedule();
    __builtin_unreachable;
}


void pthread_firsttime ()
{
    threads_arry[next_thread].thread_id = threads_index;
    threads_arry[next_thread].status = READY;
    threads_arry[next_thread].stack = malloc(STACK_SIZE);
    setjmp(threads_arry[next_thread].thread_state);

    threads_index ++;
    alarm_set();
}

int pthread_create( 
pthread_t *thread,      
const pthread_attr_t *attr,     
void *(*start_routine)(void *),     
void *arg)
{  
    lock();
    //if first call to the function starts the timer
    if(firsttime == 0)
    {
        pthread_firsttime();
        firsttime = 1;
    }
    
    if(next_thread < MAX_THREAD)
    {
    next_thread++;
    
    //printf("pcreate called\n");
    //printf("thread index: %d\n", threads_index); 
    //creating a newthread
    threads_arry[next_thread].thread_id = threads_index;
    *thread = threads_arry[next_thread].thread_id;
    threads_arry[next_thread].status = READY;
    threads_arry[next_thread].stack = malloc(STACK_SIZE);//alocating stac
    
    
    void *esp = threads_arry[next_thread].stack + STACK_SIZE;//going to the top of the stack
    
    esp -= 4;
    *((unsigned long int*)esp) = (unsigned long int)arg;
    esp -= 4; 
    *((unsigned long int*)esp) = (unsigned long int)pthread_exit_wrapper;
    
    setjmp(threads_arry[next_thread].thread_state);

    threads_arry[next_thread].thread_state[0].__jmpbuf[JB_SP] = ptr_mangle((unsigned long int) esp);
    threads_arry[next_thread].thread_state[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int) start_routine);
    //add to index

    threads_index ++;
    unlock();
    schedule();
    return 0;
    }
    else
    {
        unlock();
        return -1;
    }
}
    
pthread_t pthread_self()
{
    return threads_arry[current].thread_id;
}

int init_called = 0;

int sem_init(sem_t *sem, int pshared, unsigned value)
{

    lock();
    int i = 0;
    for (i; i < sem_index; i++)//loops through the array of semas
    {
        if(sema_arry[i].sem_ptr == sem)//if the sema exists 
        {
            return -1;//return error
        }
    }
    if(sem_index < MAX_SEMS)// if the sem_index its less thab the MAX SEMA
    {
        sema_arry[next_sem].sem_id = sem_index;
        sema_arry[next_sem].sem_value = value;
        sema_arry[next_sem].sem_ptr = sem;
        sema_arry[next_sem].sem_on = 1;
        sema_arry[next_sem].sem_in = 0;
        sema_arry[next_sem].sem_out = 0;

        int k = 0;
        for(k; k < 256; k++)
        {
            sema_arry[next_sem].sem_blocked[k] = 0;
        }

        next_sem++;
        sem_index ++;
        unlock();
        return 0;
    }
    else
    {
        unlock();
        return -1;
    }

}

int sem_wait(sem_t *sem)
{
    lock();
    int i = 0;
    for (i; i< sem_index; i++)
    {
        if(sema_arry[i].sem_ptr == sem && sema_arry[i].sem_on == 1)
        {
            if(sema_arry[i].sem_value > 0)
            {
                sema_arry[i].sem_value--;
                unlock();
            }
            else
            {
                int position = sema_arry[i].sem_in;
                threads_arry[current].status = BLOCKED;
                if(sema_arry[i].sem_blocked[position] == 0)
                {
                    sema_arry[i].sem_blocked[position] = threads_arry[current].thread_id;
                    sema_arry[i].sem_in++;
                }
                else
                {
                    unlock();
                    return -1;
                }
                schedule();
                lock();
                //blocks thread
                //adds the thread to q
                //calls schedule
                //unlock
            }
        }
    }
    unlock();
    return 0;
}

int sem_post(sem_t *sem)
{
    lock();
    int i = 0;
    for (i; i< sem_index; i++)
    {
        if(sema_arry[i].sem_ptr == sem && sema_arry[i].sem_on == 1)
        {
            sema_arry[i].sem_value++;
            int position = sema_arry[i].sem_out;

            if(sema_arry[i].sem_blocked[position] != 0)
            {
                int k = 0;
                for(k; k < 256; k++)
                {
                    if(threads_arry[k].thread_id == sema_arry[i].sem_blocked[position])
                    {
                        break;
                    } 
                }
                threads_arry[k].status = READY;
                sema_arry[i].sem_out++;
            }
            //check threads waiting
            //wake one up
            //unlock
            //sem_in ++
        }
    }
    unlock();
    return 0;
}

int sem_destroy(sem_t *sem)
{
    lock();
    int i = 0;
    for(i; i< sem_index; i++)//loop throuht the sema array
    {
        if(sema_arry[i].sem_ptr == sem)// find the correct sema
        {
            sema_arry[i].sem_on = 0;
            unlock();
            return 0;
        }
        else
        {
            unlock();
            return -1;
        }
    }
}