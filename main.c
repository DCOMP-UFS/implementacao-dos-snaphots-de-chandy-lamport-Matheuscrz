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
    int snapshot[3]; // Estado do snapshot
    int snapshot_done; // Indica se o snapshot foi realizado
} Clock;
typedef struct {    
    pthread_mutex_t mutex;
    pthread_cond_t condEmpty;
    pthread_cond_t condFull;
    int count;
    Clock queue[CLOCK_QUEUE_SIZE];
} Queue;

Queue inputQueue;
Queue outputQueue;

int snapshot_done = 0; // Variável para indicar se o snapshot foi realizado

void Snapshot(Clock *clock);

void Event(int pid, Clock *clock) {
    // Incrementa o relógio lógico do processo especificado
    clock->p[pid]++;
    printf("Processo: %d, Relógio: (%d, %d, %d)\n", pid, clock->p[0], clock->p[1], clock->p[2]);
}

Clock dequeue(Queue *queue) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0) {
        pthread_cond_wait(&queue->condEmpty, &queue->mutex);
    }

    Clock clock = queue->queue[0];

    for (int i = 0; i < queue->count - 1; i++) {
        queue->queue[i] = queue->queue[i + 1];
    }

    queue->count--;

    pthread_cond_signal(&queue->condFull);
    pthread_mutex_unlock(&queue->mutex);

    return clock;
}

void enqueue(Queue *queue, Clock clock) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == CLOCK_QUEUE_SIZE) {
        pthread_cond_wait(&queue->condFull, &queue->mutex);
    }
    
    queue->queue[queue->count] = clock;
    queue->count++;

    pthread_cond_signal(&queue->condEmpty);
    pthread_mutex_unlock(&queue->mutex);
}

void SendControl(int pid, Clock *clock) {
    // Envia um relógio ao processo especificado
    Event(pid, clock);
    enqueue(&outputQueue, *clock);
}

Clock* ReceiveControl(int pid, Clock *clock) {
    // Recebe um relógio do processo especificado
    Clock* temp = clock;
    Clock received = dequeue(&inputQueue);
    for (int i = 0; i < 3; i++) {
        if (temp->p[i] < received.p[i]) {
            temp->p[i] = received.p[i];
        }
    }
    temp->p[pid]++;
    printf("Processo: %d, Relógio: (%d, %d, %d)\n", pid, clock->p[0], clock->p[1], clock->p[2]);

    // Se uma mensagem de snapshot foi recebida e um snapshot ainda não foi realizado, inicie o snapshot
    if (received.snapshot_done && !temp->snapshot_done) {
        Snapshot(temp);
    }

    return temp;
}

void Snapshot(Clock *clock) {
    // Salva o estado do relógio
    for (int i = 0; i < 3; i++) {
        clock->snapshot[i] = clock->p[i];
    }
    clock->snapshot_done = 1;

    // Envia uma mensagem para todos os outros processos para iniciar o snapshot
    for (int i = 0; i < 3; i++) {
        if (i != clock->pid) {
            SendControl(i, clock);
        }
    }
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
      clock = dequeue(&outputQueue);
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
      enqueue(&inputQueue, clock);
    }
 
    return NULL;
}

void process0(){
    Clock clock0;
    // Processo de rank 0
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 0);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 0);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 0);

    // Inicia um snapshot de forma aleatória
    if (rand() % 10 < 5) { // 50% de chance de iniciar um snapshot
        Snapshot(&clock0);
        // Imprime o estado do snapshot do processo
        printf("Snapshot do processo 0: (%d, %d, %d)\n", clock0.p[0], clock0.p[1], clock0.p[2]);
    }

    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

void process1(){
    Clock clock1;
    // Processo de rank 1
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 1);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 1);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 1);
    
    // Inicia um snapshot de forma aleatória
    if (rand() % 10 < 5) { // 50% de chance de iniciar um snapshot
        Snapshot(&clock1);
        // Imprime o estado do snapshot do processo
        printf("Snapshot do processo 1: (%d, %d, %d)\n", clock1.p[0], clock1.p[1], clock1.p[2]);
    }

    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

void process2(){
    Clock clock2;
    // Processo de rank 2
    pthread_t thread[THREAD_NUM];
    pthread_create(&thread[0], NULL, &MainThread, (void*) 2);
    pthread_create(&thread[1], NULL, &SendThread, (void*) 2);
    pthread_create(&thread[2], NULL, &ReceiveThread, (void*) 2);
    
    // Inicia um snapshot de forma aleatória
    if (rand() % 10 < 5) { // 50% de chance de iniciar um snapshot
        Snapshot(&clock2);
        // Imprime o estado do snapshot do processo
        printf("Snapshot do processo 2: (%d, %d, %d)\n", clock2.p[0], clock2.p[1], clock2.p[2]);
    }

    for (int i = 0; i < THREAD_NUM; i++){  
        if (pthread_join(thread[i], NULL) != 0) {
            perror("Falha ao juntar a thread");
        }
    }
}

int main() {
    int my_rank;
    
    srand(time(NULL)); // Inicializar o gerador de números aleatórios
    
    // Inicializar as filas de entrada e saída
    pthread_mutex_init(&inputQueue.mutex, NULL);
    pthread_mutex_init(&outputQueue.mutex, NULL);
    pthread_cond_init(&inputQueue.condEmpty, NULL);
    pthread_cond_init(&outputQueue.condEmpty, NULL);
    pthread_cond_init(&inputQueue.condFull, NULL);
    pthread_cond_init(&outputQueue.condFull, NULL);
    inputQueue.count = 0;
    outputQueue.count = 0;

    MPI_Init(NULL, NULL); 
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank); 

    if (my_rank == 0) { 
        process0();
    } else if (my_rank == 1) {  
        process1();
    } else if (my_rank == 2) {  
        process2();
    }

    // Destruir as filas de entrada e saída
    pthread_mutex_destroy(&inputQueue.mutex);
    pthread_mutex_destroy(&outputQueue.mutex);
    pthread_cond_destroy(&inputQueue.condEmpty);
    pthread_cond_destroy(&outputQueue.condEmpty);
    pthread_cond_destroy(&inputQueue.condFull);
    pthread_cond_destroy(&outputQueue.condFull);
    
    MPI_Finalize();

    return 0;
}
