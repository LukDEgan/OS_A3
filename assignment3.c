
#include <libc.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define SOCKETERROR (-1)
#define SERVERPORT 1234
#define BUFFERSIZE 1024
#define SERVERBACKLOG 10
#define MAXBOOKS 50
void *handle_connection(void *p_client_socket);
int check(int exp, const char *msg);
void add_line(char *line, int book_index);
void write_book_to_file(int book_index);
int setup_server(int port, int log);
int accept_new_connection(int server_socket);

typedef struct Node {
  char *line;
  struct Node *next;
  struct Node *book_next;

} Node;

typedef struct BookList {
  Node *head;
  Node **book_heads;
  int book_count;
} BookList;

pthread_mutex_t book_lock = PTHREAD_MUTEX_INITIALIZER;
BookList book_list;

int main(int argc, char *argv[]) {
  book_list.book_heads = malloc(MAXBOOKS * sizeof(Node *));
  book_list.book_count = 0;
  int server_socket = setup_server(SERVERPORT, SERVERBACKLOG);

  while (1) {
    printf("waiting for connection...\n");
    int client_socket = accept_new_connection(server_socket);
    printf("Connected!\n");
    pthread_t thread;
    int *pclient = malloc(sizeof(int));
    *pclient = client_socket;
    pthread_create(&thread, NULL, handle_connection, pclient);
    pthread_detach(thread);
  }
  return 0;
}
int accept_new_connection(int server_socket) {
  int addr_size = sizeof(struct sockaddr_in);
  int client_socket;
  struct sockaddr_in client_addr;
  check(client_socket = accept(server_socket, (struct sockaddr *)&client_addr,
                               (socklen_t *)&addr_size),
        "accept failed");
  return client_socket;
}
int setup_server(int port, int log) {
  int server_socket;
  struct sockaddr_in server_addr;

  check((server_socket = socket(AF_INET, SOCK_STREAM, 0)),
        "Failed to create socket");

  // Initialize address struct
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(SERVERPORT);

  check(
      bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)),
      "ERROR on binding");
  check(listen(server_socket, log), "Bind failed");

  return server_socket;
};
void *handle_connection(void *p_client_socket) {
  int client_socket = *((int *)p_client_socket);
  free(p_client_socket);

  char buffer[BUFFERSIZE];
  char *incomplete_line_buffer = NULL;  // Buffer to store incomplete lines
  int bytes_read;
  size_t incomplete_line_buffer_size = 0;  // Size of the line buffer

  // Lock to update book list safely
  pthread_mutex_lock(&book_lock);
  int current_book_index =
      book_list.book_count++;  // Get the current book index and increment
  pthread_mutex_unlock(&book_lock);

  // Read from client socket
  while ((bytes_read = read(client_socket, buffer, BUFFERSIZE - 1)) > 0) {
    buffer[bytes_read] = '\0';  // Null-terminate the string

    // Process the buffer to extract complete lines
    char *start = buffer;
    char *newline;
    while ((newline = strchr(start, '\n')) != NULL) {
      // Allocate memory for the complete line
      size_t line_length = newline - start;  // Exclude the newline character

      // Check if there's any data in incomplete_line_buffer
      if (incomplete_line_buffer_size > 0) {
        // Reallocate incomplete_line_buffer to append the new line
        incomplete_line_buffer =
            realloc(incomplete_line_buffer,
                    incomplete_line_buffer_size + line_length + 1);
        memcpy(incomplete_line_buffer + incomplete_line_buffer_size, start,
               line_length);
        incomplete_line_buffer[incomplete_line_buffer_size + line_length] =
            '\0';  // Null-terminate

        // Add the complete line
        add_line(incomplete_line_buffer, current_book_index);

        // Clear the buffer
        free(incomplete_line_buffer);
        incomplete_line_buffer = NULL;
        incomplete_line_buffer_size = 0;
      } else {
        // No previous data, add directly
        char *complete_line = strndup(start, line_length);  // Exclude newline
        add_line(complete_line, current_book_index);
        free(complete_line);
      }

      start = newline + 1;  // Move past the newline character
    }

    // If there's remaining data in the buffer (a partial line), save it
    if (*start != '\0') {
      incomplete_line_buffer_size = strlen(start);
      incomplete_line_buffer =
          realloc(incomplete_line_buffer, incomplete_line_buffer_size + 1);
      strcpy(incomplete_line_buffer, start);  // Copy incomplete line to buffer
    }
  }

  // If there's leftover data in the incomplete_line_buffer after the loop, add
  // it
  if (incomplete_line_buffer_size > 0) {
    add_line(incomplete_line_buffer, current_book_index);
    free(incomplete_line_buffer);
  }

  close(client_socket);

  // Write the entire book to a file once all data is received
  write_book_to_file(current_book_index);

  return NULL;
}

int check(int exp, const char *msg) {
  if (exp == SOCKETERROR) {
    perror(msg);
    exit(1);
  }
  return exp;
}
void add_line(char *line, int book_index) {
  Node *new_node = malloc(sizeof(Node));
  if (!new_node) {
    perror("Failed to allocate memory for new node");
    exit(EXIT_FAILURE);
  }

  // Create a copy of the line without the newline character at the end
  size_t line_length = strlen(line);
  if (line_length > 0 && line[line_length - 1] == '\n') {
    line_length--;  // Exclude the newline character
  }

  new_node->line =
      strndup(line, line_length);  // Duplicate the line without newline
  if (!new_node->line) {
    perror("Failed to allocate memory for line");
    free(new_node);
    exit(EXIT_FAILURE);
  }

  new_node->next = NULL;
  new_node->book_next = NULL;

  pthread_mutex_lock(&book_lock);

  // Initialize book_heads if necessary
  if (book_list.book_heads[book_index] == NULL) {
    book_list.book_heads[book_index] = new_node;  // Set the head of the list
  } else {
    // Add the new node to the end of the book's list (linked via book_next)
    Node *current = book_list.book_heads[book_index];
    while (current->book_next != NULL) {
      current = current->book_next;
    }
    current->book_next = new_node;  // Append the new node
  }

  pthread_mutex_unlock(&book_lock);
}

void write_book_to_file(int book_index) {
  pthread_mutex_lock(&book_lock);

  // Create the filename using book_index
  char filename[50];
  sprintf(filename, "book_%d.txt", book_index + 1);  // book_index is zero-based

  // Open the file
  FILE *file = fopen(filename, "w");
  if (!file) {
    perror("Error opening file");
    pthread_mutex_unlock(&book_lock);
    return;
  }

  // Write the content of the book to the file (using book_next)
  Node *current = book_list.book_heads[book_index];
  while (current != NULL) {
    fprintf(file, "%s\n", current->line);
    current = current->book_next;
  }

  fclose(file);
  printf("Finished writing Book %d to file %s\n", book_index + 1, filename);

  pthread_mutex_unlock(&book_lock);
}