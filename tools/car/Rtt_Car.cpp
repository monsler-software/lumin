//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_Archive.h"
#include "Rtt_FileSystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include "Rtt_Car.h"
 
// ----------------------------------------------------------------------------

using namespace Rtt;

static void
Usage( const char* arg0 )
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s [--xor-key key|--xor-key-file path|--no-xor] {-a|--add} dest.car srcfile0 [srcfile1 ...]\n", arg0);
	fprintf(stderr, "  %s [--xor-key key|--xor-key-file path|--no-xor] {-f|--filelist} filelist dest.car\n", arg0);
	fprintf(stderr, "  %s {-x|--extract} src.car destdir\n", arg0);
	fprintf(stderr, "  %s {-l|--list} src.car\n", arg0);
}

static char*
ReadTextFile( const char *path )
{
	FILE *file = fopen( path, "rb" );
	if ( ! file )
	{
		fprintf( stderr, "car: cannot open key file '%s'\n", path );
		return NULL;
	}

	fseek( file, 0, SEEK_END );
	long fileSize = ftell( file );
	fseek( file, 0, SEEK_SET );

	if ( fileSize < 0 )
	{
		fclose( file );
		return NULL;
	}

	char *buffer = (char*)calloc( (size_t)fileSize + 1, sizeof( char ) );
	if ( ! buffer )
	{
		fclose( file );
		return NULL;
	}

	size_t bytesRead = fread( buffer, 1, (size_t)fileSize, file );
	buffer[bytesRead] = '\0';
	fclose( file );

	while ( bytesRead > 0 && ( buffer[bytesRead - 1] == '\n' || buffer[bytesRead - 1] == '\r' ) )
	{
		buffer[--bytesRead] = '\0';
	}

	return buffer;
}

static void
SerializeArchive(
	const char *dstPath,
	int numSrcPaths,
	const char *srcPaths[],
	bool hasArchiveOptions,
	const char *xorKey,
	bool useXor )
{
	if ( hasArchiveOptions )
	{
		Archive::Serialize( dstPath, numSrcPaths, srcPaths, xorKey, useXor );
	}
	else
	{
		Archive::Serialize( dstPath, numSrcPaths, srcPaths );
	}
}

// ----------------------------------------------------------------------------

Rtt_EXPORT int
Rtt_CarMain( int argc, const char *argv[] )
{
	int result = 0;
	bool useXor = true;
	bool hasArchiveOptions = false;
	const char *xorKey = NULL;
	char *xorKeyBuffer = NULL;
	int argIndex = 1;

	while ( argIndex < argc )
	{
		if ( 0 == strcmp( argv[argIndex], "--xor-key" ) )
		{
			if ( argIndex + 1 >= argc )
			{
				Usage( argv[0] );
				return -1;
			}
			xorKey = argv[argIndex + 1];
			hasArchiveOptions = true;
			argIndex += 2;
		}
		else if ( 0 == strcmp( argv[argIndex], "--xor-key-file" ) )
		{
			if ( argIndex + 1 >= argc )
			{
				Usage( argv[0] );
				return -1;
			}
			free( xorKeyBuffer );
			xorKeyBuffer = ReadTextFile( argv[argIndex + 1] );
			if ( ! xorKeyBuffer )
			{
				return -1;
			}
			xorKey = xorKeyBuffer;
			hasArchiveOptions = true;
			argIndex += 2;
		}
		else if ( 0 == strcmp( argv[argIndex], "--no-xor" ) )
		{
			useXor = false;
			hasArchiveOptions = true;
			argIndex += 1;
		}
		else
		{
			break;
		}
	}

	if ( argc - argIndex < 2 )
	{
		Usage( argv[0] );
		result = -1;
	}
	else
	{
		if (0 == strcmp(argv[argIndex], "-x") || 0 == strcmp(argv[argIndex], "--extract"))
		{
			if ( argc - argIndex < 3 )
			{
				Usage( argv[0] );
				result = -1;
			}
			else
			{
				Archive::Deserialize( argv[argIndex + 2], argv[argIndex + 1] );
			}
		}
		else if (0 == strcmp(argv[argIndex], "-l") || 0 == strcmp(argv[argIndex], "--list"))
		{
			if (argc - argIndex < 2)
			{
				Usage(argv[0]);
				result = -1;
			}
			else
			{
				Archive::List(argv[argIndex + 1]);
			}
		}
		else if (0 == strcmp(argv[argIndex], "-f") || 0 == strcmp(argv[argIndex], "--filelist"))
		{
			if ( argc != argIndex + 3 )
			{
				Usage( argv[0] );
				result = -1;
			}
			else
			{
				FILE *inFile = NULL;
				
				// If the filename is "-" read file list from stdin
				if ( 0 == strcmp( argv[argIndex + 1], "-" ) )
				{
					inFile = stdin;
				}
				else
				{
					if ((inFile = fopen(argv[argIndex + 1], "r")) == NULL)
					{
						fprintf(stderr, "%s: cannot open '%s' for reading\n", argv[0], argv[argIndex + 1]);
						
						return -1;
					}
				}
				
				int numSrcPaths = 0;
				int allocStride = 100;
				int numAlloced = allocStride;
				int spaceLeft = numAlloced;
				char buf[BUFSIZ];
				const char **srcPaths = (const char **) calloc(numAlloced, sizeof(char *));
				
				if (srcPaths == NULL)
				{
					fprintf(stderr, "%s: out of memory allocating %d filenames\n", argv[0], numAlloced);
					
					return -1;
				}
				
				while (fgets(buf, BUFSIZ, inFile) != NULL)
				{
					// zap the newline
					size_t len = strlen(buf);
					if (buf[len-1] == '\n')
					{
						buf[len-1] = '\0';
					}
					
					if (spaceLeft == 0)
					{
						numAlloced += allocStride;
						spaceLeft = allocStride;
						
						srcPaths = (const char **) realloc( srcPaths, (numAlloced * sizeof(char *)));
					}
					
					if (srcPaths == NULL)
					{
						fprintf(stderr, "%s: out of memory allocating %d filenames\n", argv[0], numAlloced);
						
						return -1;
					}
					
					if ((srcPaths[numSrcPaths++] = strdup(buf)) == NULL)
					{
						fprintf(stderr, "%s: out of memory after processing %d filenames\n", argv[0], numSrcPaths);
						
						return -1;
					}
					
					// printf("srcPath '%s', spaceLeft %d, numAlloced %d, numSrcPaths %d\n", srcPaths[numSrcPaths-1], spaceLeft,numAlloced,numSrcPaths);
										
					--spaceLeft;
				}
				
				fclose( inFile );
				
				SerializeArchive( argv[argIndex + 2], numSrcPaths, srcPaths, hasArchiveOptions, xorKey, useXor );
				
				// Free the memory we allocated
				for (int i = 0; i < numSrcPaths; i++)
				{
					free( const_cast<char *>(srcPaths[i]) );
				}
				free( srcPaths );
			}
		}
		else
		{
			bool isAddMode = (0 == strcmp(argv[argIndex], "-a") || 0 == strcmp(argv[argIndex], "--add"));
			int dstIndex = isAddMode ? argIndex + 1 : argIndex;
			int srcIndex = isAddMode ? argIndex + 2 : argIndex + 1;
			int numSrcPaths = argc - srcIndex;
			const char **srcPaths = argv + srcIndex;
			#if 0
				for (int i = 0; i < argc; i++ )
				{
					printf( "argv[%d] = %s\n", i, argv[i] );
				}
			#endif
			SerializeArchive( argv[dstIndex], numSrcPaths, srcPaths, hasArchiveOptions, xorKey, useXor );
		}
	}

	free( xorKeyBuffer );
    return result;
}
