#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/stat.h>

static int debug = 0;
static int nlines = 10;
static char *ifile = NULL;
static off_t totalread = 0;

//#define ASSUMELINELEN	(80*10)
#define ASSUMELINELEN	(1024*8)

#if 1
  unsigned char rbuf[ASSUMELINELEN];
#endif

unsigned char *countLines (unsigned char *b, unsigned char *p, int *lines)
{
  do {
    if (*p == '\n')
    {
      *lines-=1;
    }
  } while ( ( --p >= b ) && ( *lines > 0) );
  return (p);
}

static char **lineCbuf = NULL;
static size_t *lsizCbuf = NULL;

int mytp_iterative (int fd, int lines)
{
  int   nlines = lines;
  int   lastlineread;
  size_t lineSize = 0;
  char *line = NULL;
  FILE *fp = fdopen (fd, "r");

  lineCbuf = malloc (sizeof(char *)*nlines);
  lsizCbuf = malloc (sizeof(size_t *)*nlines);

  if ((lineCbuf == NULL) || (lsizCbuf == NULL))
  {
    perror ("malloc");
    return (-1);
  }

  memset (lineCbuf, 0, sizeof(char *)*nlines);
  memset (lsizCbuf, 0, sizeof(size_t *)*nlines);

  nlines = 0;
  lastlineread = 0;
  while (getline (&(lineCbuf[nlines]), &(lsizCbuf[nlines]), fp) != -1)
  {
    totalread += lsizCbuf[nlines];
    lastlineread = nlines++;
    nlines = nlines % lines;
    if (debug) fprintf (stderr, "lastlineread: %d, nlines: %d\n", lastlineread, nlines);
  }
  if (debug) fprintf (stderr, "OUT: lastlineread: %d, nlines: %d, t=%ld\n", lastlineread, nlines, totalread);

  if (totalread > 0)
  {
    while (nlines != lastlineread)
    {
      if (lineCbuf[nlines])
        write (1, lineCbuf[nlines], strlen(lineCbuf[nlines]));
      else
        write (1, "ZZZZ\n", 5);
      nlines++;
      nlines = nlines % lines;
      if (debug) fprintf (stderr, "NOW: lastlineread: %d, nlines: %d, t=%ld\n", lastlineread, nlines, totalread);
    }
    if (lineCbuf[nlines])
    {
      write (1, lineCbuf[nlines], strlen(lineCbuf[nlines]));
      if (debug) fprintf (stderr, "NOW: lastlineread: %d, nlines: %d, t=%ld\n", lastlineread, nlines, totalread);
    }
    else
      write (1, "here\n", 5);
  }
  else
  {
    write (1, "HERE\n", 5);
  }

  free (lineCbuf);
  return (0);
}

int myt_iterative (int fd, int lines, off_t filesize)
{
  unsigned char *p;
  off_t readhead = filesize;
  int   nlines = lines;
  off_t savehead;
  off_t nread = /* lines * */ ASSUMELINELEN;

  readhead = filesize - nread;
  if (readhead < 0)
  {
    readhead = 0;
    nread = filesize;
  }

  /*
   * search loop
   */

  while ((readhead >= 0) && (lines > 0))
  {
    if (debug)
      fprintf (stderr,
        "fd = %d, readhead = %ld, nread = %ld, filesize = %ld, lines = %d, saved = %ld\n",
        fd, readhead, nread, filesize, lines, savehead);

    if ((lseek (fd, readhead, SEEK_SET)) < 0)
    {
      perror ("lseek:SEEK_SET");
      return (-1);
    }

    errno = 0;
    if ((read (fd, &rbuf[0], nread)) < nread)
    {
      fprintf (stderr, "errno = %d\n", errno);
      perror ("read:short");
      return (-1);
    }

    if (nread > 0)
    {
#if 0
      p = countLines (&rbuf[0], &rbuf[nread-1], &lines);
#else
      p = &rbuf[nread-1];
      do {
        if (*p == '\n')
        {
          lines-=1;
        }
      } while ( ( --p >= &rbuf[0] ) && ( lines > 0) );
#endif

      if ( lines == 0 )
      {
        // done, loop will terminate
        /*
         * *p is a char we have not looked at
         * *p+1 is the lineth '\n' observed
         * *p+2 is a char just after the lineth '\n'
         */
        p+=2;
        if (debug)
          fprintf (stderr, "saved: %ld\n", savehead);
      }
      else
      {
        /*
         * *p is a char we have not looked at
         * *p+1 is a char just observed but not a '\n'
         */
        p+=1; /* let p be valid just in case we are @begin of file */

        // save where we are, keep looking
        savehead = readhead;
        if (debug)
          fprintf (stderr, "saved: %ld\n", savehead);

        readhead -= nread;
        if (readhead < 0)
        {
          readhead = 0;
          if (savehead == 0)
          {
            lines = 0; // nothing left to read; not that many lines in file
          }
          else
          {
            nread = savehead; // nread is last readhead saved (savehead).
            if (debug)
              fprintf (stderr,
                "last read will be from %ld to %d\n", readhead, nread);
          }
        }
      }
    }

    totalread += nread;
  }

  // assert nread <= ASSUMELINELEN;
  write (1, p, &rbuf[nread]-p); // assumes the p+=2/p+=1 in search loop above
  if (debug)
    fprintf (stderr, "totalread = %ld len=%ld\n", totalread, &rbuf[nread]-p);

  readhead += nread;
  lseek (fd, readhead, SEEK_SET);
  nread = ASSUMELINELEN;
  while ((readhead < filesize))
  {
    read (fd, &rbuf[0], nread);
    write (1, &rbuf[0], nread);
    readhead += nread;
  }

  return (0);
}

/*
 * on large input files can blow the stack space
 */
int myt_recursive (int fd, int lines, off_t filesize)
{
  off_t readhead = 0;
  off_t nread = /* lines * */ ASSUMELINELEN;
#if 0
  unsigned char rbuf[nread];
#endif
  unsigned char *p;

  readhead = filesize - nread;
  if (readhead < 0)
  {
    readhead = 0;
    nread = filesize;
  }

  if (debug) fprintf (stderr, "fd = %d, readhead = %lld, nread = %lld, filesize = %lld, lines = %d\n", fd, readhead, nread, filesize, lines);

  if ((lseek (fd, readhead, SEEK_SET)) < 0)
  {
    perror ("lseek:SEEK_SET");
    return (-1);
  }

  if ((read (fd, &rbuf[0], nread)) < nread)
  {
    perror ("read:short");
    return (-1);
  }

  if (nread > 0)
  {
#if 0
    p = countLines (&rbuf[0], &rbuf[nread-1], &lines);
#else
    p = &rbuf[nread-1];
    do {
      if (*p == '\n')
      {
        lines-=1;
      }
    } while ( ( --p >= &rbuf[0] ) && ( lines > 0) );
#endif
    if ( lines == 0 )
      p+=2;
    else
      p+=1;
  }

  totalread += nread;
  
  if ((readhead == 0) || (lines == 0))
  {
    write (1, p, &rbuf[nread]-p);
    return (0);
  }
  else
  {
    if ((myt_recursive (fd, lines, readhead)) < 0)
      return (-1);
#if 1
    lseek (fd, readhead, SEEK_SET);
    read (fd, &rbuf[0], nread);
#endif
    write (1, &rbuf[0], nread);
  }

  return (0);
}

int main (int argc, char *argv[])
{
  int op;
  int rc;
  int ifd = -1;
  struct stat statbuf;

  while ((op = getopt (argc, argv, "n:d")) != -1)
  {
    switch (op)
    {
      case 'd':
        debug = 1;
        break;

      case 'n':
        if (!optarg)
        {
          fprintf (stderr, "%s: %c requires an arg\n", argv[0], op);
          exit (-1);
        }
        if (sscanf (optarg, "%d", &nlines) < 1)
        {
          fprintf (stderr, "%s: %c requires a numeric arg\n", argv[0], op);
          exit (-1);
        }
        if (nlines < 0)
        {
          fprintf (stderr, "%s: %c requires a positive numeric arg (%d)\n", argv[0], op, nlines);
          exit (-1);
        }
        if (nlines == 0)
        {
          exit (0);
        }
        break;

      case '?':
      default:
        fprintf (stderr, "%s: unknown arg '%s'\n", argv[0], argv[optind-1]);
        exit (-1);
        /* NOTREACHED */
        break;
    }
  }

  if (debug) fprintf (stderr, "%s: optind = %d\n", argv[0], optind);

  if (argv[optind] == (char *)NULL)
  {
    if (debug)
    {
      fprintf (stderr, "%s: requires a filename\n", argv[0]);
      fprintf (stderr, "assuming stdin\n");
    }
    argv[optind] = "-";
  }

  ifile = argv[optind];
  if (!(strncmp("-",ifile,strlen(ifile))))
  {
    ifd = 0;
  }
  else if ((ifd = open (ifile, O_RDONLY)) < 0)
  {
    perror ("open");
    exit (-1);
  }

  if ((fstat (ifd, &statbuf)) < 0)
  {
    perror ("fstat");
    exit (-1);
  }

  if (debug)
    fprintf (stderr, "file is '%s' (%d) and lines is '%d'\n", ifile, ifd, nlines);

  if (S_ISREG (statbuf.st_mode))
  {
    if (debug)
      fprintf (stderr, "file is '%ld' bytes long\n", statbuf.st_size);
#if 1
    if ((rc = myt_iterative (ifd, nlines+1, statbuf.st_size)) < 0)
#else
    if ((rc = myt_recursive (ifd, nlines+1, statbuf.st_size)) < 0)
#endif
    {
      fprintf (stderr, "%s: mytail failed\n", argv[0]);
    }
  }
  else // regular file
  {
    if (debug)
      fprintf (stderr, "file is a pipe\n" );

    if ((rc = mytp_iterative (ifd, nlines)) < 0)
    {
      fprintf (stderr, "%s: mytail_pipe failed\n", argv[0]);
    }
  }

  close (ifd);
  exit (rc);
}
