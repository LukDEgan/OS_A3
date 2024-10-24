

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
void write_book_to_file(int book_index);
int setup_server(int port, int log);
int accept_new_connection(int server_socket);
void *analyze_frequency(void *arg);
void sort_books_by_frequency_and_output();
int count_pattern_occurrences(const char *line);
void trim_newline(char *str);
pthread_mutex_t analysis_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t analysis_cond = PTHREAD_COND_INITIALIZER;
int analysis_ready = 0;
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

typedef struct FrequencyNode {
  struct FrequencyNode *next;
  char *line;
} FrequencyNode;
typedef struct FrequencyList {
  int frequency;
  FrequencyNode *head;
} FrequencyList;

pthread_mutex_t book_lock = PTHREAD_MUTEX_INITIALIZER;
BookList book_list;
char *search_pattern;  // Global variable to store the search pattern

int main(int argc, char *argv[]) {
  int port = 0;  // Initialize port number

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

  // Ensure both port and search pattern are provided
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
void sort_books_by_frequency_and_output() {
  int i, j;
  int frequencies[MAXBOOKS];
  int sorted_books[MAXBOOKS];
  char *book_titles[MAXBOOKS];  // Array to store book titles

  pthread_mutex_lock(&book_lock);  // Lock book list while analyzing

  // Collect frequencies and capture the book titles
  for (i = 0; i < book_list.book_count; i++) {
    Node *current = book_list.book_heads[i];
    int count = 0;

    // The title is the first line (head of the list)
    if (current != NULL) {
      book_titles[i] = current->line;  // Store the book title
      trim_newline(book_titles[i]);    // Trim any newline or extra characters
    }

    // Traverse the book's lines and count occurrences of the search pattern
    while (current != NULL) {
      count += count_pattern_occurrences(current->line);
      current = current->book_next;
    }
    frequencies[i] = count;
    sorted_books[i] = i;  // Track book indices for sorting
  }

  // Sort books based on frequencies using a simple sorting algorithm
  for (i = 0; i < book_list.book_count - 1; i++) {
    for (j = i + 1; j < book_list.book_count; j++) {
      if (frequencies[sorted_books[i]] < frequencies[sorted_books[j]]) {
        // Swap book indices based on frequency
        int temp = sorted_books[i];
        sorted_books[i] = sorted_books[j];
        sorted_books[j] = temp;
      }
    }
  }

  // Output the sorted list of books by frequency
  printf("\nAnalysis Results (Top books by occurrences of '%s'):\n",
         search_pattern);
  for (i = 0; i < book_list.book_count; i++) {
    int book_index = sorted_books[i];
    printf("%d --> Book: %d, Title: '%s', Pattern: '%s', Frequency: %d\n",
           i + 1, book_index + 1, book_titles[book_index], search_pattern,
           frequencies[book_index]);
  }

  pthread_mutex_unlock(&book_lock);  // Unlock book list after analysis
}
void trim_newline(char *str) {
  size_t len = strlen(str);
  if (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
    str[len - 1] = '\0';  // Replace newline character with null terminator
  }
}

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
void *handle_connection(void *p_client_socket) {
  int client_socket = *((int *)p_client_socket);
  free(p_client_socket);
  // frequency for pattern word
  FrequencyList frequency_list;
  frequency_list.frequency = 0;
  frequency_list.head = NULL;
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
void *analyze_frequency(void *arg) {
  while (1) {
    // Sleep for the defined interval
    sleep(ANALYSIS_INTERVAL);

    // Lock the analysis mutex
    pthread_mutex_lock(&analysis_lock);

    // Sort and output the frequency analysis
    sort_books_by_frequency_and_output();

    // Unlock the mutex
    pthread_mutex_unlock(&analysis_lock);
  }
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
  char *pline = new_node->line;
  trim_newline(pline);
  printf("Thread: Added new '%s' to book %d in list\n", pline,
         book_list.book_count);
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
  printf("Thead: Finished writing Book %d to file %s\n", book_index + 1,
         filename);

  pthread_mutex_unlock(&book_lock);
}