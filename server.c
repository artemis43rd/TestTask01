#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_CLIENTS 5

pthread_mutex_t mutex;
int active_clients = 0;
int max_clients_reached = 0;

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    char buffer[1024] = {0};
    int valread;

    // Увеличение счетчика активных клиентов
    pthread_mutex_lock(&mutex);
    active_clients++;
    pthread_mutex_unlock(&mutex);

    // Чтение команды от клиента
    valread = read(client_socket, buffer, sizeof(buffer));
    printf("Received command: %s\n", buffer);

    // Выполнение команды и отправка результата
    FILE* fp = popen(buffer, "r");
    if (fp == NULL) {
        perror("popen");
        close(client_socket);
        pthread_exit(NULL);
    }

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        send(client_socket, buffer, strlen(buffer), 0);
    }
    pclose(fp);

    close(client_socket);

    // Уменьшение счетчика активных клиентов
    pthread_mutex_lock(&mutex);
    active_clients--;
    pthread_mutex_unlock(&mutex);

    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Создание сокета
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Настройка опций сокета для повторного использования адреса
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Привязка сокета к порту
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Ожидание подключений
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is running on port %d\n", PORT);

    // Инициализация мьютекса
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    // Принятие и обработка соединений
    while (1) {
        // Сброс флага, показывающего достижение максимальной емкости
        pthread_mutex_lock(&mutex);
        max_clients_reached = 0;
        pthread_mutex_unlock(&mutex);

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                 (socklen_t *)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        // Проверка количества активных клиентов
        pthread_mutex_lock(&mutex);
        if (active_clients >= MAX_CLIENTS) {
            printf("Maximum clients reached. Connection rejected.\n");
            max_clients_reached = 1;
            char msg[] = "Server reached maximum capacity. Connection rejected.\n";
            send(new_socket, msg, strlen(msg), 0);
            close(new_socket);
        } else {
            // Создание нового потока для обработки клиента
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_client, (void *)&new_socket) != 0) {
                perror("pthread_create");
                close(new_socket);
            }
            pthread_detach(thread);
        }
        pthread_mutex_unlock(&mutex);
    }

    // Уничтожение мьютекса
    pthread_mutex_destroy(&mutex);

    return 0;
}