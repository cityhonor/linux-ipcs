#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include "debug.h"

#define SEM_NAME_CLIENT "client_test"
#define SEM_NAME_SERVER "server_test"
#define SHMKEY (1)

static sem_t    *sem_lock_client = NULL;
static sem_t    *sem_lock_server = NULL;
static void     *shm_addr = NULL;
static int      shmid = -1;

void sem_client(int signal)
{
    int ret = 0;
    uint8_t cnt_value = 0;
    if(signal == SIGALRM)
    {
        ret = sem_wait(sem_lock_client);
        if(ret != 0)
        {
            debug_info("sem_wait sem_lock_client failed\r\n");
        }
        cnt_value = ((uint8_t*)shm_addr)[0];
        cnt_value++;
        memcpy(shm_addr, &cnt_value, sizeof(cnt_value));
        ret = sem_post(sem_lock_server);
        if(ret != 0)
        {
            debug_info("sem_post sem_lock_server failed\r\n");
        }
    }
}

void* sem_server(void *arg)
{
    int ret = 0;
    uint8_t cnt_value = 123u;
    memcpy(shm_addr, &cnt_value, sizeof(cnt_value));
    while(1)
    {
        ret = sem_wait(sem_lock_server);
        if(ret != 0)
        {
            debug_info("sem_wait sem_lock_server failed\r\n");
        }
        cnt_value = ((uint8_t*)shm_addr)[0];
        debug_info("cnt_value = %u\r\n", cnt_value);
        ret = sem_post(sem_lock_client);
        if(ret != 0)
        {
            debug_info("sem_post sem_lock_client failed\r\n");
        }
    }
}

int parent_init(void)
{
    int ret = 0;
    sem_lock_client = sem_open(SEM_NAME_CLIENT, O_CREAT | O_EXCL, 0666, 0);
    sem_lock_server = sem_open(SEM_NAME_SERVER, O_CREAT | O_EXCL, 0666, 1);
    if (sem_lock_client == SEM_FAILED)
    {
        sem_lock_client = sem_open(SEM_NAME_CLIENT, 0);
        if (sem_lock_client == SEM_FAILED)
        {
            perror("sem_lock_client open failed\r\n");
            return -1;
        }
    }
    if (sem_lock_server == SEM_FAILED)
    {
        sem_lock_server = sem_open(SEM_NAME_SERVER, 0);
        if (sem_lock_server == SEM_FAILED)
        {
            perror("sem_lock_server open failed\r\n");
            return -1;
        }  
    }

    shmid = shmget(SHMKEY, 0, 0666);
    if (shmid < 0)
    {
        shmid = shmget(SHMKEY, 4096, IPC_CREAT | IPC_EXCL | 0666);
        if (shmid < 0)
        {
            perror("parent shmget");
            return -1;
        }
    }

    shm_addr = shmat(shmid, 0, 0);
    if (shm_addr == (void *)-1)
    {
        perror("parent shmat failed\r\n");
        return -1;
    }
    return 0;
}

int child_init(void)
{
    int ret = 0;
    sem_lock_client = sem_open(SEM_NAME_CLIENT, 0);
    if (sem_lock_client == SEM_FAILED)
    {
        perror("sem_lock_client open failed\r\n");
        return -1;
    }

    sem_lock_server = sem_open(SEM_NAME_SERVER, 0);
    if (sem_lock_server == SEM_FAILED)
    {
        perror("sem_lock_server open failed\r\n");
        return -1;
    }

    usleep(200 * 1000);
    shmid = shmget(SHMKEY, 0, 0666);
    if (shmid < 0)
    {
        shmid = shmget(SHMKEY, 4096, IPC_CREAT | IPC_EXCL | 0666);
        if (shmid < 0)
        {
            perror("child shmget failed\r\n");
            return -1;
        }
    }
    shm_addr = shmat(shmid, 0, 0);
    if (shm_addr == (void *)-1)
    {
        perror("child shmat failed\r\n");
        return -1;
    }
    signal(SIGALRM, sem_client);

    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 40000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 40000;
    ret = setitimer(ITIMER_REAL, &timer, NULL);
    if(ret != 0) 
    {
        debug_error("setitimer error");
        return -1;
    }
    return 0;
}


int main(void)
{
    pthread_t pid;
    int status;
    if ((pid = fork()) < 0) 
    {
        debug_error("fork error");
        exit(-1);
    } 
    else if (pid > 0) 
    { /* parent */
        parent_init();
        wait(&status);
    }
    else
    { /* child */
        child_init();
        status = pthread_create(&pid, NULL, sem_server, NULL);
        if(status != 0) 
        {
            debug_error("pthread_create failed\r\n");
        }
        pthread_join(pid, NULL);
    }
    sem_close(sem_lock_client);
    sem_close(sem_lock_server);
    sem_unlink(SEM_NAME_CLIENT);
    sem_unlink(SEM_NAME_SERVER);
    status = shmctl(shmid, IPC_RMID, NULL);
    if (status < 0)
    {
        perror("shmctl del failed");
    }
    exit(0);
}



