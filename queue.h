#pragma once
#define __STDC_ALLOC_LIB__ // see: https://en.cppreference.com/w/c/experimental/dynamic/strdup
#define __STDC_WANT_LIB_EXT2__ 1 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct node_s
{
  char *data;
  struct node_s *next;
} Node;

typedef struct queue_s
{
  Node *front;
  Node *rear;
} Queue;

void queue_init(Queue *q) 
{
  q->front = q->rear = NULL;
}

int queue_is_empty(Queue *q)
{
  return q->front == NULL;
}

// NOTE: This function uses strdup(), so when dequing you have to free the string
void enqueue(Queue *q, const char *data)
{
  Node *new_node = (Node *)malloc(sizeof(Node));
  if (!new_node)
  {
    fprintf(stderr, "ERROR: Memory allocation failed\n");
    return;
  }
  new_node->data = strdup(data);
  if (!new_node->data)
  {
    fprintf(stderr, "ERROR: Memory allocation failed\n");
    free(new_node); 
    return;
  }
  new_node->next = NULL;

  if (queue_is_empty(q))
  {
    q->front = q->rear = new_node;
  }
  else
  {
    q->rear->next = new_node;
    q->rear = new_node;
  }
}

// NOTE see enqueue
char *dequeue(Queue *q)
{
  if (queue_is_empty(q))
  {
    fprintf(stderr, "ERROR: Queue is empty can't dequeue!\n");
    return NULL; // Return NULL to indicate an error
  }
  char *data = q->front->data;

  Node *tmp = q->front;
  q->front = q->front->next;

  if (q->front == NULL)
  {
    q->rear = NULL;
  }

  free(tmp);  // Free the memory of the dequeued node
  return data; // Return the data pointer before freeing the string memory
}

void queue_destroy(Queue *q)
{
  while (!queue_is_empty(q))
  {
    char *data = dequeue(q);
    free(data); // Free the memory of each string
  }
}


void queue_print(Queue *q)
{
  if (queue_is_empty(q))
  {
    printf("The queue is empty!\n");
    return;
  }
  printf("Queue elements:\n");
  Node *current = q->front;
  while (current != NULL)
  {
    printf("%s\n", current->data);
    current = current->next;
  }
}