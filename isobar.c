/*
 * isobar.c: ISO Boot Archive Remover.
 *
 * Jason Hood, 6, 8 & 9 March, 2005.
 *
 * Extract the boot image (or code if no emulation) from a bootable CD-ROM
 * (or an image of one).
 *
 * Adapted from the program by David Brinkman.
 *
 * v1.01, 30 May, 2005:
 *   always include Image Size in display.
 *
 * v1.02, 5 & 6 June, 2005:
 *   Win32 port.
 *
 * Todo: possibly replace the boot image in a CD image;
 *	 recognise boot sections;
 *	 ignore non-bootable CDs (are there any?).
 */

#define PVERS "1.02"
#define PDATE "6 June, 2005"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>

#ifdef _WIN32
#include <windows.h>
#define far
#define farmalloc malloc
#define _fstrcmp strcmp
#define SFMT "s"
#define MAX 32
#else
#include <dos.h>
#include <alloc.h>
#include <string.h>
typedef unsigned char BYTE;
typedef unsigned int  WORD;
typedef unsigned long DWORD;
typedef unsigned int  UINT;
#define SFMT "Fs"
#define MAX 30u
#endif

int  CDReadLong( UINT SectorCount, DWORD StartSector );

char far* buf;
int  CD = -1;
long offset = 0, imgsize, blksize;
#ifdef _WIN32
HANDLE fdin;
#define OFLAG _O_BINARY | _O_CREAT | _O_WRONLY | _O_TRUNC
#else
int  fdin = 0;
extern int _fmode;
#define OFLAG O_CREAT | O_WRONLY | O_TRUNC
#endif

const char* platform[] = { "80x86",
			   "Power PC",
			   "Mac" };

const char* boot_type[] = { "no emulation",
			    "1.2 meg floppy",
			    "1.44 meg floppy",
			    "2.88 meg floppy",
			    "hard disk" };

enum
{
  E_OK, 		// No problems
  E_OPT,		// Unknown/invalid option
  E_MEM,		// Not enough memory
  E_NOCD,		// Not a CD drive, MSCDEX/SHSUCDX not installed,
			//  unknown CD format or no CD present
  E_CREATE,		// Unable to create image file
  E_ABORTED		// Read/write error
};


void usage( void )
{
  puts(

"ISOBAR by Jason Hood <jadoxa@yahoo.com.au>.\n"
"Version "PVERS" ("PDATE"). Freeware.\n"
"http://shsucdx.adoxa.cjb.net/\n"
"\n"
"Extract the boot image (or code) from a bootable CD-ROM or .ISO image.\n"
"\n"
"isobar [-o file [-d]] [iso-file|CD-ROM-drive]\n"
"\n"
"-o file       Write the boot image (or code) to the specified filename\n"
"                (without this boot information is displayed).\n"
"-d            For a hard disk image just write the drive (strip MBR).\n"
"iso-file      An image of a bootable CD-ROM.\n"
"CD-ROM-drive  The drive letter of a CD-ROM containing a bootable disc\n"
"                (default is first CD).\n"
"\n"
"ISOBAR was derived from the program by David Brinkman."

  );

  exit( E_OK );
}


int main( int argc, char* argv[] )
{
  int	i;
  int	fdout = 0;
  char *outfile = NULL, *isofile = NULL, cdbuf[8];
  DWORD bootfound;
  int	drive = 0;
  int	type;
  DWORD n;
  UINT	len, w;

#ifndef _WIN32
  union REGS regs;

  _fmode = O_BINARY;
#endif

  if (argc > 1)
  {
    if (argv[1][0] == '?' || argv[1][1] == '?' || !strcmp( argv[1], "--help" ))
      usage();

    for (i = 1; i < argc; ++i)
    {
      if (*argv[i] == '-' || *argv[i] == '/')
      {
	switch (argv[i][1] | 0x20)
	{
	  case 'o':
	    if (argv[i][2] == '\0' && argv[i+1] == NULL)
	    {
	      fputs( "ERROR: -o requires filename.\n", stderr );
	      return E_OPT;
	    }
	    outfile = (argv[i][2] == '\0') ? argv[++i] : argv[i] + 2;
	  break;

	  case 'd':
	    drive = 1;
	  break;

	  default:
	    fprintf( stderr, "ERROR: unknown option: %s.\n", argv[i] );
	    return E_OPT;
	}
      }
      else
      {
	isofile = argv[i];
      }
    }
  }

#ifdef _WIN32
  if (!isofile)
  {
    char buf[32*4+1];
    len = GetLogicalDriveStrings( sizeof(buf)-1, buf );
    for (isofile = buf; len; isofile += 4, len -= 4)
    {
      if (GetDriveType( isofile ) == DRIVE_CDROM)
      {
	CD = *isofile - 'A';
	break;
      }
    }
    if (CD == -1)
    {
      fputs( "ERROR: No CD-ROM drives assigned.\n", stderr );
      return E_NOCD;
    }
  }
  else if (isofile[1] == '\0' || (isofile[1] == ':' && isofile[2] == '\0'))
  {
    CD = (*isofile | 0x20) - 'a';
    cdbuf[0] = CD + 'A';
    cdbuf[1] = ':';
    cdbuf[2] = '/';
    cdbuf[3] = '\0';
    if (GetDriveType( cdbuf ) != DRIVE_CDROM)
    {
      fprintf( stderr, "ERROR: %c: is not a CD-ROM drive.\n", cdbuf[0] );
      return E_NOCD;
    }
  }
  if (CD != -1)
  {
    sprintf( cdbuf, "//./%c:", CD + 'A' );
    isofile = cdbuf;
  }
  fdin = CreateFile( isofile, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
		     NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL );
  if (fdin == INVALID_HANDLE_VALUE)
  {
    fprintf( stderr, "ERROR: Cannot open %s.\n", isofile );
    return E_NOCD;
  }
  if (CD != -1)
    isofile = cdbuf + 4;

#else
  if (!isofile)
  {
    regs.x.ax = 0x1500;
    regs.x.bx = 0;
    int86( 0x2f, &regs, &regs );
    CD = regs.x.cx;
  }
  else if (isofile[1] == '\0' || (isofile[1] == ':' && isofile[2] == '\0'))
  {
    CD = (*isofile | 0x20) - 'a';
    regs.x.ax = 0x150B;
    regs.x.cx = CD;
    int86( 0x2f, &regs, &regs );
    if (regs.x.bx != 0xADAD)
      regs.x.bx = 0;
    else if (regs.x.ax == 0)
    {
      fprintf( stderr, "ERROR: %c: is not a CD-ROM drive.\n", CD + 'A' );
      return E_NOCD;
    }
  }
  else
  {
    fdin = open( isofile, O_RDONLY );
    if (fdin < 0)
    {
      fprintf( stderr, "ERROR: Cannot open %s.\n", isofile );
      return E_NOCD;
    }
    regs.x.bx = 1;
  }
  if (regs.x.bx == 0)
  {
    fputs( "ERROR: No CD-ROM drives assigned.\n", stderr );
    return E_NOCD;
  }
  else if (CD != -1)
  {
    cdbuf[0] = CD + 'A';
    cdbuf[1] = ':';
    cdbuf[2] = '\0';
    isofile = cdbuf;
  }
#endif

  buf = farmalloc( MAX << 11 ); // transfer up to MAX blocks at a time
  if (buf == NULL)
  {
    fputs( "ERROR: Not enough memory.\n", stderr );
    return E_MEM;
  }

  if (!CDReadLong( 1, 0x11 ))
  {
    fputs( "Read error!\n", stderr );
    return E_ABORTED;
  }

  if (_fstrcmp( buf+1, "CD001\01EL TORITO SPECIFICATION" ))
  {
    fprintf( stderr, "ERROR: %s is not EL TORITO.\n", isofile );
    return E_NOCD;
  }
  bootfound = *(DWORD far*)(buf+0x47);
  printf( "Catalog Sector:\t%lx\n", bootfound );

  if (!CDReadLong( 1, bootfound ))
  {
    fputs( "Read error!\n", stderr );
    return E_ABORTED;
  }

  // Just check the key bytes, don't worry about the checksum.
  if (buf[0x1e] != (char)0x55 || buf[0x1f] != (char)0xAA)
  {
    fprintf( stderr, "ERROR: %s has an invalid boot catalog.\n", isofile );
    return E_NOCD;
  }
  type = (BYTE)buf[1];
  printf( "Platform:\t%s (%02x)\n", (type < 3) ? platform[type] : "unknown",
				    type );
  printf( "ID String:\t%.24"SFMT"\n", (buf[4]) ? buf+4 : "not recorded" );
  printf( "Bootable:\t%s (%02x)\n", (buf[0x20] == (char)0x88) ? "yes" : "no",
				    (BYTE)buf[0x20] );
  type = buf[0x21] & 15;
  printf( "Boot Type:\t%s (%02x)\n", (type < 5) ? boot_type[type] : "unknown",
				     (BYTE)buf[0x21] );
  i = *(WORD far*)(buf+0x22);
  printf( "Load Segment:\t%04x\n", (i == 0) ? 0x7c0 : i );
  printf( "System Type:\t%02x\n", (BYTE)buf[0x24] );
  imgsize = *(WORD far*)(buf+0x26);
  printf( "Sector Count:\t%02x (%d)\n", (UINT)imgsize, (UINT)imgsize );
  offset = *(DWORD far*)(buf+0x28);
  printf( "Image Sector:\t%lx\n", offset );

  if (type == 0)		// no emulation
  {
    blksize = 0x200;
  }
  else
  {
    CDReadLong( 1, offset );
    if (type == 4 && !drive)	// hard disk, keep MBR
    {
      blksize = 0x200;
      imgsize = *(DWORD far*)(buf+0x1ca);
    }
    else
    {
      if (type == 4)		// hard disk, skip MBR
      {
	offset += *(DWORD far*)(buf+0x1c6) >> 2;	// diff between HD & CD
	CDReadLong( 1, offset );
      }
      blksize = *(WORD far*)(buf+11);
      imgsize = *(WORD far*)(buf+19);
      if (imgsize == 0)
	imgsize = *(DWORD far*)(buf+32);
    }
  }
  imgsize *= blksize;
  printf( "Image Size:\t%ld bytes\n", imgsize );

  if (outfile)
  {
    fdout = open( outfile, OFLAG, 0777 );
    if (fdout < 0)
    {
      fprintf( stderr, "ERROR: Cannot create %s.\n", outfile );
      return E_CREATE;
    }

    do
    {
      n = imgsize >> 11;
      if (n > MAX)
	n = MAX;
      else if (n == 0)
	n = 1;
      if (!CDReadLong( (UINT)n, offset ))
      {
	fputs( "Read error!", stderr );
	return E_ABORTED;
      }
      len = (UINT)n << 11;
      if (len > imgsize)
	len = (UINT)imgsize;
#ifdef _WIN32
      w = write( fdout, buf, len );
#else
      _dos_write( fdout, buf, len, &w );
#endif
      if (w != len)
      {
	fputs( "Write error!", stderr );
	return E_ABORTED;
      }
      offset += n;
      imgsize -= len;
    } while (imgsize);
    close( fdout );
    printf( "\nThe output image has been saved in: %s\n", outfile );
  }

#ifdef _WIN32
  CloseHandle( fdin );
#else
  if (fdin)
    close( fdin );
#endif

  return 0;
}


int CDReadLong( UINT SectorCount, DWORD StartSector )
{
  static long pos = 0;
  long ofs;

#ifdef _WIN32
  DWORD len;

  // Let's try and avoid unnecessary seeking (and hope long is big enough).
  ofs = StartSector << 11;
  if (ofs != pos)
    SetFilePointer( fdin, ofs, NULL, FILE_BEGIN );
  ReadFile( fdin, buf, SectorCount << 11, &len, NULL );
  pos += len;
  return (len == (SectorCount << 11));

#else
  struct REGPACK regs;
  WORD len;

  if (fdin)
  {
    // Let's try and avoid unnecessary seeking.
    ofs = StartSector << 11;
    if (ofs != pos)
      lseek( fdin, ofs, SEEK_SET );
    _dos_read( fdin, buf, SectorCount << 11, &len );
    pos += len;
    return (len == (SectorCount << 11));
  }

  regs.r_ax = 0x1508;
  regs.r_es = FP_SEG( buf );
  regs.r_bx = FP_OFF( buf );
  regs.r_cx = CD;
  regs.r_si = (WORD)(StartSector >> 16);
  regs.r_di = (WORD)StartSector;
  regs.r_dx = SectorCount;
  intr( 0x2f, &regs );
  return !(regs.r_flags & 1);	// carry flag set if error

#endif
}
