/* input_file.c - Deal with Input Files -
   Copyright (C) 1987, 1990, 1991, 1992 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*
 * Confines all details of reading source bytes to this module.
 * All O/S specific crocks should live here.
 * What we lose in "efficiency" we gain in modularity.
 * Note we don't need to #include the "as.h" file. No common coupling!
 */

#include <stdio.h>
#include <string.h>

#include "as.h"
#include "input-file.h"

static int input_file_get PARAMS ((char **));

/* This variable is non-zero if the file currently being read should be
   preprocessed by app.  It is zero if the file can be read straight in.
   */
int preprocess = 0;

/*
 * This code opens a file, then delivers BUFFER_SIZE character
 * chunks of the file on demand.
 * BUFFER_SIZE is supposed to be a number chosen for speed.
 * The caller only asks once what BUFFER_SIZE is, and asks before
 * the nature of the input files (if any) is known.
 */

#define BUFFER_SIZE (32 * 1024)

/*
 * We use static data: the data area is not sharable.
 */

static FILE *f_in;
static char *file_name;

/* Struct for saving the state of this module for file includes.  */
struct saved_file
  {
    FILE *f_in;
    char *file_name;
    int preprocess;
    char *app_save;
  };

/* These hooks accomodate most operating systems. */

void 
input_file_begin ()
{
  f_in = (FILE *) 0;
}

void 
input_file_end ()
{
}

/* Return BUFFER_SIZE. */
unsigned int 
input_file_buffer_size ()
{
  return (BUFFER_SIZE);
}

int 
input_file_is_open ()
{
  return f_in != (FILE *) 0;
}

/* Push the state of our input, returning a pointer to saved info that
   can be restored with input_file_pop ().  */
char *
input_file_push ()
{
  register struct saved_file *saved;

  saved = (struct saved_file *) xmalloc (sizeof *saved);

  saved->f_in = f_in;
  saved->file_name = file_name;
  saved->preprocess = preprocess;
  if (preprocess)
    saved->app_save = app_push ();

  input_file_begin ();		/* Initialize for new file */

  return (char *) saved;
}

void
input_file_pop (arg)
     char *arg;
{
  register struct saved_file *saved = (struct saved_file *) arg;

  input_file_end ();		/* Close out old file */

  f_in = saved->f_in;
  file_name = saved->file_name;
  preprocess = saved->preprocess;
  if (preprocess)
    app_pop (saved->app_save);

  free (arg);
}

int vr4300mul_enabled = -1;

// Mimic KMC as mulmul fix functionality
char* read_asm(const char *pathname, size_t* output_size) {
    int bVar2;
    int bVar3;
    FILE* fd;
    int iVar7;
    size_t input_size;
    char *buffer;
    size_t num_bytes_read;
    void *output;
    char *next_input_pos;
    char *vr4300mul_env;
    char *cur_insn_mul;
    size_t initial_output_size;
    char *cur_output_pos;
    char cur_output_char;
    int cur_char;
    uint prev_insn_was_float_mul;
    char *local_28;
    char *cur_insn;
    char *cur_input_pos;
    char cur_insn_first_char;

    fd = fopen(pathname, "r");
    if (fd == NULL)
    {
        as_fatal ("Can't open %s for reading.", pathname);
    }

    fseek(fd, 0, SEEK_END);
    input_size = ftell(fd);
    initial_output_size = input_size;
    if (input_size < 0x10000)
    {
        initial_output_size = 0x10000;
    }
    fseek(fd, 0, SEEK_SET);
    buffer = (char *)malloc(initial_output_size + 100 + input_size);
    num_bytes_read = fread(buffer + initial_output_size, 1, input_size, fd);
    if (input_size == num_bytes_read)
    {
        buffer[initial_output_size + input_size] = '\x1a';
        cur_input_pos = buffer + initial_output_size;
        bVar3 = false;
        bVar2 = false;
        iVar7 = 0;
        local_28 = (char *)0x0;
        cur_insn = (char *)0x0;
        prev_insn_was_float_mul = 0;
        cur_output_pos = buffer;
        if (vr4300mul_enabled == -1)
        {
            vr4300mul_enabled = 1;
            vr4300mul_env = getenv("VR4300MUL");
            if (vr4300mul_env != NULL)
            {
                while (isspace(*vr4300mul_env))
                {
                    vr4300mul_env++;
                }
                if (strncasecmp(vr4300mul_env, "OFF", 3) == 0)
                {
                    vr4300mul_enabled = 0;
                }
            }
        }
    loop_start:
        cur_char = (int)*cur_input_pos;
        next_input_pos = cur_input_pos + 1;
        /* If the current character is a carriage return and the next character is a
            line feed, skip the carriage return */
        if (cur_char == '\r')
        {
            if (*next_input_pos == '\n')
            {
                cur_char = '\n';
                next_input_pos = cur_input_pos + 2;
            }
        }
        else
        {
            /* End of file */
            if (cur_char == 0x1a)
            {
                *output_size = cur_output_pos - buffer;
                output = realloc(buffer, cur_output_pos - buffer);
                fclose(fd);
                return output;
            }
        }
        cur_input_pos = next_input_pos;
        cur_output_char = (char)cur_char;
        if (iVar7 != 0)
        {
            if (cur_char == '\n')
            {
            check_mulmul:
                if (vr4300mul_enabled != 0)
                {
                    if (((cur_insn == NULL) && (cur_char != ' ')) && (cur_char != '\t'))
                    {
                        cur_insn = cur_output_pos;
                    }
                    if ((cur_char == '\n') && (cur_insn != NULL))
                    {
                        cur_insn_first_char = *cur_insn;
                        if (prev_insn_was_float_mul == 0)
                        {
                            if ((local_28 == cur_insn) ||
                                (((cur_insn_first_char == 'm' && (cur_insn[1] == 'u')) &&
                                    (((cur_insn[2] == 'l' && (cur_insn[3] == '.')) &&
                                    ((cur_insn[4] == 's' || (cur_insn[4] == 'd'))))))))
                            {
                                prev_insn_was_float_mul = 1;
                            }
                        }
                        else
                        {
                            cur_insn_mul = cur_insn;
                            if (cur_insn_first_char == 'd')
                            {
                                cur_insn_mul = cur_insn + 1;
                            }
                            if (((*cur_insn_mul == 'm') && (cur_insn_mul[1] == 'u')) &&
                                (cur_insn_mul[2] == 'l'))
                            {
                                prev_insn_was_float_mul = (cur_insn_mul[3] == '.') ? 1 : 0;
                                /* Shift the file forward by 5 characters */
                                bcopy(cur_insn, cur_insn + 5, cur_output_pos - cur_insn);
                                /* Insert a nop, newline and tab into the gap created by the previous bcopy */
                                bcopy("nop\n\t", cur_insn, 5);
                                cur_output_pos = cur_output_pos + 5;
                            }
                            else
                            {
                                if (isalpha(cur_insn_first_char))
                                {
                                    prev_insn_was_float_mul = 0;
                                }
                            }
                        }
                        cur_insn = NULL;
                        local_28 = cur_output_pos + 1;
                    }
                }
                goto write_output;
            }
            if ((cur_char == '*') && (*cur_input_pos == '/'))
            {
                cur_input_pos = cur_input_pos + 1;
                iVar7 = 0;
            }
            goto loop_start;
        }
        if (!bVar2)
        {
            if (((cur_char != '/') || (*cur_input_pos != '*')) || (bVar3))
            {
                if (cur_char == '\\')
                {
                    bVar2 = true;
                }
                else
                {
                    if (cur_char != '"')
                        goto check_mulmul;
                    bVar3 = bVar3 ^ 1;
                }
                goto write_output;
            }
            iVar7 = 1;
            cur_input_pos = cur_input_pos + 1;
            goto loop_start;
        }
        bVar2 = false;
    write_output:
        *cur_output_pos = cur_output_char;
        cur_output_pos = cur_output_pos + 1;
        goto loop_start;
    }
    fclose(fd);
    return NULL;
}


void
input_file_open (filename, pre)
     char *filename;		/* "" means use stdin. Must not be 0. */
     int pre;
{
  int c;
  char buf[80];

  preprocess = pre;

  assert (filename != 0);	/* Filename may not be NULL. */
  if (filename[0])
    {				/* We have a file name. Suck it and see. */
      // Create a temp file to hold the processed output
      char temp_filename[] = "astemp_XXXXXX.s";
      int fd = mkstemps(temp_filename, 2);
      unlink(temp_filename);
      // Process the input file
      size_t processed_size;
      char* processed = read_asm(filename, &processed_size);
      // Write the processed output to the "input" file
      ssize_t written = write(fd, processed, processed_size);
      fsync(fd);
      lseek(fd, 0, SEEK_SET);
      // Convert the temp file descriptor to a FILE* pointer
      f_in = fdopen(fd, "r");
      // Free the processed output
      free(processed);
      file_name = filename;
    }
  else
    {				/* use stdin for the input file. */
      f_in = stdin;
      file_name = "{standard input}";	/* For error messages. */
    }
  if (f_in == (FILE *) 0)
    {
      as_bad ("Can't open %s for reading.", file_name);
      as_perror ("%s", file_name);
      return;
    }

  c = getc (f_in);
  if (c == '#')
    {				/* Begins with comment, may not want to preprocess */
      c = getc (f_in);
      if (c == 'N')
	{
	  fgets (buf, 80, f_in);
	  if (!strcmp (buf, "O_APP\n"))
	    preprocess = 0;
	  if (!strchr (buf, '\n'))
	    ungetc ('#', f_in);	/* It was longer */
	  else
	    ungetc ('\n', f_in);
	}
      else if (c == '\n')
	ungetc ('\n', f_in);
      else
	ungetc ('#', f_in);
    }
  else
    ungetc (c, f_in);
}

/* Close input file.  */
void 
input_file_close ()
{
  if (f_in != NULL)
    {
      fclose (f_in);
    }				/* don't close a null file pointer */
  f_in = 0;
}				/* input_file_close() */

/* This function is passed to do_scrub_chars.  */

static int
input_file_get (from)
     char **from;
{
  static char buf[BUFFER_SIZE];
  int size;

  size = fread (buf, sizeof (char), sizeof buf, f_in);
  if (size < 0)
    {
      as_perror ("Can't read from %s", file_name);
      size = 0;
    }
  *from = buf;
  return size;
}

/* Read a buffer from the input file.  */

char *
input_file_give_next_buffer (where)
     char *where;		/* Where to place 1st character of new buffer. */
{
  char *return_value;		/* -> Last char of what we read, + 1. */
  register int size;

  if (f_in == (FILE *) 0)
    return 0;
  /*
   * fflush (stdin); could be done here if you want to synchronise
   * stdin and stdout, for the case where our input file is stdin.
   * Since the assembler shouldn't do any output to stdout, we
   * don't bother to synch output and input.
   */
  if (preprocess)
    size = do_scrub_chars (input_file_get, where, BUFFER_SIZE);
  else
    size = fread (where, sizeof (char), BUFFER_SIZE, f_in);
  if (size < 0)
    {
      as_perror ("Can't read from %s", file_name);
      size = 0;
    }
  if (size)
    return_value = where + size;
  else
    {
      if (fclose (f_in))
	as_perror ("Can't close %s", file_name);
      f_in = (FILE *) 0;
      return_value = 0;
    }
  return (return_value);
}

/* end of input-file.c */
