#include <stdio.h>

void print_head(void)
{
  printf("/*\n");
  printf(" * Generated by mk_thread\n");
  printf(" */\n");
  printf("\n");
  printf("#ifndef __UM_THREAD_H\n");
  printf("#define __UM_THREAD_H\n");
  printf("\n");
}

void print_constant_ptr(char *name, int value)
{
  printf("#define %s(task) ((unsigned long *) "
	 "&(((char *) (task))[%d]))\n", name, value);
}

void print_constant(char *name, char *type, int value)
{
  printf("#define %s(task) *((%s *) &(((char *) (task))[%d]))\n", name, type, 
	 value);
}

void print_tail(void)
{
  printf("\n");
  printf("#endif\n");
}
