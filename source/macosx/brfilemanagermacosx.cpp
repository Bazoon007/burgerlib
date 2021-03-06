/***************************************

	MacOS version

	Copyright (c) 1995-2016 by Rebecca Ann Heineman <becky@burgerbecky.com>

	It is released under an MIT Open Source license. Please see LICENSE
	for license details. Yes, you can use it in a
	commercial title without paying anything, just give me a credit.
	Please? It's not like I'm asking you for money!
	
***************************************/

#include "brfilemanager.h"

#if defined(BURGER_MACOSX) || defined(DOXYGEN)
#include "brfile.h"
#include "brstringfunctions.h"
#include "brstring.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/attr.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <unistd.h>
#include <crt_externs.h>
#include <string.h>
#include <mach-o/dyld.h>

#include <AvailabilityMacros.h>
#if defined(BURGER_METROWERKS)
#include <CarbonCore/Files.h>
#include <CarbonCore/Folders.h>
#else
#include <Carbon/Carbon.h>
#endif
#include <Foundation/NSString.h>
#include <Foundation/NSFileManager.h>
#include <Foundation/NSAutoreleasePool.h>
#undef Free

/***************************************

	Given a drive number, return in generic format
	the drive's name.

***************************************/

Word BURGER_API Burger::FileManager::GetVolumeName(Burger::Filename *pOutput,Word uVolumeNum)
{
	Word uResult = File::OUTOFRANGE;
	int fp = open("/Volumes",O_RDONLY,0);
	if (fp!=-1) {
		int eError;
		Word bScore = FALSE;
		Word bFoundRoot = FALSE;
		Word uEntry = 1;
		do {
			// Structure declaration of data coming from getdirentriesattr()
			struct FInfoAttrBuf {
				u_int32_t length;				// Length of this data structure
				attrreference_t name;		// Offset for the filename
				fsobj_type_t objType;		// VREG for file, VREG for directory
				char m_Name[256*4];
			};

			// Attributes requested
			attrlist AttributesList;
			// Buffer to hold the attributes and the filename
			FInfoAttrBuf Entry;

			// Initialize the attributes list
			MemoryClear(&AttributesList,sizeof(AttributesList));
			// "sizeof" for the structure
			AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;
			// Let's get the name, type of file, creation time, modification time, finder information and hidden/locked flags

			// Note: If these flags are changed, the FInfoAttrBuf MUST be
			// adjusted to reflect the request or weird stuff will happen
			AttributesList.commonattr = ATTR_CMN_NAME | ATTR_CMN_OBJTYPE;
		
		// For some dumb reason, SDK 10.5 insists this is declared unsigned int on 64 bit CPUs
#if defined(BURGER_64BITCPU)
			unsigned int uCount = 1;
			unsigned int uJunkBase;
			unsigned int uNewState;
#else
			unsigned long uCount = 1;			// Load only a single directory entry
			unsigned long uJunkBase;
			unsigned long uNewState;
#endif

			// Get the directory entry
			eError = getdirentriesattr(fp,&AttributesList,&Entry,sizeof(Entry),&uCount,&uJunkBase,&uNewState,0);

			// No errors and an entry was returned?
			// Note: eError is 0 if more data is pending, 1 if this is the last entry.
			// uCount is zero when no entry is loaded

			if (eError>=0 && uCount) {

				// Get the pointer to the volume name
				char *pName = (reinterpret_cast<char *>(&Entry.name)+Entry.name.attr_dataoffset);

				// Special case for the root volume, it's a special link

				if (!bFoundRoot && Entry.objType==VLNK) {

					// Read in the link to see if it's pointing to the home folder

					char LinkBuffer[128];
					String Linkname("/Volumes/",pName);
					ssize_t uLinkDataSize = readlink(Linkname.GetPtr(),LinkBuffer,sizeof(LinkBuffer));
					if (uLinkDataSize==1 && LinkBuffer[0]=='/') {

						// This is the boot volume
						bFoundRoot = TRUE;
						// Is the user looking for the boot volume?
						if (!uVolumeNum) {
							bScore=TRUE;
						}
					} else {
						// Pretend it's a normal mounted volume
						Entry.objType = VDIR;
					}
				}

				// Normal volume (Enumate them)
				if (Entry.objType==VDIR) {
					if (uVolumeNum==uEntry) {
						bScore=TRUE;
					}
					++uEntry;
				}

				// Matched a volume!

				if (bScore) {
					--pName;
					// Insert a starting and ending colon
					pName[0] = ':';
					WordPtr uIndex = StringLength(pName);
					pName[uIndex] = ':';
					pName[uIndex+1] = 0;
					// Set the filename
					pOutput->Set(pName);
					// Exit okay!
					uResult = File::OKAY;
					break;
				}
			}
		} while (eError==0);
		// Close the directory
		close(fp);
	}

	// Clear on error
	if (uResult!=File::OKAY) {
		// Kill the string since I have an error
		pOutput->Clear();
	}
	return uResult;
}

/***************************************

	Set the initial default prefixs for a power up state
	*: = Boot volume
	$: = System folder
	@: = Prefs folder
	8: = Default directory
	9: = Application directory

***************************************/

void BURGER_API Burger::FileManager::DefaultPrefixes(void)
{
	Filename MyFilename;
	Word uResult = GetVolumeName(&MyFilename,0);		// Get the boot volume name
	if (uResult==File::OKAY) {
		// Set the initial prefix
		const char *pBootName = MyFilename.GetPtr();
		SetPrefix(PREFIXBOOT,pBootName);
		Free(g_pFileManager->m_pBootName);
		WordPtr uMax = StringLength(pBootName);
		g_pFileManager->m_uBootNameSize = static_cast<Word>(uMax);
		g_pFileManager->m_pBootName = StringDuplicate(pBootName);
	}
	
	char *pTemp = getcwd(NULL,0);					// This covers all versions
	if (pTemp) {
		MyFilename.SetFromNative(pTemp);
		SetPrefix(PREFIXCURRENT,MyFilename.GetPtr());		// Set the standard work prefix
		free(pTemp);
	}

	// Get the location of the application binary
	char NameBuffer[2048];
	uint32_t uSize = static_cast<uint32_t>(sizeof(NameBuffer));
	int iTest = _NSGetExecutablePath(NameBuffer,&uSize);
	if (!iTest) {
		MyFilename.SetFromNative(NameBuffer);
		MyFilename.DirName();
		SetPrefix(PREFIXAPPLICATION,MyFilename.GetPtr());		// Set the standard work prefix
	}

	FSRef MyRef;
	if (!FSFindFolder(kOnSystemDisk,kSystemFolderType,kDontCreateFolder,&MyRef)) {
		if (!FSRefMakePath(&MyRef,reinterpret_cast<Word8 *>(NameBuffer),static_cast<UInt32>(sizeof(NameBuffer)))) {
			MyFilename.SetFromNative(NameBuffer);
			// Set the standard work prefix
			SetPrefix(PREFIXSYSTEM,MyFilename.GetPtr());		
		}
	}
	
	if (!FSFindFolder(kOnSystemDisk,kPreferencesFolderType,kDontCreateFolder,&MyRef)) {
		if (!FSRefMakePath(&MyRef,reinterpret_cast<Word8 *>(NameBuffer),static_cast<UInt32>(sizeof(NameBuffer)))) {
			MyFilename.SetFromNative(NameBuffer);
			// Set the standard work prefix
			SetPrefix(PREFIXPREFS,MyFilename.GetPtr());		
		}
	}
}

/***************************************

	This routine will get the time and date
	from a file.
	Note, this routine is Operating system specific!!!

***************************************/

Word BURGER_API Burger::FileManager::GetModificationTime(Burger::Filename *pFileName,Burger::TimeDate_t *pOutput)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		struct timespec m_ModificationDate;		// Creation date
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_MODTIME;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		pOutput->Clear();
		uResult = File::FILENOTFOUND;
	} else {
		// Get the file dates
		pOutput->Load(&Entry.m_ModificationDate);
		// It's parsed!
		uResult = File::OKAY;
	}
	return uResult;
}

/***************************************

	This routine will get the time and date
	from a file.
	Note, this routine is Operating system specific!!!

***************************************/

Word BURGER_API Burger::FileManager::GetCreationTime(Burger::Filename *pFileName,Burger::TimeDate_t *pOutput)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		struct timespec m_CreationDate;		// Creation date
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_CRTIME;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		pOutput->Clear();
		uResult = File::FILENOTFOUND;
	} else {
		// Get the file dates
		pOutput->Load(&Entry.m_CreationDate);
		// It's parsed!
		uResult = File::OKAY;
	}
	return uResult;
}

/***************************************

	Determine if a file exists.
	I will return TRUE if the specified path
	is a path to a file that exists, if it doesn't exist
	or it's a directory, I return FALSE.
	Note : I do not check if the file havs any data in it.
	Just the existence of the file.

***************************************/

Word BURGER_API Burger::FileManager::DoesFileExist(Burger::Filename *pFileName)
{
	Word uResult = FALSE;
	struct stat MyStat;
	int eError = stat(pFileName->GetNative(),&MyStat);
	if (eError>=0) {
		// If it succeeded, the file must exist
		uResult = TRUE;
	}
	return uResult;
}

/***************************************

	Get a file's Filetype
	Only valid for GSOS and MacOS

***************************************/

Word32 BURGER_API Burger::FileManager::GetFileType(Burger::Filename *pFileName)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = 0;
	} else {
		// It's parsed!
		uResult = reinterpret_cast<Word32 *>(Entry.finderInfo)[0];
	}
	return uResult;
}

/***************************************
 
	Get a file's Auxtype
	Only valid for GSOS and MacOS
 
***************************************/

Word32 BURGER_API Burger::FileManager::GetAuxType(Burger::Filename *pFileName)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = 0;
	} else {
		// It's parsed!
		uResult = reinterpret_cast<Word32 *>(Entry.finderInfo)[1];
	}
	return uResult;
}

/***************************************
 
	Get a file's Auxtype and FileType
	Only valid for GSOS and MacOS
 
***************************************/

Word BURGER_API Burger::FileManager::GetFileAndAuxType(Burger::Filename *pFileName,Word32 *pFileType,Word32 *pAuxType)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = File::FILENOTFOUND;
	} else {
		// It's parsed!
		pFileType[0] = reinterpret_cast<Word32 *>(Entry.finderInfo)[0];
		pAuxType[0] = reinterpret_cast<Word32 *>(Entry.finderInfo)[1];
		uResult = File::OKAY;
	}
	return uResult;
}


/***************************************

	Set a file's Filetype
	Only valid for GSOS and MacOS

***************************************/

Word BURGER_API Burger::FileManager::SetFileType(Burger::Filename *pFileName,Word32 uFileType)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = File::FILENOTFOUND;
	} else {
		// It's parsed!
		reinterpret_cast<Word32 *>(Entry.finderInfo)[0] = uFileType;
		eError = setattrlist(pFileName->GetNative(),&AttributesList,&Entry.finderInfo,sizeof(Entry.finderInfo),0);
		if (eError<0) {
			uResult = File::IOERROR;
		} else {
			uResult = File::OKAY;
		}
	}
	return uResult;
}

/***************************************

	Set a file's Auxtype
	Only valid for GSOS and MacOS

***************************************/

Word BURGER_API Burger::FileManager::SetAuxType(Burger::Filename *pFileName,Word32 uAuxType)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = File::FILENOTFOUND;
	} else {
		// It's parsed!
		reinterpret_cast<Word32 *>(Entry.finderInfo)[1] = uAuxType;
		eError = setattrlist(pFileName->GetNative(),&AttributesList,&Entry.finderInfo,sizeof(Entry.finderInfo),0);
		if (eError<0) {
			uResult = File::IOERROR;
		} else {
			uResult = File::OKAY;
		}
	}
	return uResult;
}

/***************************************

	Set a file's Filetype and Auxtype
	Only valid for GSOS and MacOS

***************************************/

Word BURGER_API Burger::FileManager::SetFileAndAuxType(Burger::Filename *pFileName,Word32 uFileType,Word32 uAuxType)
{
	// Structure declaration of data coming from getdirentriesattr()
	struct FInfoAttrBuf {
		unsigned long length;		// Length of this data structure
		char finderInfo[32];		// Aux/File type are the first 8 bytes
	};

	// Attributes requested
	attrlist AttributesList;
	// Buffer to hold the attributes and the filename
	FInfoAttrBuf Entry;

	// Initialize the attributes list
	MemoryClear(&AttributesList,sizeof(AttributesList));
	// "sizeof" for the structure
	AttributesList.bitmapcount = ATTR_BIT_MAP_COUNT;

	// Note: If these flags are changed, the FInfoAttrBuf MUST be
	// adjusted to reflect the request or weird stuff will happen
	AttributesList.commonattr = ATTR_CMN_FNDRINFO;

	// Get the directory entry
	int eError = getattrlist(pFileName->GetNative(),&AttributesList,&Entry,sizeof(Entry),0);

	// No errors?

	Word uResult;
	if (eError<0) {
		uResult = File::FILENOTFOUND;
	} else {
		// It's parsed!
		reinterpret_cast<Word32 *>(Entry.finderInfo)[0] = uFileType;
		reinterpret_cast<Word32 *>(Entry.finderInfo)[1] = uAuxType;
		eError = setattrlist(pFileName->GetNative(),&AttributesList,&Entry.finderInfo,sizeof(Entry.finderInfo),0);
		if (eError<0) {
			uResult = File::IOERROR;
		} else {
			uResult = File::OKAY;
		}
	}
	return uResult;
}

/***************************************

	Create a directory path using an operating system native name
	Return FALSE if successful, or TRUE if an error

***************************************/

Word BURGER_API Burger::FileManager::CreateDirectoryPath(Burger::Filename *pFileName)
{
	// Assume an eror condition
	Word uResult = File::IOERROR;
	// Get the full path
	const char *pPath = pFileName->GetNative();

	// Already here?

	struct stat MyStat;
	int eError = stat(pPath,&MyStat);
	if (eError==0) {
		// Ensure it's a directory for sanity's sake
		if (S_ISDIR(MyStat.st_mode)) {
			// There already is a directory here by this name.
			// Exit okay!
			uResult = File::OKAY;
		}

	} else {
		// No folder here...
		// Let's try the easy way
		eError = mkdir(pPath,0777);
		if (eError==0) {
			// That was easy!
			uResult = File::OKAY;

		} else {

			// Check the pathname
			if (pPath[0]) {

				// This is more complex, parse each
				// segment of the folder to see if it
				// either already exists, and if not,
				// create it.

				// Skip the leading '/'
				char *pWork = const_cast<char *>(pPath)+1;
				// Is there a mid fragment?
				char *pEnd = StringCharacter(pWork,'/');
				if (pEnd) {

					// Let's iterate! Assume success unless 
					// an error occurs in this loop.

					uResult = File::OKAY;
					do {
						// Terminate at the fragment
						pEnd[0] = 0;
						// Create the directory (Maybe)
						eError = mkdir(pPath,0777);
						// Restore the pathname
						pEnd[0] = '/';
						// Error and it's not because it's already present
						if (eError!=0 && errno != EEXIST) {
							// Uh, oh... Perhaps not enough permissions?
							uResult = File::IOERROR;
							break;
						}
						// Skip past this fragment
						pWork = pEnd+1;
						// Get to the next fragement
						pEnd = StringCharacter(pWork,'/');
						// All done?
					} while (pEnd);
				}
			}		
		}
	}
	return uResult;
}

/***************************************

	Change a directory using long filenames
	This only accepts Native OS filenames

***************************************/

Word BURGER_API Burger::FileManager::ChangeOSDirectory(Burger::Filename *pDirName)
{
	if (!chdir(pDirName->GetNative())) {
		return FALSE;
	}
	return (Word)-1;	// Error!
}

/***************************************

	Open a file using a native path

***************************************/

FILE * BURGER_API Burger::FileManager::OpenFile(Burger::Filename *pFileName,const char *pType)
{
	return fopen(pFileName->GetNative(),pType);		/* Do it the MacOSX way */
}

/***************************************

	Copy a file using native pathnames

***************************************/

Word BURGER_API Burger::FileManager::CopyFile(Burger::Filename *pDestName,Burger::Filename *pSourceName)
{
	Word uResult = TRUE;
	NSAutoreleasePool *pPool = [[NSAutoreleasePool alloc] init];

	NSFileManager *pFileManager = [[NSFileManager alloc] init];
	if (pFileManager) {
		NSString *pSrcString = [NSString stringWithUTF8String:pSourceName->GetNative()];
		if (pSrcString) {
			NSString *pDestString = [NSString stringWithUTF8String:pDestName->GetNative()];
			if (pDestString) {
#if defined(MAC_OS_X_VERSION_10_5) && 0
				if ([pFileManager copyItemAtPath:pSrcString toPath:pDestString error:NULL]==YES) {
					uResult = FALSE;
				}
#else
				if ([pFileManager copyPath:pSrcString toPath:pDestString handler:NULL]==YES) {
					uResult = FALSE;
				}
#endif
			}
		}
		[pFileManager release];
	}
	if (pPool) {
		[pPool release];
	}
	// Free all allocated temp memory
	return uResult;
}

/***************************************

	Delete a file using native file system

***************************************/

Word BURGER_API Burger::FileManager::DeleteFile(Burger::Filename *pFileName)
{
	if (!remove(pFileName->GetNative())) {
		return FALSE;
	}
	return TRUE;		/* Oh oh... */
}

/***************************************

	Rename a file using native pathnames

***************************************/

Word BURGER_API Burger::FileManager::RenameFile(Burger::Filename *pNewName,Burger::Filename *pOldName)
{
	if (!rename(pOldName->GetNative(),pNewName->GetNative())) {
		return FALSE;
	}
	return TRUE;		/* Oh oh... */
}

#endif
