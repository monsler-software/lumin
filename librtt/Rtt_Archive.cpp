//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Solar2D game engine.
// With contributions from Dianchu Technology
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_Archive.h"

#include "Rtt_LuaFile.h"
#include "Core/Rtt_String.h"
#include "Core/Rtt_FileSystem.h"

#if !defined( Rtt_NO_ARCHIVE )
	#include "Rtt_LuaContext.h"
	#include "Rtt_Runtime.h"
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <string>
#include <vector>

#if defined( Rtt_WIN_ENV ) || defined( Rtt_POWERVR_ENV ) // || defined( Rtt_NXS_ENV )
	#include <io.h>
	#include <sys/stat.h>
	static const unsigned S_IRUSR = _S_IREAD;     ///< read by user
	static const unsigned S_IWUSR = _S_IWRITE;    ///< write by user
#elif defined( Rtt_ANDROID_ENV )
	#include "NativeToJavaBridge.h"
	#include <sys/mman.h>
#else
	#include <stdlib.h>
	#include <unistd.h>
	#include <sys/mman.h>
#endif

#include <errno.h>
#include <sys/stat.h>

// #define Rtt_DEBUG_ARCHIVE 1

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

enum
{
	kArchiveVersion1 = 0x1,
	kArchiveVersion2 = 0x2,
	kArchiveDefaultVersion = kArchiveVersion2,
	kArchiveFlagXorData = 0x1
};

static const U32 kArchiveSeedMask = 0x4C554D49; // "LUMI"

static U32
ArchiveMix32( U32 value )
{
	value ^= value >> 16;
	value *= 0x7feb352d;
	value ^= value >> 15;
	value *= 0x846ca68b;
	value ^= value >> 16;
	return value;
}

static U32
ArchiveHashString( U32 seed, const char *value )
{
	if ( ! value )
	{
		return ArchiveMix32( seed ^ 0x9e3779b9 );
	}

	U32 hash = seed ? seed : 2166136261u;
	while ( *value )
	{
		hash ^= (U8)*value++;
		hash *= 16777619u;
	}
	return ArchiveMix32( hash );
}

static U32
ArchiveEncodeSeed( U32 seed )
{
	return ( ( seed << 7 ) | ( seed >> 25 ) ) ^ kArchiveSeedMask ^ 0xA341316Cu;
}

static U32
ArchiveDecodeSeed( U32 encodedSeed )
{
	// This is intentionally not cryptographic. It keeps the seed out of plain
	// text while preserving a compact self-contained archive format.
	U32 value = encodedSeed ^ kArchiveSeedMask ^ 0xA341316Cu;
	return ( value >> 7 ) | ( value << 25 );
}

static U8
ArchiveXorByte( U32 seed, U32 absoluteOffset )
{
	U32 value = seed ^ ( absoluteOffset * 0x9e3779b9u ) ^ 0xC2B2AE35u;
	value = ArchiveMix32( value );
	return (U8)( value ^ ( value >> 8 ) ^ ( value >> 16 ) ^ ( value >> 24 ) );
}

static void
ArchiveXorTransform( void *data, size_t len, U32 seed, U32 absoluteOffset )
{
	U8 *bytes = (U8*)data;
	for ( size_t i = 0; i < len; i++ )
	{
		bytes[i] ^= ArchiveXorByte( seed, absoluteOffset + (U32)i );
	}
}

static bool
IsTruthyEnvironmentValue( const char *value )
{
	return value && ( 0 == strcmp( value, "1" )
		|| 0 == Rtt_StringCompareNoCase( value, "true" )
		|| 0 == Rtt_StringCompareNoCase( value, "yes" )
		|| 0 == Rtt_StringCompareNoCase( value, "on" ) );
}

static const char*
GetArchiveXorKeyFromEnvironment()
{
	const char *key = getenv( "LUMIN_CAR_XOR_KEY" );
	if ( ! key || '\0' == key[0] )
	{
		key = getenv( "CORONA_CAR_XOR_KEY" );
	}
	return key;
}

static bool
IsArchiveXorDisabledByEnvironment()
{
	return IsTruthyEnvironmentValue( getenv( "LUMIN_CAR_NO_XOR" ) )
		|| IsTruthyEnvironmentValue( getenv( "CORONA_CAR_NO_XOR" ) );
}

static U32
CreateArchiveXorSeed(
	const char *dstPath,
	const char *xorKey,
	const std::vector<std::string>& fileList )
{
	U32 seed = ArchiveHashString( 0xC0DEC0DEu, xorKey && xorKey[0] ? xorKey : "lumin-resource-car-v2" );
	seed = ArchiveHashString( seed, dstPath );

	for ( std::vector<std::string>::const_iterator it = fileList.begin(); it != fileList.end(); ++it )
	{
		seed = ArchiveHashString( seed, it->c_str() );
	}

	if ( ! xorKey || '\0' == xorKey[0] )
	{
		seed ^= (U32)time( NULL );
	}

	seed = ArchiveMix32( seed );
	return seed ? seed : 0x13579BDFu;
}

static bool
CopyFile( FILE *src, FILE *dst, long *numBytes, U32 xorSeed, U32 absoluteOffset )
{
	bool result = true;
	Rtt_ASSERT( src && dst );

	long startPos = ftell( dst );

	char buffer[4096];
	size_t totalBytesRead = 0;
	size_t objectsRead = 0;
	while ( ( objectsRead = fread( buffer, sizeof( buffer[0] ), sizeof( buffer ), src ) ) > 0 )
	{
		if ( xorSeed )
		{
			ArchiveXorTransform( buffer, objectsRead, xorSeed, absoluteOffset + (U32)totalBytesRead );
		}
		if ( fwrite( buffer, sizeof( buffer[0] ), objectsRead, dst ) < objectsRead )
		{
			Rtt_TRACE( ( "ERROR: Copy could not be copied!\n" ) );
			result = false;
			goto exit_gracefully;
		}
		totalBytesRead += objectsRead;
	}

	if ( ferror( src ) )
	{
		result = false;
	}

/*
// Only works for plain text files
#if 1
    char buf[256];
    const size_t kNumElem = sizeof(buf) / sizeof(buf[0]);

    const size_t kElemSize = sizeof( buf[0] ); // obj size

    size_t objectsRead = 0;
    while ( ( objectsRead = fread( buf, kElemSize, kNumElem, src ) ) )
    {
        if ( fwrite( buf, kElemSize, objectsRead, dst ) < objectsRead )
        {
            result = false;
            goto exit_gracefully;
        }
    } 
#else
	char buf[256];
	const size_t kBufSize = sizeof(buf) / sizeof(buf[0]);

	while ( fgets( buf, kBufSize, src ) )
	{
		if ( ! Rtt_VERIFY( EOF != fputs( buf, dst ) ) )
		{
			Rtt_TRACE( ( "ERROR: Copy could not be copied!\n" ) );
			result = false;
			goto exit_gracefully;
		}
	}
#endif
*/

exit_gracefully:
	if ( numBytes )
	{
		*numBytes = ftell( dst ) - startPos;
	}
	return result;
}

static bool
CopyFile( FILE *src, FILE *dst, long *numBytes )
{
	return CopyFile( src, dst, numBytes, 0, 0 );
}

template < size_t N >
static size_t
GetByteAlignedValue( size_t x )
{
	Rtt_STATIC_ASSERT( 0 == (N & (N-1)) ); // Ensure that N is a power of 2

	return ( x + (N-1) ) & (~(N-1));
}

static size_t
GetFileSize( const char *filepath )
{
	Rtt_ASSERT( filepath );

	struct stat statbuf;
	int result = stat( filepath, & statbuf );

	if (result != 0)
	{
		fprintf(stderr, "car: cannot stat file '%s'\n", filepath);
	}

	Rtt_UNUSED( result ); Rtt_ASSERT( 0 == result );

	return statbuf.st_size;
}

/*
#if defined( Rtt_DEBUG ) && !defined( Rtt_ANDROID_ENV )
static bool
Diff( const char *path1, const char *path2 )
{
	size_t len1 = GetFileSize( path1 );
	size_t len2 = GetFileSize( path2 );

	bool result = ( len1 == len2 );
	if ( result )
	{
#if defined( Rtt_WIN_DESKTOP_ENV ) || defined( Rtt_POWERVR_ENV )
		WinFile file1;
		WinFile file2;

		file1.Open( path1 );
		file1.Open( path2 );
		if ( !file1.IsOpen() || !file2.IsOpen() )
			return false;

		if ( file1.GetFileSize() != file2.GetFileSize() )
			return false;

		result = ( 0 == memcmp( file1.GetContents(), file2.GetContents(), file1.GetFileSize() ) );
#elif defined( Rtt_WIN_PHONE_ENV )
		//TODO: To be implemented later...
		Rtt_ASSERT_NOT_IMPLEMENTED();
#else
		int fd1 = open( path1, O_RDONLY );
		void *p1 = mmap( NULL, len1, PROT_READ, MAP_SHARED, fd1, 0 );

		int fd2 = open( path2, O_RDONLY );
		void *p2 = mmap( NULL, len2, PROT_READ, MAP_SHARED, fd2, 0 );

		result = ( 0 == memcmp( p1, p2, len1 ) );

		munmap( p2, len2 );
		close( fd2 );

		munmap( p1, len1 );
		close( fd1 );
#endif
	}

	return result;
}
#endif
*/

static char
DirSeparator()
{
#if defined( Rtt_WIN_ENV ) || defined( Rtt_POWERVR_ENV )
	return '\\';
#else
	return '/';
#endif
}

static const char*
GetBasename( const char* path )
{
    const char kDirSeparator = DirSeparator();

    const char* result = path;
    for ( path = strchr( path, kDirSeparator );
          path && '\0' != *path;
          path = strchr( path, kDirSeparator ) )
    {
         ++path;
         result = path;
    }

    return result;
}

// ----------------------------------------------------------------------------

struct ArchiveWriterEntry
{
	U32 type;
	U32 offset;
	const char* name;
	size_t nameLen;
	const char* srcPath;
	size_t srcLen;
};

class ArchiveWriter
{
	public:
		enum
		{
			kTagSize = sizeof(U32)*2,
			kVersion = kArchiveDefaultVersion
		};

	public:
		ArchiveWriter();
		~ArchiveWriter();

	public:
		int Initialize( const char *dstPath, U8 version, U32 flags, U32 xorSeed );

	public:
		int Serialize( Archive::Tag tag, U32 len ) const;
		int Serialize( U32 value ) const;
		int Serialize( const char *value, size_t len ) const;
		int Serialize( const char *filepath ) const;

	public:
//		int Serialize( ArchiveWriterEntry& entry );

	public:
		S32 GetPosition() const;
		bool IsXorEnabled() const { return kArchiveVersion2 == fVersion && ( fFlags & kArchiveFlagXorData ); }
		U32 GetXorSeed() const { return fXorSeed; }

	private:
		FILE *fDst;
		U8 fVersion;
		U32 fFlags;
		U32 fXorSeed;
};

// ----------------------------------------------------------------------------

ArchiveWriter::ArchiveWriter()
:	fDst( NULL ),
	fVersion( kArchiveDefaultVersion ),
	fFlags( 0 ),
	fXorSeed( 0 )
{
}

ArchiveWriter::~ArchiveWriter()
{
	if ( Rtt_VERIFY( fDst ) )
	{
		Rtt_FileClose( fDst );
	}
}

int
ArchiveWriter::Initialize( const char *dstPath, U8 version, U32 flags, U32 xorSeed )
{
	int result = 0;

	fDst = Rtt_FileOpen(dstPath, "wb");

	if (fDst == NULL)
	{
		fprintf(stderr, "car: cannot open archive '%s' for writing\n", dstPath);
	}
	else
	{
		fVersion = version;
		fFlags = ( kArchiveVersion2 == fVersion ? flags : 0 );
		fXorSeed = ( fFlags & kArchiveFlagXorData ? xorSeed : 0 );

		FILE *dst = fDst;
		result += fprintf( dst, "%c", 'r');
		result += fprintf( dst, "%c", 'a');
		result += fprintf( dst, "%c", 'c');
		result += fprintf( dst, "%c", fVersion );
		if ( kArchiveVersion2 == fVersion )
		{
			result += Serialize( fFlags );
			result += Serialize( ArchiveEncodeSeed( fXorSeed ) );
		}
	}

	return result;
}

int
ArchiveWriter::Serialize( Archive::Tag tag, U32 len ) const
{
	Rtt_ASSERT( fDst );

	int result = Serialize( tag );
	result += Serialize( len );
	return result;
}

int
ArchiveWriter::Serialize( U32 value ) const
{
	Rtt_ASSERT( fDst );
	return fprintf( fDst, "%c%c%c%c",
		(unsigned char)(value & 0xFF),
		(unsigned char)(value >> 8 & 0xFF),
		(unsigned char)(value >> 16 & 0xFF),
		(unsigned char)(value >> 24 & 0xFF) );
}

int
ArchiveWriter::Serialize( const char *value, size_t len ) const
{
	Rtt_ASSERT( fDst );

	FILE *dst = fDst;
	int result = Serialize( (U32) len );

	// size_t len4 = (len + 3) & (~0x3); // next multiple of 4 if len is not a multiple of 4

	++len; // increment to include space for '\0'-termination
	size_t len4 = GetByteAlignedValue< 4 >( len );
	for ( size_t i = 0; i < len; i++ )
	{
		result += fprintf( dst, "%c", value[i] );
	}

	// Pad 0's to 4-byte align
	for ( size_t i = len; i < len4; i++ )
	{
		result += fprintf( dst, "%c", 0 );
	}

	return result;
}

int
ArchiveWriter::Serialize( const char *filepath ) const
{
	Rtt_ASSERT( fDst );
	Rtt_ASSERT( filepath );

	int result = 0;

	FILE *src = Rtt_FileOpen( filepath, "rb" );

	if (src == NULL)
	{
		fprintf(stderr, "car: cannot serialize file '%s' (%s)\n", filepath, strerror(errno));

		return 0;
	}
	else
	{
		size_t len = GetFileSize( filepath );
		size_t len4 = GetByteAlignedValue< 4 >( len );

		FILE *dst = fDst;
		long numBytes = 0;
		U32 payloadOffset = (U32)ftell( dst );
		if ( Rtt_VERIFY( CopyFile( src, dst, & numBytes, GetXorSeed(), payloadOffset ) ) )
		{
			Rtt_ASSERT( numBytes >= 0 && (size_t)numBytes == len );

			size_t numChars = len4 - len;
			Rtt_ASSERT( numChars < 4 );
			switch( numChars )
			{
				case 3:
					fprintf( dst, "%c", '\0' );
				case 2:
					fprintf( dst, "%c", '\0' );
				case 1:
					fprintf( dst, "%c", '\0' );
				default:
					break;
			}

			result += len4;
		}
		else
		{
			result += numBytes;
		}

		Rtt_FileClose(src);
	}

	return result;
}

/*
int
ArchiveWriter::Serialize( ArchiveWriterEntry& entry )
{
}
*/

S32
ArchiveWriter::GetPosition() const
{
	Rtt_ASSERT( fDst );
	return (S32) ftell( fDst );
}

// ----------------------------------------------------------------------------

class ArchiveReader
{
	public:
		ArchiveReader();
//		~ArchiveReader();

		bool Initialize( const void* data, size_t numBytes );

	public:
		U32 ParseTag( U32& rLength );
		U32 ParseU32();
		const char* ParseString();
		void* ParseData( U32& rLength );

	public:
		bool Seek( S32 offset, bool fromOrigin );
		S32 Tell() const;
		U32 OffsetOf( const void *data ) const;
		U8 GetVersion() const { return fVersion; }
		U32 GetFlags() const { return fFlags; }
		U32 GetXorSeed() const { return fXorSeed; }
		bool IsXorEnabled() const { return kArchiveVersion2 == fVersion && ( fFlags & kArchiveFlagXorData ); }

	protected:
		void VerifyBounds() const;

	private:
		const void *fPos;
		const void *fData;
		size_t fDataLen;
		U8 fVersion;
		U32 fFlags;
		U32 fXorSeed;
};

ArchiveReader::ArchiveReader()
:	fPos( NULL ),
	fData( NULL ),
	fDataLen( 0 ),
	fVersion( 0 ),
	fFlags( 0 ),
	fXorSeed( 0 )
{
}

bool
ArchiveReader::Initialize( const void* data, size_t numBytes )
{
	const U8 *bytes = (const U8*)data;
	const size_t kBaseHeaderSize = 4;
	bool result = ( data && numBytes > kBaseHeaderSize
		&& bytes[0] == 'r'
		&& bytes[1] == 'a'
		&& bytes[2] == 'c'
		&& ( bytes[3] == kArchiveVersion1 || bytes[3] == kArchiveVersion2 ) );
	if ( result )
	{
		fVersion = bytes[3];
		size_t headerSize = kBaseHeaderSize;
		fFlags = 0;
		fXorSeed = 0;
		if ( kArchiveVersion2 == fVersion )
		{
			headerSize += sizeof( U32 ) * 2;
			result = ( numBytes > headerSize );
			if ( ! result )
			{
				return false;
			}

			U32 flags = ((U32)bytes[4])
					| (((U32)bytes[5]) << 8)
					| (((U32)bytes[6]) << 16)
					| (((U32)bytes[7]) << 24);
			U32 encodedSeed = ((U32)bytes[8])
					| (((U32)bytes[9]) << 8)
					| (((U32)bytes[10]) << 16)
					| (((U32)bytes[11]) << 24);
			fFlags = flags;
			fXorSeed = ArchiveDecodeSeed( encodedSeed );
		}

		fPos = ((U8*)data) + headerSize;
		fData = data;
		fDataLen = numBytes;

#if Rtt_DEBUG_ARCHIVE
		Rtt_TRACE( ( "[ArchiveReader::Initialize] inData(%p) fPos(%p) fData(%p) headerSize(%ld) fDataLen(%ld)\n",
			data, fPos, fData, headerSize, fDataLen ) );
#endif
	}
#if Rtt_DEBUG_ARCHIVE
	else
	{
		Rtt_TRACE( ( "[ArchiveReader::Initialize] header check failed: numBytes %ld, kHeaderSize %ld\n",
			numBytes, kHeaderSize ) );
	}
#endif

	return result;
}

static U32
ReadU32( U32 *p )
{
	#ifdef Rtt_LITTLE_ENDIAN
		return *p;
	#else
		U8 *pp = (U8*)p;
		return ((U32)pp[0])
				| (((U32)pp[1]) << 8)
				| (((U32)pp[2]) << 16)
				| (((U32)pp[3]) << 24);
	#endif
}

U32
ArchiveReader::ParseTag( U32& rLength )
{
	VerifyBounds();

	U32 *p = (U32*)fPos;

	U32 tag = ReadU32( p ); p++; // *p++;
	rLength = ReadU32( p ); p++; // *p++;
	fPos = p;

	VerifyBounds();
	return tag;
}

U32
ArchiveReader::ParseU32()
{
	VerifyBounds();

	U32 *p = (U32*)fPos;

	U32 result = ReadU32( p ); p++; // *p++;
	fPos = p;

	VerifyBounds();
	return result;
}

const char*
ArchiveReader::ParseString()
{
	VerifyBounds();

	U32 *p = (U32*)fPos;

	U32 len = ReadU32( p ); p++; // *p++;
	const char* result = reinterpret_cast< char* >( p );
	Rtt_ASSERT( strlen( result ) == len );

	// 4-byte alignment was calculated using string size (which includes '\0' termination byte)
	fPos = p + GetByteAlignedValue< 4 >( len + 1 ) / sizeof( *p );

	VerifyBounds();
	return result;
}

void*
ArchiveReader::ParseData( U32& rLength )
{
	VerifyBounds();

	U32 *p = (U32*)fPos;

	U32 len = ReadU32( p ); p++; // *p++;
	rLength = len;
	void* result = p;

	fPos = p + GetByteAlignedValue< 4 >( len ) / sizeof( *p );

	VerifyBounds();
	return result;
}

bool
ArchiveReader::Seek( S32 offset, bool fromOrigin )
{
	VerifyBounds();

	bool result = false;

	if ( fromOrigin )
	{
		result = ( offset >= 0 );
		if ( result )
		{
			fPos = ((U8*)fData) + offset;
		}
	}
	else
	{
		void *p = ((U8*)fPos) + offset;
		void *pEnd = ((U8*)fData) + fDataLen;
		result = ( p >= fData && p < pEnd );
		if ( result )
		{
			fPos = p;
		}
	}

	VerifyBounds();
	return result;
}

S32
ArchiveReader::Tell() const
{
	VerifyBounds();
	return (S32)( (const U8*)fPos - (const U8*)fData );
}

U32
ArchiveReader::OffsetOf( const void *data ) const
{
	const U8 *p = (const U8*)data;
	Rtt_ASSERT( p >= (const U8*)fData && p <= (((U8*)fData) + fDataLen ) );
	return (U32)( (const U8*)data - (const U8*)fData );
}

void
ArchiveReader::VerifyBounds() const
{
	// fprintf(stderr, "fPos %p, fData %p, fDataLen 0x%lx (%d)\n", fPos, fData, fDataLen, fDataLen);
	Rtt_ASSERT( fPos >= fData && fPos < (((U8*)fData) + fDataLen ) );
}

// ----------------------------------------------------------------------------

void
Archive::Serialize( const char *dstPath, int numSrcPaths, const char *srcPaths[] )
{
	Serialize(
		dstPath,
		numSrcPaths,
		srcPaths,
		GetArchiveXorKeyFromEnvironment(),
		! IsArchiveXorDisabledByEnvironment() );
}

void
Archive::Serialize( const char *dstPath, int numSrcPaths, const char *srcPaths[], const char *xorKey, bool useXor )
{
	std::vector<std::string> fileList;
	size_t fileCount = 0;

#ifdef WIN32
	char tmpDirTemplate[_MAX_PATH];
	const char* tmp = getenv("TMP");
	if (tmp == NULL)
	{
		tmp = getenv("TEMP");
	}

	if (tmp)
	{
		_snprintf(tmpDirTemplate, sizeof(tmpDirTemplate), "%s\\CBXXXXXX", tmp);
	}
	else
	{
		strcpy(tmpDirTemplate, "\\tmp\\CBXXXXXX");
	}
#else
	char tmpDirTemplate[] = "/tmp/CBXXXXXX";
#endif

	const char *tmpDirName = Rtt_MakeTempDirectory(tmpDirTemplate);

	if (Rtt_FileExists(dstPath))
	{
		// Archive already exists, extract the current contents so we can overwrite
		// old files with fresh copies and add any new ones (this is done to avoid
		// implementing actual archive editing)

		// First extract any files already in the archive to preserve them
		Deserialize(tmpDirName, dstPath);

		// Copy all the new files to the temporary directory (overwriting any with the same
		// name that might have been extracted to preserve the semantics of car files)
		for ( int i = 0; i < numSrcPaths; i++ )
		{
			String tmpFileCopy(tmpDirName);
			tmpFileCopy.AppendPathComponent(GetBasename(srcPaths[i]));

			if ( ! Rtt_CopyFile(srcPaths[i], tmpFileCopy.GetString()))
			{
				fprintf(stderr, "car: cannot open '%s' for reading\n", srcPaths[i]);

				return;
			}
		}

		// Enumerate the files now in the temporary directory and make the new archive with them
		fileList = Rtt_ListFiles(tmpDirName);
		fileCount = fileList.size();
	}
	else
	{
		// Archive doesn't already exist so we can't be updating it,
		// just copy the file paths to our file list

		for ( int i = 0; i < numSrcPaths; i++ )
		{
			fileList.push_back(srcPaths[i]);
		}

		fileCount = numSrcPaths;
	}

	ArchiveWriter writer;
	U8 archiveVersion = useXor ? kArchiveVersion2 : kArchiveVersion1;
	U32 archiveFlags = useXor ? kArchiveFlagXorData : 0;
	U32 xorSeed = useXor ? CreateArchiveXorSeed( dstPath, xorKey, fileList ) : 0;
	int startPos = writer.Initialize( dstPath, archiveVersion, archiveFlags, xorSeed );
	if ( Rtt_VERIFY( startPos > 0 ) )
	{
		ArchiveWriterEntry* entries = new ArchiveWriterEntry[fileCount];

		U32 contentsLen = sizeof(U32); // numElements
		size_t entryIdx = 0;
		for (std::vector<std::string>::iterator it = fileList.begin(); it != fileList.end(); ++it)
		{
			ArchiveWriterEntry& entry = entries[entryIdx++];
			entry.type = kLuaObjectResource;
			entry.offset = 0;

			const char* path = it->c_str();
			entry.name = GetBasename( path );
			entry.nameLen = strlen( entry.name );
			entry.srcPath = path;
			entry.srcLen = GetFileSize( path );

			// type, offset, numChars, string data
			contentsLen += 3*sizeof(U32) + GetByteAlignedValue< 4 >( entry.nameLen + 1 );
		}

		U32 offsetBase = startPos + contentsLen;

		offsetBase += writer.Serialize( Archive::kContentsTag, contentsLen );

		// Contents
		// --------------------------
		//   U32        numElements
		//   Record[]   {
		//                U32 type
		//                U32 offset
		//                String name
		//              }
		// 
		// String
		// --------------------------
		//   U32        length
		//   U8[]	    bytes (4 byte-aligned padding)
		writer.Serialize( (int)fileCount );
		for ( size_t i = 0; i < fileCount; i++ )
		{
			ArchiveWriterEntry& entry = entries[i];
			writer.Serialize( entry.type );
			writer.Serialize( offsetBase );
			writer.Serialize( entry.name, entry.nameLen );

			// store offset for this entry
			entry.offset = offsetBase;

			// For next offset, add srcLen *and* bytes for tag, length 
			offsetBase +=
				GetByteAlignedValue< 4 >( entry.srcLen )
				+ ArchiveWriter::kTagSize
				+ sizeof(U32);
		}

		// Data
		// --------------------------
		//   String     data
		for ( size_t i = 0; i < fileCount; i++ )
		{
			ArchiveWriterEntry& entry = entries[i];

			Rtt_ASSERT(
				writer.GetPosition() >= 0
				&& (size_t)writer.GetPosition() == entry.offset );

			// data tag length = sizeof( length ) + byte-aligned len of bytes buffer
			writer.Serialize( kDataTag, sizeof( U32 ) + (U32) GetByteAlignedValue< 4 >( entry.srcLen ) );
			writer.Serialize( (U32) entry.srcLen );
			writer.Serialize( entry.srcPath );
		}

		// EOF
		writer.Serialize( kEOFTag, 0 );

		delete [] entries;
	}

	Rtt_DeleteDirectory(tmpDirName);
}

static void
WriteFile( const char *dstDir, const char *filename, const void *src, size_t srcNumBytes )
{
	Rtt_ASSERT( dstDir );
	Rtt_ASSERT( filename );
	Rtt_ASSERT( src );

	String path(dstDir);
	path.AppendPathComponent(filename);

	int fd = Rtt_FileDescriptorOpen( path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );

	if (fd == -1)
	{
		fprintf(stderr, "car: cannot open '%s' for writing\n", path.GetString());

		return;
	}
	else
	{
		if ( 0 == srcNumBytes )
		{
			Rtt_FileDescriptorClose( fd );
			return;
		}

		// Set size of file
		_lseek( fd, srcNumBytes - 1, SEEK_SET );
		_write( fd, "", 1 );

		int canWrite = 1;
		void *dst = Rtt_FileMemoryMap( fd, 0, srcNumBytes, canWrite );
		if (dst == NULL)
		{
			fprintf(stderr, "car: cannot map '%s' for writing\n", path.GetString());

			return;
		}
		else
		{
			*(U32*)dst = 0xdeadbeef;
			memcpy( dst, src, srcNumBytes );
			Rtt_FileMemoryUnmap( dst, srcNumBytes );
		}

		Rtt_FileDescriptorClose( fd );
	}
}

size_t
Archive::Deserialize( const char *dstDir, const char *srcCarFile )
{
	int fd = Rtt_FileDescriptorOpen(srcCarFile, O_RDONLY, S_IRUSR);
	struct stat statbuf;
	size_t count = 0;

	if (fd == -1)
	{
		fprintf(stderr, "car: cannot open archive '%s'\n", srcCarFile);

		return 0;
	}

	if (fstat( fd, & statbuf ) == -1)
	{
		fprintf(stderr, "car: cannot stat archive '%s'\n", srcCarFile);

		return 0;
	}

	size_t dataLen = statbuf.st_size;

	void *data = Rtt_FileMemoryMap(fd, 0, dataLen, false);

	Rtt_FileDescriptorClose(fd);

	ArchiveReader reader;
	if (reader.Initialize(data, dataLen) == 0)
	{
		fprintf(stderr, "car: file '%s' is not a car archive\n", srcCarFile);
	}
	else
	{
		U32 tag = kUnknownTag;
		//while( kEOFTag != tag )
		{
			U32 tagLen;
			tag = reader.ParseTag( tagLen );
			switch( tag )
			{
				case kContentsTag:
					{
						U32 numElements = reader.ParseU32();
						ArchiveEntry *entries = new ArchiveEntry[numElements];
						for ( U32 i = 0; i < numElements; i++ )
						{
							ArchiveEntry& entry = entries[i];
							entry.type = reader.ParseU32();
							entry.offset = reader.ParseU32();
							entry.name = reader.ParseString();
						}

						for ( U32 i = 0; i < numElements; i++ )
						{
							ArchiveEntry& entry = entries[i];

							reader.Seek( entry.offset, true );
							U32 tagLen;
							U32 tag = reader.ParseTag( tagLen );
							if ( Rtt_VERIFY( Archive::kDataTag == tag ) )
							{
								U32 resourceLen = 0;
								void* resource = reader.ParseData( resourceLen );
								char *decodedResource = NULL;
								const void *resourceToWrite = resource;
								if ( reader.IsXorEnabled() )
								{
									decodedResource = new char[resourceLen];
									memcpy( decodedResource, resource, resourceLen );
									ArchiveXorTransform(
										decodedResource,
										resourceLen,
										reader.GetXorSeed(),
										reader.OffsetOf( resource ) );
									resourceToWrite = decodedResource;
								}
								WriteFile( dstDir, entry.name, resourceToWrite, resourceLen );
								delete [] decodedResource;
								++count;
							}
						}

						delete [] entries;
					}
					break;
				default:
					Rtt_ASSERT_NOT_REACHED();
					break;
			}
		}
	}

	if (data != NULL)
	{
		Rtt_FileMemoryUnmap(data, dataLen);
	}

	return count;
}

void
Archive::List(const char *srcCarFile)
{
	int fd = Rtt_FileDescriptorOpen(srcCarFile, O_RDONLY, S_IRUSR);
	struct stat statbuf;

	if (fd == -1)
	{
		fprintf(stderr, "car: cannot open archive '%s'\n", srcCarFile);

		return;
	}

	if (fstat( fd, & statbuf ) == -1)
	{
		fprintf(stderr, "car: cannot stat archive '%s'\n", srcCarFile);

		return;
	}
	size_t dataLen = statbuf.st_size;

	void *data = Rtt_FileMemoryMap(fd, 0, dataLen, false);

	Rtt_FileDescriptorClose(fd);

	ArchiveReader reader;
	if (reader.Initialize(data, dataLen) == 0)
	{
		fprintf(stderr, "car: file '%s' is not a car archive\n", srcCarFile);
	}
	else
	{
		U32 tag = kUnknownTag;
		//while( kEOFTag != tag )
		{
			U32 tagLen;
			tag = reader.ParseTag( tagLen );
			switch( tag )
			{
			case kContentsTag:
			{
				U32 numElements = reader.ParseU32();
				ArchiveEntry *entries = new ArchiveEntry[numElements];
				for ( U32 i = 0; i < numElements; i++ )
				{
					ArchiveEntry& entry = entries[i];
					entry.type = reader.ParseU32();
					entry.offset = reader.ParseU32();
					entry.name = reader.ParseString();
				}

				for ( U32 i = 0; i < numElements; i++ )
				{
					ArchiveEntry& entry = entries[i];

					reader.Seek( entry.offset, true );
					U32 tagLen;
					U32 tag = reader.ParseTag( tagLen );
					if ( Rtt_VERIFY( Archive::kDataTag == tag ) )
					{
						U32 resourceLen = 0;
						reader.ParseData( resourceLen );
						printf("%7d %s\n", resourceLen, entry.name);
					}
				}

				delete [] entries;
			}
			break;
			default:
				Rtt_ASSERT_NOT_REACHED();
				break;
			}
		}
	}

	if (data != NULL)
	{
		Rtt_FileMemoryUnmap(data, dataLen);
	}
}

// ----------------------------------------------------------------------------

#if !defined( Rtt_NO_ARCHIVE )

// ----------------------------------------------------------------------------

Archive::Archive( Rtt_Allocator& allocator, const char *srcPath )
:	fAllocator( allocator ),
	fEntries( NULL ),
	fNumEntries( 0 ),
	fData( NULL ),
	fDataLen( 0 ),
	fPath( NULL ),
	fVersion( 0 ),
	fFlags( 0 ),
	fXorSeed( 0 )
#if defined( Rtt_ARCHIVE_COPY_DATA )
	, fBits( &allocator )
#endif
{
	if ( srcPath )
	{
		size_t pathLen = strlen( srcPath ) + 1;
		fPath = (char*)Rtt_MALLOC( & allocator, pathLen );
		if ( fPath )
		{
			memcpy( fPath, srcPath, pathLen );
		}
	}

#if defined( Rtt_WIN_PHONE_ENV ) || defined(Rtt_NXS_ENV)
	FILE* filePointer = Rtt_FileOpen(srcPath, "rb");
	if (filePointer)
	{
		fseek(filePointer, 0, SEEK_END);
		fDataLen = ftell(filePointer);
		if (fDataLen > 0)
		{
			const size_t MAX_BYTES_PER_READ = 1024;
			rewind(filePointer);
			fData = Rtt_MALLOC(&allocator, fDataLen);
			for (long totalBytesRead = 0; totalBytesRead < fDataLen;)
			{
				size_t bytesRead = fread(((U8*)fData + totalBytesRead), 1, MAX_BYTES_PER_READ, filePointer);
				if (bytesRead < MAX_BYTES_PER_READ)
				{
					int errorNumber = errno;
					if (ferror(filePointer) && errorNumber)
					{
						Rtt_FREE((void*)fData);
						fData = NULL;
						fDataLen = 0;
						Rtt_LogException(strerror(errorNumber));
						break;
					}
				}
				totalBytesRead += (long)bytesRead;
				if (feof(filePointer))
				{
					fDataLen = totalBytesRead;
					break;
				}
			}
		}
		Rtt_FileClose(filePointer);
	}
#else
	int fileDescriptor = Rtt_FileDescriptorOpen(srcPath, O_RDONLY, S_IRUSR);
	struct stat statbuf;
	int result = fstat(fileDescriptor, &statbuf); Rtt_ASSERT(result >= 0);

	int canWrite = 0;
	fData = Rtt_FileMemoryMap(fileDescriptor, 0, statbuf.st_size, canWrite);

	fDataLen = statbuf.st_size;
	Rtt_FileDescriptorClose(fileDescriptor);
#endif

#if defined( EMSCRIPTEN )
	// On browser, mmap is not reliable wrt byte-alignment,
	// so ensure byte-alignment by copying data to a malloc'd buffer
	fBits.Set( (const char *)fData, fDataLen );
	munmap( (void *)fData, fDataLen );
	fData = fBits.Get();
#endif


#if Rtt_DEBUG_ARCHIVE

	Rtt_TRACE( ( "[Archive] fDataLen(%ld)\n", fDataLen ) );
	const U8 *bytes = (const U8 *)fData;
	for ( size_t i = 0; i < fDataLen; i++ )
	{
		if ( i % 16 == 0 )
		{
			if ( i > 0 )
			{
				Rtt_TRACE( ( "\n" ) );
			}
			Rtt_TRACE( ( "%08lx  ", i ) );
		}

		Rtt_TRACE( ( "%02x ", bytes[i] ) );

		if ( i > 0 && (i+1) % 8 == 0 )
		{
			Rtt_TRACE( ( " " ) );
		}
	}
	Rtt_TRACE( ( "\n" ) );
#endif

	ArchiveReader reader;
	if ( Rtt_VERIFY( reader.Initialize( fData, fDataLen ) ) )
	{
		fVersion = reader.GetVersion();
		fFlags = reader.GetFlags();
		fXorSeed = reader.GetXorSeed();

		U32 tag = kUnknownTag;
		//while( kEOFTag != tag )
		{
			U32 tagLen;
			tag = reader.ParseTag( tagLen );
			switch( tag )
			{
				case kContentsTag:
					{
						U32 numElements = reader.ParseU32();
						fEntries = (ArchiveEntry*)Rtt_MALLOC( & allocator, sizeof( ArchiveEntry )*numElements );
						fNumEntries = numElements;
#if Rtt_DEBUG_ARCHIVE
						Rtt_TRACE( ( "[Archive::Archive] fNumEntries %ld, fEntries %p\n", fNumEntries, fEntries ) );
#endif

						for ( U32 i = 0; i < numElements; i++ )
						{
							ArchiveEntry& entry = fEntries[i];
							entry.type = reader.ParseU32();
							entry.offset = reader.ParseU32();
							entry.name = reader.ParseString();
						}
					}
					break;
				default:
					Rtt_TRACE( ( "Unknown tag: %d\n", tag ) );
					Rtt_ASSERT_NOT_REACHED();
					break;
			}
		}
	}
}

Archive::~Archive()
{
#if defined( Rtt_EMSCRIPTEN_ENV ) || defined( Rtt_NXS_ENV )
	// Do nothing.
#elif defined( Rtt_WIN_PHONE_ENV )
	Rtt_FREE((void*)fData);
#else
	if ( Rtt_VERIFY( fData ) )
	{
#if defined( Rtt_WIN_DESKTOP_ENV ) || defined( Rtt_POWERVR_ENV )
		Rtt_FileMemoryUnmap( fData, fDataLen );
#else
//TODO: Merge this code with the Windows code block up above using the new Rtt_File*() functions.
		munmap( (void*)fData, fDataLen );
#endif
	}
#endif

	Rtt_FREE( fEntries );
	Rtt_FREE( fPath );

}

// This will be added to the list of Lua loaders called via "require"
int
Archive::ResourceLoader( lua_State *L )
{
	const char *name = luaL_checkstring(L, 1);

	Runtime* runtime = LuaContext::GetRuntime( L );

	Archive* archive = runtime->GetArchive();

	const char kExtension[] = "." Rtt_LUA_OBJECT_FILE_EXTENSION;
	const size_t nameLen = strlen( name );
	const size_t filenameByteLength = nameLen + sizeof( kExtension );
	char *filename = (char*)malloc( filenameByteLength );
	snprintf( filename, filenameByteLength, "%s%s", name, kExtension );

	// Actually, we shouldn't throw an error, so ignore the status.
	// As long as we push an error string, ll_require will take care of
	// appending the strings and throw an error if all loaders fail to
	// find the module.
	int status = archive->LoadResource( L, filename ); Rtt_UNUSED( status );

	free( filename );

/*
	// Should not throw an error b/c the module could be part of the Corona preloaded libs.
	if ( 0 != status )
	{
		luaL_error(L, "error loading module " LUA_QS ":\n\t%s", name, lua_tostring(L, -1));
	}
*/

	return 1;
}

struct ArchiveLuaStream
{
	FILE *file;
	U32 seed;
	U32 absoluteOffset;
	U32 remaining;
	U32 position;
	char buffer[4096];
	bool failed;
};

static const char*
ArchiveLuaStreamReader( lua_State *L, void *data, size_t *size )
{
	Rtt_UNUSED( L );

	ArchiveLuaStream *stream = (ArchiveLuaStream*)data;
	if ( ! stream || stream->failed || 0 == stream->remaining )
	{
		*size = 0;
		return NULL;
	}

	size_t bytesToRead = stream->remaining < sizeof( stream->buffer )
		? stream->remaining
		: sizeof( stream->buffer );
	size_t bytesRead = fread( stream->buffer, 1, bytesToRead, stream->file );
	if ( bytesRead == 0 )
	{
		stream->failed = true;
		*size = 0;
		return NULL;
	}

	ArchiveXorTransform(
		stream->buffer,
		bytesRead,
		stream->seed,
		stream->absoluteOffset + stream->position );

	stream->position += (U32)bytesRead;
	stream->remaining -= (U32)bytesRead;
	*size = bytesRead;
	return stream->buffer;
}

static int
LoadLuaBufferXor(
	lua_State *L,
	Rtt_Allocator& allocator,
	const void *resource,
	U32 resourceLen,
	U32 seed,
	U32 absoluteOffset,
	const char *name )
{
	size_t allocSize = resourceLen > 0 ? resourceLen : 1;
	char *buffer = (char*)Rtt_MALLOC( & allocator, allocSize );
	if ( ! buffer )
	{
		lua_pushfstring( L, "archive is corrupted. could not allocate resource (%s)", name );
		return LUA_ERRMEM;
	}

	memcpy( buffer, resource, resourceLen );
	ArchiveXorTransform( buffer, resourceLen, seed, absoluteOffset );
	int status = luaL_loadbuffer( L, buffer, resourceLen, name );
	Rtt_FREE( buffer );
	return status;
}

static int
LoadLuaStreamXor(
	lua_State *L,
	const char *archivePath,
	U32 resourceOffset,
	U32 resourceLen,
	U32 seed,
	const char *name )
{
	FILE *file = archivePath ? Rtt_FileOpen( archivePath, "rb" ) : NULL;
	if ( ! file )
	{
		lua_pushfstring( L, "archive is corrupted. could not open archive for resource (%s)", name );
		return LUA_ERRFILE;
	}

	int status = 0;
	if ( 0 != fseek( file, resourceOffset, SEEK_SET ) )
	{
		lua_pushfstring( L, "archive is corrupted. could not seek resource (%s)", name );
		status = LUA_ERRFILE;
	}
	else
	{
		ArchiveLuaStream stream;
		stream.file = file;
		stream.seed = seed;
		stream.absoluteOffset = resourceOffset;
		stream.remaining = resourceLen;
		stream.position = 0;
		stream.failed = false;

		status = lua_load( L, ArchiveLuaStreamReader, & stream, name );
		if ( 0 == status && stream.failed )
		{
			lua_pop( L, 1 );
			lua_pushfstring( L, "archive is corrupted. could not read resource (%s)", name );
			status = LUA_ERRFILE;
		}
	}

	Rtt_FileClose( file );
	return status;
}

int
Archive::LoadResource( lua_State *L, const char *name )
{
	int status = LUA_ERRFILE;

	const char kFormatResourceNotFound[] = "resource (%s) does not exist in archive";
	const char kFormatAchiveCorrupted[] = "archive is corrupted. could not resolve resource (%s)";
	const char *errorFormat = kFormatResourceNotFound;

	ArchiveReader reader;

	if ( fData == NULL )
		goto exit_gracefully;

	reader.Initialize( fData, fDataLen );

	for ( size_t i = 0, iMax = fNumEntries; i < iMax; i++ )
	{
		ArchiveEntry& entry = fEntries[i];
		if ( 0 == Rtt_StringCompare( entry.name, name ) )
		{
			reader.Seek( entry.offset, true );
			U32 tagLen;
			U32 tag = reader.ParseTag( tagLen );
			if ( Rtt_VERIFY( Archive::kDataTag == tag ) )
			{
				U32 resourceLen = 0;
				void* resource = reader.ParseData( resourceLen );
				if ( reader.IsXorEnabled() )
				{
					U32 resourceOffset = reader.OffsetOf( resource );
					if ( fPath )
					{
						int stackTop = lua_gettop( L );
						status = LoadLuaStreamXor( L, fPath, resourceOffset, resourceLen, reader.GetXorSeed(), name );
						if ( LUA_ERRFILE == status )
						{
							lua_settop( L, stackTop );
							status = LoadLuaBufferXor( L, fAllocator, resource, resourceLen, reader.GetXorSeed(), resourceOffset, name );
						}
					}
					else
					{
						status = LoadLuaBufferXor( L, fAllocator, resource, resourceLen, reader.GetXorSeed(), resourceOffset, name );
					}
				}
				else
				{
					status = luaL_loadbuffer( L, static_cast< const char* >( resource ), resourceLen, name );
				}
				goto exit_gracefully;
			}
			errorFormat = kFormatAchiveCorrupted;
		}
	}

#if defined( Rtt_DEBUG ) && defined( Rtt_ANDROID_ENV )
	Rtt_Log( errorFormat, name );
#endif

	lua_pushfstring( L, errorFormat, name );

exit_gracefully:
	return status;
}

int
Archive::DoResource( lua_State *L, const char *name, int narg )
{
#if Rtt_DEBUG_ARCHIVE
	Rtt_TRACE( ( "[Archive::DoResource] name(%s)\n", name ) );
#endif
	int status = LoadResource( L, name );

	if ( 0 == status )
	{
		int base = lua_gettop( L ) - narg;
		lua_insert( L, base ); // move chunk underneath args

		status = LuaContext::DoCall( L, narg, 0 );
#ifdef Rtt_DEBUG
		if ( status && LuaContext::HasRuntime( L ) )
		{
			// We should only assert if the call failed on a Lua state associated with a Corona runtime.
			// Note: Mac and Win32 desktop apps need to load a "resource.car" before creating a Corona runtime
			//       in order to read its archived "build.settings" to pre-configure the desktop window.
			Rtt_ASSERT( 0 );
		}
#endif
	}

	return status;
}

// ----------------------------------------------------------------------------

#endif // Rtt_NO_ARCHIVE

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
