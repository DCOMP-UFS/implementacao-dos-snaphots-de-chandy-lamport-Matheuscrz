#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h> 
#include <mpi.h>     
#include <time.h>

#define THREAD_NUM 3
#define CLOCK_QUEUE_SIZE 10

typedef struct {
    int p[3]; // Vetor de relógios lógicos para cada processo
    int pid;  // Identificador do processo
} Clock;

pthread_mutex_t outputMutex; // Mutex para garantir acesso exclusivo à fila de saída
pthread_cond_t outputCondEmpty; // Condição para sinalizar que a fila de saída está vazia
pthread_cond_t outputCondFull;  // Condição para sinalizar que a fila de saída está cheia
int outputEnqueueCount = 0;     // Contador de elementos na fila de saída
Clock outputEnqueue[CLOCK_QUEUE_SIZE]; // Fila de saída

pthread_mutex_t inputMutex;  // Mutex para garantir acesso exclusivo à fila de entrada
pthread_cond_t inputCondEmpty; // Condição para sinalizar que a fila de entrada está vazia
pthread_cond_t inputCondFull;  // Condição para sinalizar que a fila de entrada está cheia
int inputEnqueueCount = 0;     // Contador de elementos na fila de entrada
Clock inputEnqueue[CLOCK_QUEUE_SIZE]; // Fila de entrada

int snapshot_done = 0; // Variável para indicar se o snapshot foi realizado

void Event(int pid, Clock *clock) {
    // Incrementa o relógio lógico do processo especificado
    clock->p[pid]++;
    printf("Processo: %d, Relógio: (%d, %d, %d)\n", pid, clock->p[0], clock->p[1], clock->p[2]);
}

Clock dequeue(pthread_mutex_t *mutex, pthread_cond_t *condEmpty, pthread_cond_t *condFull, int *clockCount, Clock *clockQueue) {
    // Obtém um relógio da fila
    Clock clock;
    pthread_mutex_lock(mutex);
    
    while (*clockCount == 0) {
        pthread_cond_wait(condEmpty, mutex);
    }

    clock = clockQueue[0];

    for (int i = 0; i < *clockCount - 1; i++) {
        clockQueue[i] = clockQueue[i + 1];
    }

    (*clockCount)--;
    
    pthread_mutex_unlock(mutex);
    pthread_cond_signal(condFull);
    
    return clock;
}

void enqueue(pthread_mutex_t *mutex, pthread_cond_t *condEmpty, pthread_cond_t *condFull, int *clockCount, Clock clock, Clock *clockQueue) {
    // Insere um relógio na fila
    pthread_mutex_lock(mutex);

    while (*clockCount == CLOCK_QUEUE_SIZE) {
        pthread_cond_wait(condFull, mutex);
    }
    
    Clock temp = clock;
    clockQueue[*clockCount] = temp;
    (*clockCount)++;
    
    pthread_mutex_unlock(mutex);
    pthread_cond_signal(condEmpty);
}

void SendControl(int pid, Clock *clock) {
    // Envia um relógio ao processo especificado
    Event(pid, clock);
    enqueue(&outputMutex, &outputCondEmpty, &outputCondFull, &outputEnqueueCount, *clock, outputEnqueue);
}

Clock* ReceiveControl(int pid, Clock *clock) {
    // Recebe um relógio do processo especificado
    Clock* temp = clock;
    Clock received = dequeue(&inputMutex, &inputCondEmpty, &inputCondFull, &inputEnqueueCount, inputEnqueue);
    for (int i = 0; i < 3; i++) {
        if (temp->p[i] < received.p[i]) {
            temp->p[i] = received.p[i];
        }
    }
    temp->p[pid]++;
    printf("Processo: %d, Relógio: (%d, %d, %d)\n", pid, clock->p[0], clock->p[1], clock->p[2]);
    return temp;
}

void Send(int pid, Clock *clock){
    // Envia um relógio usando MPI
    int mensagem[4];
    mensagem[0] = clock->p[0];
    mensagem[1] = clock->p[1];
    mensagem[2] = clock->p[2];
    mensagem[3] = snapshot_done; // marcação para indicar se snapshot já foi feito
    // MPI_SEND
    MPI_Send(&mensagem, 4, MPI_INT, clock->pid, 0, MPI_COMM_WORLD);
}

void Receive(int pid, Clock *clock){
    // Recebe um relógio usando MPI
    int mensagem[4];
    // MPI_RECV
    MPI_Recv(&mensagem, 4, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    clock->p[0] = mensagem[0];
    clock->p[1] = mensagem[1];
    clock->p[2] = mensagem[2];
    snapshot_done = mensagem[3]; // verifica se snapshot já foi feito
}

void *MainThread(void *args) {
    // Função principal de cada thread
    long id = (long) args;
    int pid = (int) id;
    Clock* clock = malloc(sizeof(Clock));
    
    // Inicializando os campos da estrutura Clock
    clock->p[0] = 0;
    clock->p[1] = 0;
    clock->p[2] = 0;
    clock->pid = 0;
        
    if (pid == 0) {
        // Processo 0
        Event(pid, clock);
        clock->pid = 1;
        SendControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        clock->pid = 2;
        SendControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        clock->pid = 1;
        SendControl(pid, clock);
        Event(pid, clock);
        // Enviar mensagem para iniciar o snapshot
        clock->pid = 1;
        SendControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        if (!snapshot_done) {
            printf("Snapshot em processo %d:\n", pid);
            printf("Relógio: (%d, %d, %d)\n", clock->p[0], clock->p[1], clock->p[2]);
            snapshot_done = 1; // marca que snapshot foi feito
        }
    } else if (pid == 1) {
        // Processo 1
        clock->pid = 0;
        SendControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        if (!snapshot_done) {
            printf("Snapshot em processo %d:\n", pid);
            printf("Relógio: (%d, %d, %d)\n", clock->p[0], clock->p[1], clock->p[2]);
            snapshot_done = 1; // marca que snapshot foi feito
        }
    } else if (pid == 2) {
        // Processo 2
        Event(pid, clock);
        clock->pid = 0;
        SendControl(pid, clock);
        clock = ReceiveControl(pid, clock);
        if (!snapshot_done) {
            printf("Snapshot em processo %d:\n", pid);
            printf("Relógio: (%d, %d, %d)\n", clock->p[0], clock->p[1], clock->p[2]);
            snapshot_done = 1; // marca que snapshot foi feito
        }
    }

    return NULL;
}

void *SendThread(void *args) {
    // Thread de envio
    long pid = (long) args;
    Clock clock;
    
    while(1){
      clock = dequeue(&outputMutex, &outputCondEmpty, &outputCondFull, &outputEnqueueCount, outputEnqueue);
      Send(pid, &clock);
    }

    return NULL;
}

void *ReceiveThread(void *args) {
    // Thread de recebimento
    long pid = (long) args;
    Clock clock;

    while(1){
      Receive(pid, &clock);
      enqueue(&inputMutex, &inputCondEmpty, &inputCondFull, &inputEnqueueCount, clock, inputEnqueue);
    }
 
    return NULL;
}

void process0(){
    // Processo de rank 0
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 0);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 0);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 0);

    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

void process1(){
    // Processo de rank 1
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 1);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 1);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 1);
    
    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

void process2(){
    // Processo de rank 2
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 2);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 2);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 2);
    
    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

int main(int argc, char* argv[]) {
    // Função principal do programa
    int my_rank;
    
    srand(time(NULL)); // Inicializar o gerador de números aleatórios
    
    pthread_mutex_init(&inputMutex, NULL);
    pthread_mutex_init(&outputMutex, NULL);
    pthread_cond_init(&inputCondEmpty, NULL);
    pthread_cond_init(&outputCondEmpty, NULL);
    pthread_cond_init(&inputCondFull, NULL);
    pthread_cond_init(&outputCondFull, NULL);
    
    MPI_Init(NULL, NULL); 
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); 

    if (my_rank == 0) { 
        process0();
    } else if (my_rank == 1) {  
        process1();
    } else if (my_rank == 2) {  
        process2();
    }

    pthread_mutex_destroy(&inputMutex);
    pthread_mutex_destroy(&outputMutex);
    pthread_cond_destroy(&inputCondEmpty);
    pthread_cond_destroy(&outputCondEmpty);
    pthread_cond_destroy(&inputCondFull);
    pthread_cond_destroy(&outputCondFull);
    
    MPI_Finalize();

    return 0;
}
