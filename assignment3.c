#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define SOCKETERROR (-1)
#define ANALYSIS_INTERVAL 5
#define BUFFERSIZE 1024
#define SERVERBACKLOG 10
#define MAXBOOKS 50
void *handle_connection(void *p_client_socket);
int check(int exp, const char *msg);
void add_line(char *line, int book_index);
void write_book_to_file(char *filename, int book_index);
int setup_server(int port, int log);
int accept_new_connection(int server_socket);
void *analyze_frequency();
void *analyse_pattern_for_book(void *book_number);
int count_pattern_occurrences(const char *line);
void trim_newline(char *str);
pthread_mutex_t analysis_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t analysis_cond = PTHREAD_COND_INITIALIZER;
int analysis_ready = 0;
typedef struct Node {
  char *line;
  struct Node *next;
  struct Node *book_next;
  struct Node *next_frequenct_search;
  int pattern_frequency;
} Node;

typedef struct BookList {
  Node *head;
  Node **book_heads;
  int book_count;
} BookList;

pthread_mutex_t book_lock = PTHREAD_MUTEX_INITIALIZER;
BookList book_list;
char *search_pattern;

int main(int argc, char *argv[]) {
  int port = 0;
  // Parse command line arguments using getopt
  int opt;
  while ((opt = getopt(argc, argv, "l:p:")) != -1) {
    switch (opt) {
      case 'l':
        port = atoi(optarg);
        break;
      case 'p':
        search_pattern = strdup(optarg);  // Duplicate the pattern
        break;
      default:
        fprintf(stderr, "Usage: %s -l <port> -p <pattern>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }
  if (port == 0 || search_pattern == NULL) {
    fprintf(stderr, "Error: Port and search pattern must be specified.\n");
    exit(EXIT_FAILURE);
  }
  book_list.book_heads = malloc(MAXBOOKS * sizeof(Node *));
  book_list.book_count = 0;
  int server_socket = setup_server(port, SERVERBACKLOG);
  printf("Server Online\n");
  pthread_t analysis_thread;
  pthread_create(&analysis_thread, NULL, analyze_frequency, NULL);
  while (1) {
    printf("Server: Waiting for Connection...\n");
    int client_socket = accept_new_connection(server_socket);
    pthread_t thread;
    int *pclient = malloc(sizeof(int));
    *pclient = client_socket;
    pthread_create(&thread, NULL, handle_connection, pclient);
    pthread_detach(thread);
  }
  return 0;
}
// SERVER SETUP AND HELPER FUNCTIONS
int accept_new_connection(int server_socket) {
  int addr_size = sizeof(struct sockaddr_in);
  int client_socket;
  struct sockaddr_in client_addr;
  check(client_socket = accept(server_socket, (struct sockaddr *)&client_addr,
                               (socklen_t *)&addr_size),
        "accept failed");
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
  printf("Server: Client Connected from %s:%d\nServer: Generating Thread\n",
         client_ip, ntohs(client_addr.sin_port));
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
  server_addr.sin_port = htons(port);

  check(
      bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)),
      "ERROR on binding");
  check(listen(server_socket, log), "Bind failed");

  return server_socket;
};
int check(int exp, const char *msg) {
  if (exp == SOCKETERROR) {
    perror(msg);
    exit(1);
  }
  return exp;
}
//* ---------------------------------------------------------*//
// ANALYSIS THREAD FUNCTIONS
void *analyze_frequency() {
  while (1) {
    // Sleep for the defined interval
    sleep(ANALYSIS_INTERVAL);

    // Lock the analysis mutex
    pthread_mutex_lock(&analysis_lock);
    int book_count = book_list.book_count;
    pthread_t analysis_threads[book_count];
    int *results[book_count];
    // create thread for each book. Each thread will then traverse
    // next_frequent_search and count the occurances. This thread will wait
    // until the spawned threads are done
    for (int i = 0; i < book_list.book_count; i++) {
      int *thread_book_count = malloc(sizeof(int));
      *thread_book_count = i;
      if (pthread_create(&analysis_threads[i], NULL, analyse_pattern_for_book,
                         thread_book_count) != 0) {
        perror("Failed to create thread");
      }
    }
    // wait for each thread and collect return values, sort these return values;
    for (int i = 0; i < book_list.book_count; i++) {
      if (pthread_join(analysis_threads[i], (void **)&results[i]) != 0) {
        perror("Failed to join thread");
      }
    }

    // i need build a book title array from the results array, then when i sort
    // that results array, i need to rearrange the titel array in the same way

    int sorted_array[book_count];
    for (int i = 0; i < book_count; i++) {
      sorted_array[i] = i + 1;
    }
    for (int i = 0; i < book_count - 1; i++) {
      for (int j = i + 1; j < book_count; j++) {
        if (*results[i] < *results[j]) {
          // Swap the frequencies in the results array
          int temp_result = *results[i];
          *results[i] = *results[j];
          *results[j] = temp_result;

          // Swap the corresponding book titles/indices in the sorted_books
          // array
          int temp_book = sorted_array[i];  // Assuming sorted_books holds
                                            // book indices or titles
          sorted_array[i] = sorted_array[j];
          sorted_array[j] = temp_book;
        }
      }
    }
    if (book_count != 0) {
      printf("\nAnalysis Results (Top books by occurrences of '%s'):\n",

             search_pattern);
    }
    for (int i = 0; i < book_list.book_count; i++) {
      int book_index = sorted_array[i];
      char *book_title = book_list.book_heads[sorted_array[i] - 1]->line;
      printf("%d --> Book: %d, Title: '%s', Pattern: '%s', Frequency: %d\n",
             i + 1, book_index, book_title, search_pattern, *results[i]);
    }

    pthread_mutex_unlock(&analysis_lock);
  }
  //  Unlock the mutex

  return NULL;
}
void *analyse_pattern_for_book(void *book_number) {
  int book_index = *(int *)book_number;
  free(book_number);
  int frequency = 0;
  pthread_mutex_lock(&book_lock);  // Lock book list while analyzing

  // traverse via next_frequency_pattern and keep count of the occurences
  if (book_list.book_heads[book_index] != NULL) {
    Node *current = book_list.book_heads[book_index];
    frequency += current->pattern_frequency;
    while (current->next_frequenct_search != NULL) {
      current = current->next_frequenct_search;
      frequency += current->pattern_frequency;
    }
  }
  pthread_mutex_unlock(&book_lock);  // Unlock book list after analysi
  int *result = malloc(sizeof(int));
  *result = frequency;
  return (void *)result;
}
//* ---------------------------------------------------------*//

void trim_newline(char *str) {
  size_t len = strlen(str);
  if (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
    str[len - 1] = '\0';  // Replace newline character with null terminator
  }
}
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
  char filename[50];
  if (current_book_index + 1 < 10) {
    sprintf(filename, "book_0%d.txt", current_book_index + 1);
  } else {
    sprintf(filename, "book_%d.txt", current_book_index + 1);
  }
  FILE *file = fopen(filename, "w");
  fclose(file);
  pthread_mutex_unlock(&book_lock);

  // Read from client socket
  // This is a blocking read, as each connection has its own thread
  // it is desired to block on reads on each thread. This doesn't
  // block the main thread from accepting new connections or other
  // threads from reading. If this was made non-blocking then you
  // would need a select or other blocking call which waits only
  // for reads on this single file descriptor, or a busy loop which
  // keeps trying non-blocking reads untill one works. These are both
  // work arounds for a blocking reads as that is the desried functionality.
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
  // write_book_to_file(filename, current_book_index);

  return NULL;
}
int count_pattern_occurrences(const char *line) {
  int count = 0;
  const char *tmp = line;

  // Get the length of the pattern
  size_t pattern_len = strlen(search_pattern);

  // Loop to find each occurrence of the pattern in the line
  while ((tmp = strstr(tmp, search_pattern)) != NULL) {
    count++;             // Increment the count when pattern is found
    tmp += pattern_len;  // Move the pointer ahead to avoid overlapping matches
  }

  return count;
}

void add_line(char *line, int book_index) {
  Node *new_node = malloc(sizeof(Node));
  new_node->next = NULL;
  new_node->book_next = NULL;
  new_node->next_frequenct_search = NULL;
  new_node->pattern_frequency = 0;

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
  pthread_mutex_lock(&book_lock);

  // add new node to end of list first
  if (book_list.head != NULL) {
    Node *current = book_list.head;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = new_node;
  }
  int pattern_occurances = count_pattern_occurrences(line);
  new_node->pattern_frequency = pattern_occurances;
  // then connect new node to books most recent node
  if (book_list.book_heads[book_index] == NULL) {
    book_list.book_heads[book_index] = new_node;
  } else {
    Node *current = book_list.book_heads[book_index];
    while (current->book_next != NULL) {
      if (pattern_occurances > 0 && current->next_frequenct_search == NULL) {
        current->next_frequenct_search = new_node;
      }
      current = current->book_next;
    }
    // if we at the end of the list, but the last node has pattern then must
    // also update second to last
    if (pattern_occurances > 0) {
      current->next_frequenct_search = new_node;
    }
    current->book_next = new_node;  // Append the new node
  }

  // print log statement for added node
  char *pline = new_node->line;
  trim_newline(pline);
  printf(
      "--------------------------\nThread: Added new line :\n'%s'\nto book %d "
      "in list\n",
      pline, book_list.book_count);
  char filename[50];
  if (book_list.book_count + 2 < 10) {
    sprintf(filename, "book_0%d.txt", book_list.book_count);
  } else {
    sprintf(filename, "book_%d.txt", book_list.book_count);
  }
  FILE *file = fopen(filename, "a");
  fprintf(file, "%s\n", line);
  fclose(file);
  pthread_mutex_unlock(&book_lock);
}
