/* dictzip.c -- 
 * Created: Tue Jul 16 12:45:41 1996 by r.faith@ieee.org
 * Revised: Fri Mar  7 11:00:28 1997 by faith@cs.unc.edu
 * Copyright 1996 Rickard E. Faith (r.faith@ieee.org)
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 1, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id: dictzip.c,v 1.8 1997/03/07 16:10:34 faith Exp $
 * 
 */

#include "dictzip.h"
#include <sys/stat.h>

void dict_data_print_header( FILE *str, dictData *header )
{
   char        *date, *year;
   long        ratio, num, den;
   static int  first = 1;

   if (first) {
      fprintf( str,
	       "type   crc        date    time chunks  size     compr."
	       "  uncompr. ratio name\n" );
      first = 0;
   }
   
   switch (header->type) {
   case DICT_TEXT:
      date = ctime( &header->mtime ) + 4; /* no day of week */
      date[12] = date[20] = '\0'; /* no year or newline*/
      year = &date[16];
      fprintf( str, "text %08lx %s %11s ", header->crc, year, date );
      fprintf( str, "            " );
      fprintf( str, "          %9ld ", header->length );
      fprintf( str, "  0.0%% %s",
	       header->origFilename ? header->origFilename : "" );
      putc( '\n', str );
      break;
   case DICT_GZIP:
   case DICT_DZIP:
      fprintf( str, "%s", header->type == DICT_DZIP ? "dzip " : "gzip " );
#if 0
      switch (header->method) {
      case 0:  fprintf( str, "store" ); break;
      case 1:  fprintf( str, "compr" ); break;
      case 2:  fprintf( str, "pack " ); break;
      case 3:  fprintf( str, "lzh  " ); break;
      case 8:  fprintf( str, "defla" ); break;
      default: fprintf( str, "?    " ); break;
      }
#endif
      date = ctime( &header->mtime ) + 4; /* no day of week */
      date[12] = date[20] = '\0'; /* no year or newline*/
      year = &date[16];
      fprintf( str, "%08lx %s %11s ", header->crc, year, date );
      if (header->type == DICT_DZIP) {
	 fprintf( str, "%5d %5d ", header->chunkCount, header->chunkLength );
      } else {
	 fprintf( str, "            " );
      }
      fprintf( str, "%9ld %9ld ", header->compressedLength, header->length );
      /* Algorithm for calculating ratio from gzip-1.2.4,
         util.c:display_ratio Copyright (C) 1992-1993 Jean-loup Gailly.
         May be distributed under the terms of the GNU General Public
         License. */
      num = header->length-(header->compressedLength-header->headerLength);
      den = header->length;
      if (!den)
	 ratio = 0;
      else if (den < 2147483L)
	 ratio = 1000L * num / den;
      else
	 ratio = num / (den/1000L);
      if (ratio < 0) {
	 putc( '-', str );
	 ratio = -ratio;
      } else putc( ' ', str );
      fprintf( str, "%2ld.%1ld%%", ratio / 10L, ratio % 10L);
      fprintf( str, " %s",
	       header->origFilename ? header->origFilename : "" );
      putc( '\n', str );
      break;
   case DICT_UNKNOWN:
   default:
      break;
   }
}

int dict_data_zip( const char *inFilename, const char *outFilename,
		   const char *preFilter, const char *postFilter )
{
   char          inBuffer[IN_BUFFER_SIZE];
   char          outBuffer[OUT_BUFFER_SIZE];
   int           count;
   unsigned long inputCRC = crc32( 0L, Z_NULL, 0 );
   z_stream      zStream;
   FILE          *outStr;
   FILE          *inStr;
   int           len;
   struct stat   st;
   char          *header;
   int           headerLength;
   int           dataLength;
   int           extraLength;
   int           chunkLength;
#if HEADER_CRC
   int           headerCRC;
#endif
   unsigned long chunks;
   unsigned long chunk = 0;
   unsigned long total = 0;
   int           i;
   char          tail[8];
   char          *pt, *origFilename;

   
   /* Open files */
   if (!(inStr = fopen( inFilename, "r" )))
      err_fatal_errno( __FUNCTION__,
		       "Cannot open \"%s\" for read", inFilename );
   if (!(outStr = fopen( outFilename, "w" )))
      err_fatal_errno( __FUNCTION__,
		       "Cannot open \"%s\"for write", outFilename );

   origFilename = xmalloc( strlen( inFilename ) + 1 );
   if ((pt = strrchr( inFilename, '/' )))
      strcpy( origFilename, pt + 1 );
   else
      strcpy( origFilename, inFilename );

   /* Initialize compression engine */
   zStream.zalloc    = NULL;
   zStream.zfree     = NULL;
   zStream.opaque    = NULL;
   zStream.next_in   = NULL;
   zStream.avail_in  = 0;
   zStream.next_out  = NULL;
   zStream.avail_out = 0;
   if (deflateInit2( &zStream,
		     Z_BEST_COMPRESSION,
		     Z_DEFLATED,
		     -15,	/* Suppress zlib header */
		     Z_BEST_COMPRESSION,
		     Z_DEFAULT_STRATEGY ) != Z_OK)
      err_internal( __FUNCTION__,
		    "Cannot initialize deflation engine: %s\n", zStream.msg );

   /* Write initial header information */
   chunkLength = (preFilter ? PREFILTER_IN_BUFFER_SIZE : IN_BUFFER_SIZE );
   fstat( fileno( inStr ), &st );
   chunks = st.st_size / chunkLength;
   if (st.st_size % chunkLength) ++chunks;
   PRINTF(DBG_VERBOSE,("%lu chunks * %u per chunk = %lu (filesize = %lu)\n",
			chunks, chunkLength, chunks * chunkLength,
			st.st_size ));
   dataLength   = chunks * 2;
   extraLength  = 10 + dataLength;
   headerLength = GZ_FEXTRA_START
		  + extraLength		/* FEXTRA */
		  + strlen( origFilename ) + 1	/* FNAME  */
		  + (HEADER_CRC ? 2 : 0);	/* FHCRC  */
   PRINTF(DBG_VERBOSE,("(data = %d, extra = %d, header = %d)\n",
		       dataLength, extraLength, headerLength ));
   header = xmalloc( headerLength );
   for (i = 0; i < headerLength; i++) header[i] = 0;
   header[GZ_ID1]        = GZ_MAGIC1;
   header[GZ_ID2]        = GZ_MAGIC2;
   header[GZ_CM]         = Z_DEFLATED;
   header[GZ_FLG]        = GZ_FEXTRA | GZ_FNAME;
#if HEADER_CRC
   header[GZ_FLG]        |= GZ_FHCRC;
#endif
   header[GZ_MTIME+3]    = (st.st_mtime & 0xff000000) >> 24;
   header[GZ_MTIME+2]    = (st.st_mtime & 0x00ff0000) >> 16;
   header[GZ_MTIME+1]    = (st.st_mtime & 0x0000ff00) >>  8;
   header[GZ_MTIME+0]    = (st.st_mtime & 0x000000ff) >>  0;
   header[GZ_XFL]        = GZ_MAX;
   header[GZ_OS]         = GZ_OS_UNIX;
   header[GZ_XLEN+1]     = (extraLength & 0xff00) >> 8;
   header[GZ_XLEN+0]     = (extraLength & 0x00ff) >> 0;
   header[GZ_SI1]        = GZ_RND_S1;
   header[GZ_SI2]        = GZ_RND_S2;
   header[GZ_SUBLEN+1]   = ((extraLength - 4) & 0xff00) >> 8;
   header[GZ_SUBLEN+0]   = ((extraLength - 4) & 0x00ff) >> 0;
   header[GZ_VERSION+1]  = 0;
   header[GZ_VERSION+0]  = 1;
   header[GZ_CHUNKLEN+1] = (chunkLength & 0xff00) >> 8;
   header[GZ_CHUNKLEN+0] = (chunkLength & 0x00ff) >> 0;
   header[GZ_CHUNKCNT+1] = (chunks & 0xff00) >> 8;
   header[GZ_CHUNKCNT+0] = (chunks & 0x00ff) >> 0;
   strcpy( &header[GZ_FEXTRA_START + extraLength], origFilename );
   fwrite( header, 1, headerLength, outStr );
    
   /* Read, compress, write */
   while (!feof( inStr )) {
      if ((count = fread( inBuffer, 1, chunkLength, inStr ))) {
	 dict_data_filter( inBuffer, &count, IN_BUFFER_SIZE, preFilter );
	 
	 inputCRC = crc32( inputCRC, inBuffer, count );
	 zStream.next_in   = inBuffer;
	 zStream.avail_in  = count;
	 zStream.next_out  = outBuffer;
	 zStream.avail_out = OUT_BUFFER_SIZE;
	 if (deflate( &zStream, Z_FULL_FLUSH ) != Z_OK)
	    err_fatal( __FUNCTION__, "deflate: %s\n", zStream.msg );
	 assert( zStream.avail_in == 0 );
	 len = OUT_BUFFER_SIZE - zStream.avail_out;
	 assert( len <= 0xffff );

	 dict_data_filter( outBuffer, &len, OUT_BUFFER_SIZE, postFilter );
	 
	 assert( len <= 0xffff );
	 header[GZ_RNDDATA + chunk*2 + 1] = (len & 0xff00) >>  8;
	 header[GZ_RNDDATA + chunk*2 + 0] = (len & 0x00ff) >>  0;
	 fwrite( outBuffer, 1, len, outStr );

	 ++chunk;
	 total += count;
	 if (dbg_test( DBG_VERBOSE )) {
	    printf( "chunk %5lu: %lu of %lu total\r",
		    chunk, total, st.st_size );
	    fflush( stdout );
	 }
      }
   }
   PRINTF(DBG_VERBOSE,("total: %lu chunks, %lu bytes\n", chunks, st.st_size));
    
   /* Write last bit */
#if 0
   dmalloc_verify(0);
#endif
   zStream.next_in   = inBuffer;
   zStream.avail_in  = 0;
   zStream.next_out  = outBuffer;
   zStream.avail_out = OUT_BUFFER_SIZE;
   if (deflate( &zStream, Z_FINISH ) != Z_STREAM_END)
      err_fatal( __FUNCTION__, "deflate: %s\n", zStream.msg );
   assert( zStream.avail_in == 0 );
   len = OUT_BUFFER_SIZE - zStream.avail_out;
   fwrite( outBuffer, 1, len, outStr );
   PRINTF(DBG_VERBOSE,("(wrote %d bytes, final, crc = %lx)\n",
		       len, inputCRC ));

   /* Write CRC and length */
#if 0
   dmalloc_verify(0);
#endif
   tail[0 + 3] = (inputCRC & 0xff000000) >> 24;
   tail[0 + 2] = (inputCRC & 0x00ff0000) >> 16;
   tail[0 + 1] = (inputCRC & 0x0000ff00) >>  8;
   tail[0 + 0] = (inputCRC & 0x000000ff) >>  0;
   tail[4 + 3] = (st.st_size & 0xff000000) >> 24;
   tail[4 + 2] = (st.st_size & 0x00ff0000) >> 16;
   tail[4 + 1] = (st.st_size & 0x0000ff00) >>  8;
   tail[4 + 0] = (st.st_size & 0x000000ff) >>  0;
   fwrite( tail, 1, 8, outStr );

   /* Write final header information */
#if 0
   dmalloc_verify(0);
#endif
   rewind( outStr );
#if HEADER_CRC
   headerCRC = crc32( 0L, Z_NULL, 0 );
   headerCRC = crc32( headerCRC, header, headerLength - 2);
   header[headerLength - 1] = (headerCRC & 0xff00) >> 8;
   header[headerLength - 2] = (headerCRC & 0x00ff) >> 0;
#endif
   fwrite( header, 1, headerLength, outStr );

   /* Close files */
#if 0
   dmalloc_verify(0);
#endif
   fclose( outStr );
   fclose( inStr );
    
   /* Shut down compression */
   if (deflateEnd( &zStream ) != Z_OK)
      err_fatal( __FUNCTION__, "defalteEnd: %s\n", zStream.msg );

   xfree( origFilename );
   xfree( header );

   return 0;
}

static const char *id_string( const char *id )
{
   static char buffer[BUFFERSIZE];
   arg_List a = arg_argify( id );

   sprintf( buffer, "%s (%s)", arg_get( a, 2 ), arg_get( a, 3 ) );
   arg_destroy( a );
   return buffer;
}

static void banner( void )
{
   const char *id = "$Id: dictzip.c,v 1.8 1997/03/07 16:10:34 faith Exp $";
   
   fprintf( stderr, "%s %s\n", err_program_name(), id_string( id ) );
   fprintf( stderr, "Copyright 1996 Rickard E. Faith (faith@cs.unc.edu)\n" );
}

static void license( void )
{
   static const char *license_msg[] = {
     "",
     "This program is free software; you can redistribute it and/or modify it",
     "under the terms of the GNU General Public License as published by the",
     "Free Software Foundation; either version 1, or (at your option) any",
     "later version.",
     "",
     "This program is distributed in the hope that it will be useful, but",
     "WITHOUT ANY WARRANTY; without even the implied warranty of",
     "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU",
     "General Public License for more details.",
     "",
     "You should have received a copy of the GNU General Public License along",
     "with this program; if not, write to the Free Software Foundation, Inc.,",
     "675 Mass Ave, Cambridge, MA 02139, USA.",
   };
   const char        **p = license_msg;
   
   banner();
   while (*p) fprintf( stderr, "   %s\n", *p++ );
}
    
static void help( void )
{
   static const char *help_msg[] = {
      "-d --decompress      decompress",
      "-f --force           force overwrite of output file",
      "-h --help            give this help",
      "-k --keep            do not delete original file",
      "-l --list            list compressed file contents",
      "-L --license         display software license",
      "   --stdout          write to stdout (decompression only)",
      "-t --test            test compressed file integrity",
      "-v --verbose         verbose mode",
      "-V --version         display version number",
      "-D --debug           select debug option",
      "-s --start <offset>  starting offset for decompression (decimal)",
      "-e --end <offset>    ending offset for decompression (decimal)",
      "-S --Start <offset>  starting offset for decompression (base64)",
      "-E --End <offset>    ending offset for decompression (base64)",
      "-p --pre <filter>    pre-compression filter",
      "-P --post <filter>   post-compression filter",
      0 };
   const char        **p = help_msg;

   banner();
   while (*p) fprintf( stderr, "%s\n", *p++ );
}

int main( int argc, char **argv )
{
   int           c;
   int           i;
   int           decompressFlag = 0;
   int           forceFlag      = 0;
   int           keepFlag       = 0;
   int           listFlag       = 0;
   int           stdoutFlag     = 0;
   int           testFlag       = 0;
   char          buffer[BUFFERSIZE];
   char          *buf;
   char          *pre           = NULL;
   char          *post          = NULL;
   unsigned long start          = 0;
   unsigned long end            = 0;
   dictData      *header;
   struct option longopts[] = {
      { "decompress",   0, 0, 'd' },
      { "force",        0, 0, 'f' },
      { "help",         0, 0, 'h' },
      { "keep",         0, 0, 'k' },
      { "list",         0, 0, 'l' },
      { "license",      0, 0, 'L' },
      { "stdout",       0, 0, 513 },
      { "test",         0, 0, 't' },
      { "verbose",      0, 0, 'v' },
      { "version",      0, 0, 'V' },
      { "debug",        1, 0, 'D' },
      { "start",        1, 0, 's' },
      { "end",          1, 0, 'e' },
      { "Start",        1, 0, 'S' },
      { "End",          1, 0, 'E' },
      { "pre",          1, 0, 'p' },
      { "post",         1, 0, 'P' },
      { 0,              0, 0,  0  }
   };

   /* Initialize Libmaa */
   maa_init( argv[0] );
   dbg_register( DBG_VERBOSE, "verbose" );
   dbg_register( DBG_ZIP,     "zip" );
   dbg_register( DBG_UNZIP,   "unzip" );
   
#if 0
   if (signal( SIGINT, SIG_IGN ) != SIG_IGN)  signal( SIGINT, sig_handler );
   if (signal( SIGQUIT, SIG_IGN ) != SIG_IGN) signal( SIGQUIT, sig_handler );
#endif

   while ((c = getopt_long( argc, argv,
			    "cdfhklLe:E:s:S:tvVD:p:P:",
			    longopts, NULL )) != EOF)
      switch (c) {
      case 'd': ++decompressFlag;                                      break;
      case 'f': ++forceFlag;                                           break;
      case 'k': ++keepFlag;                                            break;
      case 'l': ++listFlag;                                            break;
      case 'L': license(); exit( 1 );                                  break;
      case 513: ++stdoutFlag;                                          break;
      case 't': ++testFlag;                                            break;
      case 'v': dbg_set( "verbose" );                                  break;
      case 'V': banner(); exit( 1 );                                   break;
      case 'D': dbg_set( optarg );                                     break;
      case 's': ++decompressFlag; start = strtoul( optarg, NULL, 10 ); break;
      case 'e': ++decompressFlag; end   = strtoul( optarg, NULL, 10 ); break;
      case 'S': ++decompressFlag; start = b64_decode( optarg );        break;
      case 'E': ++decompressFlag; end   = b64_decode( optarg );        break;
      case 'p': pre = optarg;                                          break;
      case 'P': post = optarg;                                         break;
      default:  
      case 'h': help(); exit( 1 );                                     break;
      }

   for (i = optind; i < argc; i++) {
      if (listFlag) {
	 header = dict_data_open( argv[i], 1 );
	 dict_data_print_header( stdout, header );
	 dict_data_close( header );
      } else if (decompressFlag) {
	 header = dict_data_open( argv[i], 0 );
	 if (!end) end = header->length;
	 buf = dict_data_read( header, start, end, pre, post );
	 fwrite( buf, end-start, 1, stdout );
	 fflush( stdout );
	 xfree( buf );
	 dict_data_close( header );
      } else {
	 sprintf( buffer, "%s.dz", argv[i] );
	 if (!dict_data_zip( argv[i], buffer, pre, post )) {
	    if (!keepFlag && unlink( argv[i] ))
		err_fatal_errno( __FUNCTION__, "Cannot unlink %s\n", argv[i] );
	 } else {
	    err_fatal( __FUNCTION__, "Compression failed\n" );
	 }
      }
   }

   return 0;
}
