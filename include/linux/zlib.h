/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.1.3, July 9th, 1998

  Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu


  The data format used by the zlib library is described by RFCs (Request for
  Comments) 1950 to 1952 in the files ftp://ds.internic.net/rfc/rfc1950.txt
  (zlib format), rfc1951.txt (deflate format) and rfc1952.txt (gzip format).
*/

#ifndef _ZLIB_H
#define _ZLIB_H

#include <linux/zconf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZLIB_VERSION "1.1.3"

/* 
     The 'zlib' compression library provides in-memory compression and
  decompression functions, including integrity checks of the uncompressed
  data.  This version of the library supports only one compression method
  (deflation) but other algorithms will be added later and will have the same
  stream interface.

     Compression can be done in a single step if the buffers are large
  enough (for example if an input file is mmap'ed), or can be done by
  repeated calls of the compression function.  In the latter case, the
  application must provide more input and/or consume the output
  (providing more output space) before each call.

     The library also supports reading and writing files in gzip (.gz) format
  with an interface similar to that of stdio.

     The library does not install any signal handler. The decoder checks
  the consistency of the compressed data, so the library should never
  crash even in case of corrupted input.
*/

struct internal_state;

typedef struct z_stream_s {
    Byte    *next_in;   /* next input byte */
    uInt     avail_in;  /* number of bytes available at next_in */
    uLong    total_in;  /* total nb of input bytes read so far */

    Byte    *next_out;  /* next output byte should be put there */
    uInt     avail_out; /* remaining free space at next_out */
    uLong    total_out; /* total nb of bytes output so far */

    char     *msg;      /* last error message, NULL if no error */
    struct internal_state *state; /* not visible by applications */

    void     *workspace; /* memory allocated for this stream */

    int     data_type;  /* best guess about the data type: ascii or binary */
    uLong   adler;      /* adler32 value of the uncompressed data */
    uLong   reserved;   /* reserved for future use */
} z_stream;

typedef z_stream *z_streamp;

/*
   The application must update next_in and avail_in when avail_in has
   dropped to zero. It must update next_out and avail_out when avail_out
   has dropped to zero. The application must initialize zalloc, zfree and
   opaque before calling the init function. All other fields are set by the
   compression library and must not be updated by the application.

   The opaque value provided by the application will be passed as the first
   parameter for calls of zalloc and zfree. This can be useful for custom
   memory management. The compression library attaches no meaning to the
   opaque value.

   zalloc must return Z_NULL if there is not enough memory for the object.
   If zlib is used in a multi-threaded application, zalloc and zfree must be
   thread safe.

   On 16-bit systems, the functions zalloc and zfree must be able to allocate
   exactly 65536 bytes, but will not be required to allocate more than this
   if the symbol MAXSEG_64K is defined (see zconf.h). WARNING: On MSDOS,
   pointers returned by zalloc for objects of exactly 65536 bytes *must*
   have their offset normalized to zero. The default allocation function
   provided by this library ensures this (see zutil.c). To reduce memory
   requirements and avoid any allocation of 64K objects, at the expense of
   compression ratio, compile the library with -DMAX_WBITS=14 (see zconf.h).

   The fields total_in and total_out can be used for statistics or
   progress reports. After compression, total_in holds the total size of
   the uncompressed data and may be saved for use in the decompressor
   (particularly if the decompressor wants to decompress everything in
   a single step).
*/

                        /* constants */

#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1 /* will be removed, use Z_SYNC_FLUSH instead */
#define Z_PACKET_FLUSH  2
#define Z_SYNC_FLUSH    3
#define Z_FULL_FLUSH    4
#define Z_FINISH        5
/* Allowed flush values; see deflate() below for details */

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)
/* Return codes for the compression/decompression functions. Negative
 * values are errors, positive values are used for special but normal events.
 */

#define Z_NO_COMPRESSION         0
#define Z_BEST_SPEED             1
#define Z_BEST_COMPRESSION       9
#define Z_DEFAULT_COMPRESSION  (-1)
/* compression levels */

#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_DEFAULT_STRATEGY    0
/* compression strategy; see deflateInit2() below for details */

#define Z_BINARY   0
#define Z_ASCII    1
#define Z_UNKNOWN  2
/* Possible values of the data_type field */

#define Z_DEFLATED   8
/* The deflate compression method (the only one supported in this version) */

#define Z_NULL  0  /* for initializing zalloc, zfree, opaque */

                        /* basic functions */

ZEXTERN const char * ZEXPORT zlib_zlibVersion OF((void));
/* The application can compare zlibVersion and ZLIB_VERSION for consistency.
   If the first character differs, the library code actually used is
   not compatible with the zlib.h header file used by the application.
   This check is automatically made by deflateInit and inflateInit.
 */

ZEXTERN int ZEXPORT zlib_deflate_workspacesize OF((void));
/*
   Returns the number of bytes that needs to be allocated for a per-
   stream workspace.  A pointer to this number of bytes should be
   returned in stream->workspace before calling zlib_deflateInit().
*/

/* 
ZEXTERN int ZEXPORT deflateInit OF((z_streamp strm, int level));

     Initializes the internal stream state for compression. The fields
   zalloc, zfree and opaque must be initialized before by the caller.
   If zalloc and zfree are set to Z_NULL, deflateInit updates them to
   use default allocation functions.

     The compression level must be Z_DEFAULT_COMPRESSION, or between 0 and 9:
   1 gives best speed, 9 gives best compression, 0 gives no compression at
   all (the input data is simply copied a block at a time).
   Z_DEFAULT_COMPRESSION requests a default compromise between speed and
   compression (currently equivalent to level 6).

     deflateInit returns Z_OK if success, Z_MEM_ERROR if there was not
   enough memory, Z_STREAM_ERROR if level is not a valid compression level,
   Z_VERSION_ERROR if the zlib library version (zlib_version) is incompatible
   with the version assumed by the caller (ZLIB_VERSION).
   msg is set to null if there is no error message.  deflateInit does not
   perform any compression: this will be done by deflate().
*/


ZEXTERN int ZEXPORT zlib_deflate OF((z_streamp strm, int flush));
/*
    deflate compresses as much data as possible, and stops when the input
  buffer becomes empty or the output buffer becomes full. It may introduce some
  output latency (reading input without producing any output) except when
  forced to flush.

    The detailed semantics are as follows. deflate performs one or both of the
  following actions:

  - Compress more input starting at next_in and update next_in and avail_in
    accordingly. If not all input can be processed (because there is not
    enough room in the output buffer), next_in and avail_in are updated and
    processing will resume at this point for the next call of deflate().

  - Provide more output starting at next_out and update next_out and avail_out
    accordingly. This action is forced if the parameter flush is non zero.
    Forcing flush frequently degrades the compression ratio, so this parameter
    should be set only when necessary (in interactive applications).
    Some output may be provided even if flush is not set.

  Before the call of deflate(), the application should ensure that at least
  one of the actions is possible, by providing more input and/or consuming
  more output, and updating avail_in or avail_out accordingly; avail_out
  should never be zero before the call. The application can consume the
  compressed output when it wants, for example when the output buffer is full
  (avail_out == 0), or after each call of deflate(). If deflate returns Z_OK
  and with zero avail_out, it must be called again after making room in the
  output buffer because there might be more output pending.

    If the parameter flush is set to Z_SYNC_FLUSH, all pending output is
  flushed to the output buffer and the output is aligned on a byte boundary, so
  that the decompressor can get all input data available so far. (In particular
  avail_in is zero after the call if enough output space has been provided
  before the call.)  Flushing may degrade compression for some compression
  algorithms and so it should be used only when necessary.

    If flush is set to Z_FULL_FLUSH, all output is flushed as with
  Z_SYNC_FLUSH, and the compression state is reset so that decompression can
  restart from this point if previous compressed data has been damaged or if
  random access is desired. Using Z_FULL_FLUSH too often can seriously degrade
  the compression.

    If deflate returns with avail_out == 0, this function must be called again
  with the same value of the flush parameter and more output space (updated
  avail_out), until the flush is complete (deflate returns with non-zero
  avail_out).

    If the parameter flush is set to Z_FINISH, pending input is processed,
  pending output is flushed and deflate returns with Z_STREAM_END if there
  was enough output space; if deflate returns with Z_OK, this function must be
  called again with Z_FINISH and more output space (updated avail_out) but no
  more input data, until it returns with Z_STREAM_END or an error. After
  deflate has returned Z_STREAM_END, the only possible operations on the
  stream are deflateReset or deflateEnd.
  
    Z_FINISH can be used immediately after deflateInit if all the compression
  is to be done in a single step. In this case, avail_out must be at least
  0.1% larger than avail_in plus 12 bytes.  If deflate does not return
  Z_STREAM_END, then it must be called again as described above.

    deflate() sets strm->adler to the adler32 checksum of all input read
  so far (that is, total_in bytes).

    deflate() may update data_type if it can make a good guess about
  the input data type (Z_ASCII or Z_BINARY). In doubt, the data is considered
  binary. This field is only for information purposes and does not affect
  the compression algorithm in any manner.

    deflate() returns Z_OK if some progress has been made (more input
  processed or more output produced), Z_STREAM_END if all input has been
  consumed and all output has been produced (only when flush is set to
  Z_FINISH), Z_STREAM_ERROR if the stream state was inconsistent (for example
  if next_in or next_out was NULL), Z_BUF_ERROR if no progress is possible
  (for example avail_in or avail_out was zero).
*/


ZEXTERN int ZEXPORT zlib_deflateEnd OF((z_streamp strm));
/*
     All dynamically allocated data structures for this stream are freed.
   This function discards any unprocessed input and does not flush any
   pending output.

     deflateEnd returns Z_OK if success, Z_STREAM_ERROR if the
   stream state was inconsistent, Z_DATA_ERROR if the stream was freed
   prematurely (some input or output was discarded). In the error case,
   msg may be set but then points to a static string (which must not be
   deallocated).
*/


ZEXTERN int ZEXPORT zlib_inflate_workspacesize OF((void));
/*
   Returns the number of bytes that needs to be allocated for a per-
   stream workspace.  A pointer to this number of bytes should be
   returned in stream->workspace before calling zlib_inflateInit().
*/

/* 
ZEXTERN int ZEXPORT zlib_inflateInit OF((z_streamp strm));

     Initializes the internal stream state for decompression. The fields
   next_in, avail_in, and workspace must be initialized before by
   the caller. If next_in is not Z_NULL and avail_in is large enough (the exact
   value depends on the compression method), inflateInit determines the
   compression method from the zlib header and allocates all data structures
   accordingly; otherwise the allocation will be deferred to the first call of
   inflate.  If zalloc and zfree are set to Z_NULL, inflateInit updates them to
   use default allocation functions.

     inflateInit returns Z_OK if success, Z_MEM_ERROR if there was not enough
   memory, Z_VERSION_ERROR if the zlib library version is incompatible with the
   version assumed by the caller.  msg is set to null if there is no error
   message. inflateInit does not perform any decompression apart from reading
   the zlib header if present: this will be done by inflate().  (So next_in and
   avail_in may be modified, but next_out and avail_out are unchanged.)
*/


ZEXTERN int ZEXPORT zlib_inflate OF((z_streamp strm, int flush));
/*
    inflate decompresses as much data as possible, and stops when the input
  buffer becomes empty or the output buffer becomes full. It may some
  introduce some output latency (reading input without producing any output)
  except when forced to flush.

  The detailed semantics are as follows. inflate performs one or both of the
  following actions:

  - Decompress more input starting at next_in and update next_in and avail_in
    accordingly. If not all input can be processed (because there is not
    enough room in the output buffer), next_in is updated and processing
    will resume at this point for the next call of inflate().

  - Provide more output starting at next_out and update next_out and avail_out
    accordingly.  inflate() provides as much output as possible, until there
    is no more input data or no more space in the output buffer (see below
    about the flush parameter).

  Before the call of inflate(), the application should ensure that at least
  one of the actions is possible, by providing more input and/or consuming
  more output, and updating the next_* and avail_* values accordingly.
  The application can consume the uncompressed output when it wants, for
  example when the output buffer is full (avail_out == 0), or after each
  call of inflate(). If inflate returns Z_OK and with zero avail_out, it
  must be called again after making room in the output buffer because there
  might be more output pending.

    If the parameter flush is set to Z_SYNC_FLUSH, inflate flushes as much
  output as possible to the output buffer. The flushing behavior of inflate is
  not specified for values of the flush parameter other than Z_SYNC_FLUSH
  and Z_FINISH, but the current implementation actually flushes as much output
  as possible anyway.

    inflate() should normally be called until it returns Z_STREAM_END or an
  error. However if all decompression is to be performed in a single step
  (a single call of inflate), the parameter flush should be set to
  Z_FINISH. In this case all pending input is processed and all pending
  output is flushed; avail_out must be large enough to hold all the
  uncompressed data. (The size of the uncompressed data may have been saved
  by the compressor for this purpose.) The next operation on this stream must
  be inflateEnd to deallocate the decompression state. The use of Z_FINISH
  is never required, but can be used to inform inflate that a faster routine
  may be used for the single inflate() call.

     If a preset dictionary is needed at this point (see inflateSetDictionary
  below), inflate sets strm-adler to the adler32 checksum of the
  dictionary chosen by the compressor and returns Z_NEED_DICT; otherwise 
  it sets strm->adler to the adler32 checksum of all output produced
  so far (that is, total_out bytes) and returns Z_OK, Z_STREAM_END or
  an error code as described below. At the end of the stream, inflate()
  checks that its computed adler32 checksum is equal to that saved by the
  compressor and returns Z_STREAM_END only if the checksum is correct.

    inflate() returns Z_OK if some progress has been made (more input processed
  or more output produced), Z_STREAM_END if the end of the compressed data has
  been reached and all uncompressed output has been produced, Z_NEED_DICT if a
  preset dictionary is needed at this point, Z_DATA_ERROR if the input data was
  corrupted (input stream not conforming to the zlib format or incorrect
  adler32 checksum), Z_STREAM_ERROR if the stream structure was inconsistent
  (for example if next_in or next_out was NULL), Z_MEM_ERROR if there was not
  enough memory, Z_BUF_ERROR if no progress is possible or if there was not
  enough room in the output buffer when Z_FINISH is used. In the Z_DATA_ERROR
  case, the application may then call inflateSync to look for a good
  compression block.
*/


ZEXTERN int ZEXPORT zlib_inflateEnd OF((z_streamp strm));
/*
     All dynamically allocated data structures for this stream are freed.
   This function discards any unprocessed input and does not flush any
   pending output.

     inflateEnd returns Z_OK if success, Z_STREAM_ERROR if the stream state
   was inconsistent. In the error case, msg may be set but then points to a
   static string (which must not be deallocated).
*/

                        /* Advanced functions */

/*
    The following functions are needed only in some special applications.
*/

/*   
ZEXTERN int ZEXPORT deflateInit2 OF((z_streamp strm,
                                     int  level,
                                     int  method,
                                     int  windowBits,
                                     int  memLevel,
                                     int  strategy));

     This is another version of deflateInit with more compression options. The
   fields next_in, zalloc, zfree and opaque must be initialized before by
   the caller.

     The method parameter is the compression method. It must be Z_DEFLATED in
   this version of the library.

     The windowBits parameter is the base two logarithm of the window size
   (the size of the history buffer).  It should be in the range 8..15 for this
   version of the library. Larger values of this parameter result in better
   compression at the expense of memory usage. The default value is 15 if
   deflateInit is used instead.

     The memLevel parameter specifies how much memory should be allocated
   for the internal compression state. memLevel=1 uses minimum memory but
   is slow and reduces compression ratio; memLevel=9 uses maximum memory
   for optimal speed. The default value is 8. See zconf.h for total memory
   usage as a function of windowBits and memLevel.

     The strategy parameter is used to tune the compression algorithm. Use the
   value Z_DEFAULT_STRATEGY for normal data, Z_FILTERED for data produced by a
   filter (or predictor), or Z_HUFFMAN_ONLY to force Huffman encoding only (no
   string match).  Filtered data consists mostly of small values with a
   somewhat random distribution. In this case, the compression algorithm is
   tuned to compress them better. The effect of Z_FILTERED is to force more
   Huffman coding and less string matching; it is somewhat intermediate
   between Z_DEFAULT and Z_HUFFMAN_ONLY. The strategy parameter only affects
   the compression ratio but not the correctness of the compressed output even
   if it is not set appropriately.

      deflateInit2 returns Z_OK if success, Z_MEM_ERROR if there was not enough
   memory, Z_STREAM_ERROR if a parameter is invalid (such as an invalid
   method). msg is set to null if there is no error message.  deflateInit2 does
   not perform any compression: this will be done by deflate().
*/
                            
ZEXTERN int ZEXPORT zlib_deflateSetDictionary OF((z_streamp strm,
						     const Byte *dictionary,
						     uInt  dictLength));
/*
     Initializes the compression dictionary from the given byte sequence
   without producing any compressed output. This function must be called
   immediately after deflateInit, deflateInit2 or deflateReset, before any
   call of deflate. The compressor and decompressor must use exactly the same
   dictionary (see inflateSetDictionary).

     The dictionary should consist of strings (byte sequences) that are likely
   to be encountered later in the data to be compressed, with the most commonly
   used strings preferably put towards the end of the dictionary. Using a
   dictionary is most useful when the data to be compressed is short and can be
   predicted with good accuracy; the data can then be compressed better than
   with the default empty dictionary.

     Depending on the size of the compression data structures selected by
   deflateInit or deflateInit2, a part of the dictionary may in effect be
   discarded, for example if the dictionary is larger than the window size in
   deflate or deflate2. Thus the strings most likely to be useful should be
   put at the end of the dictionary, not at the front.

     Upon return of this function, strm->adler is set to the Adler32 value
   of the dictionary; the decompressor may later use this value to determine
   which dictionary has been used by the compressor. (The Adler32 value
   applies to the whole dictionary even if only a subset of the dictionary is
   actually used by the compressor.)

     deflateSetDictionary returns Z_OK if success, or Z_STREAM_ERROR if a
   parameter is invalid (such as NULL dictionary) or the stream state is
   inconsistent (for example if deflate has already been called for this stream
   or if the compression method is bsort). deflateSetDictionary does not
   perform any compression: this will be done by deflate().
*/

ZEXTERN int ZEXPORT zlib_deflateCopy OF((z_streamp dest,
					    z_streamp source));
/*
     Sets the destination stream as a complete copy of the source stream.

     This function can be useful when several compression strategies will be
   tried, for example when there are several ways of pre-processing the input
   data with a filter. The streams that will be discarded should then be freed
   by calling deflateEnd.  Note that deflateCopy duplicates the internal
   compression state which can be quite large, so this strategy is slow and
   can consume lots of memory.

     deflateCopy returns Z_OK if success, Z_MEM_ERROR if there was not
   enough memory, Z_STREAM_ERROR if the source stream state was inconsistent
   (such as zalloc being NULL). msg is left unchanged in both source and
   destination.
*/

ZEXTERN int ZEXPORT zlib_deflateReset OF((z_streamp strm));
/*
     This function is equivalent to deflateEnd followed by deflateInit,
   but does not free and reallocate all the internal compression state.
   The stream will keep the same compression level and any other attributes
   that may have been set by deflateInit2.

      deflateReset returns Z_OK if success, or Z_STREAM_ERROR if the source
   stream state was inconsistent (such as zalloc or state being NULL).
*/

ZEXTERN int ZEXPORT zlib_deflateParams OF((z_streamp strm,
					      int level,
					      int strategy));
/*
     Dynamically update the compression level and compression strategy.  The
   interpretation of level and strategy is as in deflateInit2.  This can be
   used to switch between compression and straight copy of the input data, or
   to switch to a different kind of input data requiring a different
   strategy. If the compression level is changed, the input available so far
   is compressed with the old level (and may be flushed); the new level will
   take effect only at the next call of deflate().

     Before the call of deflateParams, the stream state must be set as for
   a call of deflate(), since the currently available input may have to
   be compressed and flushed. In particular, strm->avail_out must be non-zero.

     deflateParams returns Z_OK if success, Z_STREAM_ERROR if the source
   stream state was inconsistent or if a parameter was invalid, Z_BUF_ERROR
   if strm->avail_out was zero.
*/

/*   
ZEXTERN int ZEXPORT inflateInit2 OF((z_streamp strm,
                                     int  windowBits));

     This is another version of inflateInit with an extra parameter. The
   fields next_in, avail_in, zalloc, zfree and opaque must be initialized
   before by the caller.

     The windowBits parameter is the base two logarithm of the maximum window
   size (the size of the history buffer).  It should be in the range 8..15 for
   this version of the library. The default value is 15 if inflateInit is used
   instead. If a compressed stream with a larger window size is given as
   input, inflate() will return with the error code Z_DATA_ERROR instead of
   trying to allocate a larger window.

      inflateInit2 returns Z_OK if success, Z_MEM_ERROR if there was not enough
   memory, Z_STREAM_ERROR if a parameter is invalid (such as a negative
   memLevel). msg is set to null if there is no error message.  inflateInit2
   does not perform any decompression apart from reading the zlib header if
   present: this will be done by inflate(). (So next_in and avail_in may be
   modified, but next_out and avail_out are unchanged.)
*/

ZEXTERN int ZEXPORT zlib_inflateSetDictionary OF((z_streamp strm,
						     const Byte *dictionary,
						     uInt  dictLength));
/*
     Initializes the decompression dictionary from the given uncompressed byte
   sequence. This function must be called immediately after a call of inflate
   if this call returned Z_NEED_DICT. The dictionary chosen by the compressor
   can be determined from the Adler32 value returned by this call of
   inflate. The compressor and decompressor must use exactly the same
   dictionary (see deflateSetDictionary).

     inflateSetDictionary returns Z_OK if success, Z_STREAM_ERROR if a
   parameter is invalid (such as NULL dictionary) or the stream state is
   inconsistent, Z_DATA_ERROR if the given dictionary doesn't match the
   expected one (incorrect Adler32 value). inflateSetDictionary does not
   perform any decompression: this will be done by subsequent calls of
   inflate().
*/

ZEXTERN int ZEXPORT zlib_inflateSync OF((z_streamp strm));
/* 
    Skips invalid compressed data until a full flush point (see above the
  description of deflate with Z_FULL_FLUSH) can be found, or until all
  available input is skipped. No output is provided.

    inflateSync returns Z_OK if a full flush point has been found, Z_BUF_ERROR
  if no more input was provided, Z_DATA_ERROR if no flush point has been found,
  or Z_STREAM_ERROR if the stream structure was inconsistent. In the success
  case, the application may save the current current value of total_in which
  indicates where valid compressed data was found. In the error case, the
  application may repeatedly call inflateSync, providing more input each time,
  until success or end of the input data.
*/

ZEXTERN int ZEXPORT zlib_inflateReset OF((z_streamp strm));
/*
     This function is equivalent to inflateEnd followed by inflateInit,
   but does not free and reallocate all the internal decompression state.
   The stream will keep attributes that may have been set by inflateInit2.

      inflateReset returns Z_OK if success, or Z_STREAM_ERROR if the source
   stream state was inconsistent (such as zalloc or state being NULL).
*/

extern int ZEXPORT zlib_inflateIncomp OF((z_stream *strm));
/*
     This function adds the data at next_in (avail_in bytes) to the output
   history without performing any output.  There must be no pending output,
   and the decompressor must be expecting to see the start of a block.
   Calling this function is equivalent to decompressing a stored block
   containing the data at next_in (except that the data is not output).
*/

                        /* various hacks, don't look :) */

/* deflateInit and inflateInit are macros to allow checking the zlib version
 * and the compiler's view of z_stream:
 */
ZEXTERN int ZEXPORT zlib_deflateInit_ OF((z_streamp strm, int level,
                                     const char *version, int stream_size));
ZEXTERN int ZEXPORT zlib_inflateInit_ OF((z_streamp strm,
                                     const char *version, int stream_size));
ZEXTERN int ZEXPORT zlib_deflateInit2_ OF((z_streamp strm, int  level, int  method,
                                      int windowBits, int memLevel,
                                      int strategy, const char *version,
                                      int stream_size));
ZEXTERN int ZEXPORT zlib_inflateInit2_ OF((z_streamp strm, int  windowBits,
                                      const char *version, int stream_size));
#define zlib_deflateInit(strm, level) \
        zlib_deflateInit_((strm), (level), ZLIB_VERSION, sizeof(z_stream))
#define zlib_inflateInit(strm) \
        zlib_inflateInit_((strm), ZLIB_VERSION, sizeof(z_stream))
#define zlib_deflateInit2(strm, level, method, windowBits, memLevel, strategy) \
        zlib_deflateInit2_((strm),(level),(method),(windowBits),(memLevel),\
                      (strategy), ZLIB_VERSION, sizeof(z_stream))
#define zlib_inflateInit2(strm, windowBits) \
        zlib_inflateInit2_((strm), (windowBits), ZLIB_VERSION, sizeof(z_stream))


#if !defined(_Z_UTIL_H) && !defined(NO_DUMMY_DECL)
    struct internal_state {int dummy;}; /* hack for buggy compilers */
#endif

ZEXTERN const char  * ZEXPORT zlib_zError           OF((int err));
ZEXTERN int           ZEXPORT zlib_inflateSyncPoint OF((z_streamp z));
ZEXTERN const uLong * ZEXPORT zlib_get_crc_table    OF((void));

#ifdef __cplusplus
}
#endif

#endif /* _ZLIB_H */
