/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  FreeLoader NTFS support
 *  Copyright (C) 2004       Filip Navara  <xnavara@volny.cz>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Limitations:
 * - No support for compressed files.
 * - No attribute list support.
 * - May crash on currupted filesystem.
 *
 * Bugs:
 * - I encountered file names like 'KERNEL~1.EE' stored on
 *   the disk. These aren't handled correctly yet.
 */

#include <freeldr.h>
#include <fs.h>
#include <disk.h>
#include <rtl.h>
#include <arch.h>
#include <mm.h>
#include <debug.h>
#include <cache.h>

#define NTFS_DEFS
#include "ntfs.h"

PNTFS_BOOTSECTOR NtfsBootSector;
U32 NtfsClusterSize;
U32 NtfsMftRecordSize;
U32 NtfsIndexRecordSize;
U32 NtfsDriveNumber;
U32 NtfsSectorOfClusterZero;
PNTFS_MFT_RECORD NtfsMasterFileTable;
NTFS_ATTR_CONTEXT NtfsMFTContext;

PUCHAR NtfsDecodeRun(PUCHAR DataRun, S64 *DataRunOffset, U64 *DataRunLength)
{
    U8 DataRunOffsetSize;
    U8 DataRunLengthSize;
    U8 i;

    DataRunOffsetSize = (*DataRun >> 4) & 0xF;
    DataRunLengthSize = *DataRun & 0xF;
    *DataRunOffset = 0;
    *DataRunLength = 0;
    DataRun++;
    for (i = 0; i < DataRunLengthSize; i++)
    {
        *DataRunLength += *DataRun << (i << 3);
        DataRun++;
    }
    for (i = 0; i < DataRunOffsetSize; i++)
    {
        *DataRunOffset += *DataRun << (i << 3);
        DataRun++;
    }

    DbgPrint((DPRINT_FILESYSTEM, "DataRunOffsetSize: %x\n", DataRunOffsetSize));
    DbgPrint((DPRINT_FILESYSTEM, "DataRunLengthSize: %x\n", DataRunLengthSize));
    DbgPrint((DPRINT_FILESYSTEM, "DataRunOffset: %x\n", DataRunOffset));
    DbgPrint((DPRINT_FILESYSTEM, "DataRunLength: %x\n", DataRunLength));

    return DataRun;
}

/* FIXME: Add support for attribute lists! */
BOOL NtfsFindAttribute(PNTFS_ATTR_CONTEXT Context, PNTFS_MFT_RECORD MftRecord, U32 Type, PWCHAR Name)
{
    PNTFS_ATTR_RECORD AttrRecord;
    PNTFS_ATTR_RECORD AttrRecordEnd;
    U32 NameLength;
    PWCHAR AttrName;

    AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + MftRecord->AttributesOffset);
    AttrRecordEnd = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + NtfsMftRecordSize);
    for (NameLength = 0; Name[NameLength] != 0; NameLength++)
        ;

    while (AttrRecord < AttrRecordEnd)
    {
        if (AttrRecord->Type == ATTR_TYPE_END)
            break;

        if (AttrRecord->Type == Type)
        {
            if (AttrRecord->NameLength == NameLength)
            {
                AttrName = (PWCHAR)((PCHAR)AttrRecord + AttrRecord->NameOffset);
                if (!RtlCompareMemory(AttrName, Name, NameLength << 1))
                {                
                    /* Found it, fill up the context and return. */
                    Context->Record = AttrRecord;
                    if (AttrRecord->IsNonResident)
                    {
                    	S64 DataRunOffset;
                    	U64 DataRunLength;

                        Context->CacheRun = (PUCHAR)Context->Record + Context->Record->NonResident.MappingPairsOffset;
                        Context->CacheRunOffset = 0;
                        Context->CacheRun = NtfsDecodeRun(Context->CacheRun, &DataRunOffset, &DataRunLength);
                        if (DataRunOffset != -1)
                        {
                            /* Normal run. */
                            Context->CacheRunStartLCN = 
                            Context->CacheRunLastLCN = DataRunOffset;
                        }
                        else
                        {
                            /* Sparse run. */
                            Context->CacheRunStartLCN = -1;
                            Context->CacheRunLastLCN = 0;
                        }
                        Context->CacheRunCurrentOffset = 0;
                    }
                    return TRUE;
                }
            }
        }

        AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)AttrRecord + AttrRecord->Length);
    }

    return FALSE;
}

/* FIXME: Optimize for multisector reads. */
BOOL NtfsDiskRead(U64 Offset, U64 Length, PCHAR Buffer)
{
    U16 ReadLength;

    /* I. Read partial first sector if needed */
    if (Offset % NtfsBootSector->BytesPerSector)
    {
        if (!DiskReadLogicalSectors(NtfsDriveNumber, NtfsSectorOfClusterZero + (Offset / NtfsBootSector->BytesPerSector), 1, (PCHAR)DISKREADBUFFER))
            return FALSE;
        ReadLength = min(Length, NtfsBootSector->BytesPerSector - (Offset % NtfsBootSector->BytesPerSector));
        RtlCopyMemory(Buffer, (PCHAR)DISKREADBUFFER + (Offset % NtfsBootSector->BytesPerSector), ReadLength);
        Buffer += ReadLength;
        Length -= ReadLength;
        Offset += ReadLength;
    }

    /* II. Read all complete 64-sector blocks. */
    while (Length >= 64 * NtfsBootSector->BytesPerSector)
    {
        if (!DiskReadLogicalSectors(NtfsDriveNumber, NtfsSectorOfClusterZero + (Offset / NtfsBootSector->BytesPerSector), 64, (PCHAR)DISKREADBUFFER))
            return FALSE;
        RtlCopyMemory(Buffer, (PCHAR)DISKREADBUFFER, 64 * NtfsBootSector->BytesPerSector);
        Buffer += 64 * NtfsBootSector->BytesPerSector;
        Length -= 64 * NtfsBootSector->BytesPerSector;
        Offset += 64 * NtfsBootSector->BytesPerSector;
    }

    /* III. Read the rest of data */
    if (Length)
    {
        ReadLength = ((Length + NtfsBootSector->BytesPerSector - 1) / NtfsBootSector->BytesPerSector);
        if (!DiskReadLogicalSectors(NtfsDriveNumber, NtfsSectorOfClusterZero + (Offset / NtfsBootSector->BytesPerSector), ReadLength, (PCHAR)DISKREADBUFFER))
            return FALSE;
        RtlCopyMemory(Buffer, (PCHAR)DISKREADBUFFER, Length);
    }

    return TRUE;
}

U64 NtfsReadAttribute(PNTFS_ATTR_CONTEXT Context, U64 Offset, PCHAR Buffer, U64 Length)
{
    U64 LastLCN;
    PUCHAR DataRun;
    S64 DataRunOffset;
    U64 DataRunLength;
    S64 DataRunStartLCN;
    U64 CurrentOffset;
    U64 ReadLength;
    U64 AlreadyRead;
    
    if (!Context->Record->IsNonResident)
    {
        if (Offset > Context->Record->Resident.ValueLength)
            return 0;
        if (Offset + Length > Context->Record->Resident.ValueLength)
            Length = Context->Record->Resident.ValueLength - Offset;
        RtlCopyMemory(Buffer, (PCHAR)Context->Record + Context->Record->Resident.ValueOffset + Offset, Length);
        return Length;
    }

    /*
     * Non-resident attribute
     */

    /*
     * I. Find the corresponding start data run.
     */

    if (Context->CacheRunOffset == Offset)
    {
        DataRun = Context->CacheRun;
        LastLCN = Context->CacheRunLastLCN;
        DataRunStartLCN = Context->CacheRunStartLCN;
        CurrentOffset = Context->CacheRunCurrentOffset;
    }
    else
    {
        LastLCN = 0;
        DataRun = (PUCHAR)Context->Record + Context->Record->NonResident.MappingPairsOffset;
        CurrentOffset = 0;

        while (1)
        {
            DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
            if (DataRunOffset != -1)
            {
                /* Normal data run. */
                DataRunStartLCN = LastLCN + DataRunOffset;
                LastLCN = DataRunStartLCN;
            }
            else
            {
                /* Sparse data run. */
                DataRunStartLCN = -1;
            }

            if (Offset >= CurrentOffset &&
                Offset < CurrentOffset + (DataRunLength * NtfsClusterSize))
            {
                break;
            }

            if (*DataRun == 0)
            {
                return 0;
            }

            CurrentOffset += DataRunLength * NtfsClusterSize;
        }
    }

    /*
     * II. Go through the run list and read the data
     */

    AlreadyRead = 0;
    while (Length > 0)
    {
        ReadLength = min(DataRunLength * NtfsClusterSize, Length);
        if (DataRunStartLCN == -1)
            RtlZeroMemory(Buffer, ReadLength);
	else if (!NtfsDiskRead(DataRunStartLCN * NtfsClusterSize + Offset - CurrentOffset, ReadLength, Buffer))
	    break;
	Length -= ReadLength;
	Buffer += ReadLength;
	AlreadyRead += ReadLength;

	/*
	 * Go to next run in the list. Note that we don't do it only for
         * Length > 0 because of run pointer caching.
         */
	
	{
	    if (*DataRun == 0)
	        break;
            DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
            if (DataRunOffset != -1)
            {
                /* Normal data run. */
                DataRunStartLCN = LastLCN + DataRunOffset;
                LastLCN = DataRunStartLCN;
            }
            else
            {
                /* Sparse data run. */
                DataRunStartLCN = -1;
            }
            CurrentOffset += DataRunLength * NtfsClusterSize;
	}
    }

    Context->CacheRun = DataRun;
    Context->CacheRunOffset = Offset + AlreadyRead;
    Context->CacheRunStartLCN = DataRunStartLCN;
    Context->CacheRunLastLCN = LastLCN;
    Context->CacheRunCurrentOffset = CurrentOffset;

    return AlreadyRead;
}

BOOL NtfsReadMftRecord(U32 MFTIndex, PNTFS_MFT_RECORD Buffer)
{
    return NtfsReadAttribute(&NtfsMFTContext, MFTIndex * NtfsMftRecordSize, (PCHAR)Buffer, NtfsMftRecordSize) == NtfsMftRecordSize;
}

#ifdef DEBUG
VOID NtfsPrintFile(PNTFS_INDEX_ENTRY IndexEntry)
{
    PWCHAR FileName;
    U8 FileNameLength;
    CHAR AnsiFileName[256];
    U8 i;

    FileName = IndexEntry->FileName.FileName;
    FileNameLength = IndexEntry->FileName.FileNameLength;

    for (i = 0; i < FileNameLength; i++)
        AnsiFileName[i] = FileName[i];
    AnsiFileName[i] = 0;

    DbgPrint((DPRINT_FILESYSTEM, "- %s (%x)\n", AnsiFileName, IndexEntry->Data.Directory.IndexedFile));
}
#endif

BOOL NtfsCompareFileName(PCHAR FileName, PNTFS_INDEX_ENTRY IndexEntry)
{
    PWCHAR EntryFileName;
    U8 EntryFileNameLength;
    U8 i;

    EntryFileName = IndexEntry->FileName.FileName;
    EntryFileNameLength = IndexEntry->FileName.FileNameLength;

#ifdef DEBUG
    NtfsPrintFile(IndexEntry);
#endif

    if (strlen(FileName) != EntryFileNameLength)
        return FALSE;

    /* Do case-sensitive compares for Posix file names. */
    if (IndexEntry->FileName.FileNameType == FILE_NAME_POSIX)
    {
        for (i = 0; i < EntryFileNameLength; i++)
            if (EntryFileName[i] != FileName[i])
                return FALSE;
    }
    else
    {
        for (i = 0; i < EntryFileNameLength; i++)
            if (tolower(EntryFileName[i]) != tolower(FileName[i]))
                return FALSE;
    }

    return TRUE;
}

BOOL NtfsFindMftRecord(U32 MFTIndex, PCHAR FileName, U32 *OutMFTIndex)
{
    PNTFS_MFT_RECORD MftRecord;
    U32 Magic;
    NTFS_ATTR_CONTEXT IndexRootCtx;
    NTFS_ATTR_CONTEXT IndexBitmapCtx;
    NTFS_ATTR_CONTEXT IndexAllocationCtx;
    PNTFS_INDEX_ROOT IndexRoot;
    U64 BitmapDataSize;
    U64 IndexAllocationSize;
    PCHAR BitmapData;
    PCHAR IndexRecord;
    PNTFS_INDEX_ENTRY IndexEntry, IndexEntryEnd;
    U32 RecordOffset;
    U32 IndexBlockSize;

    MftRecord = MmAllocateMemory(NtfsMftRecordSize);
    if (MftRecord == NULL)
    {
        return FALSE;
    }

    if (NtfsReadMftRecord(MFTIndex, MftRecord))
    {
        Magic = MftRecord->Magic;

        if (!NtfsFindAttribute(&IndexRootCtx, MftRecord, ATTR_TYPE_INDEX_ROOT, L"$I30"))
        {
            MmFreeMemory(MftRecord);
            return FALSE;
        }
        
        IndexRecord = MmAllocateMemory(NtfsIndexRecordSize);
        if (IndexRecord == NULL)
        {
            MmFreeMemory(MftRecord);
            return FALSE;
        }

        NtfsReadAttribute(&IndexRootCtx, 0, IndexRecord, NtfsIndexRecordSize);
        IndexRoot = (PNTFS_INDEX_ROOT)IndexRecord;
        IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)&IndexRoot->IndexHeader + IndexRoot->IndexHeader.EntriesOffset);
        /* Index root is always resident. */
        IndexEntryEnd = (PNTFS_INDEX_ENTRY)(IndexRecord + IndexRootCtx.Record->Resident.ValueLength);

        DbgPrint((DPRINT_FILESYSTEM, "NtfsIndexRecordSize: %x IndexBlockSize: %x\n", NtfsIndexRecordSize, IndexRoot->IndexBlockSize));

        while (IndexEntry < IndexEntryEnd &&
               !(IndexEntry->Flags & INDEX_ENTRY_END))
        {
            if (NtfsCompareFileName(FileName, IndexEntry))
            {
                *OutMFTIndex = IndexEntry->Data.Directory.IndexedFile;
                MmFreeMemory(IndexRecord);
                MmFreeMemory(MftRecord);
                return TRUE;
            }
	    IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)IndexEntry + IndexEntry->Length);
        }

        if (IndexRoot->IndexHeader.Flags & LARGE_INDEX)
        {
            DbgPrint((DPRINT_FILESYSTEM, "Large Index!\n"));

            IndexBlockSize = IndexRoot->IndexBlockSize;

            if (!NtfsFindAttribute(&IndexBitmapCtx, MftRecord, ATTR_TYPE_BITMAP, L"$I30"))
            {
                DbgPrint((DPRINT_FILESYSTEM, "Corrupted filesystem!\n"));
                MmFreeMemory(MftRecord);
                return FALSE;
            }
            if (IndexBitmapCtx.Record->IsNonResident)
                BitmapDataSize = IndexBitmapCtx.Record->NonResident.DataSize;
            else
                BitmapDataSize = IndexBitmapCtx.Record->Resident.ValueLength;
            DbgPrint((DPRINT_FILESYSTEM, "BitmapDataSize: %x\n", BitmapDataSize));
            BitmapData = MmAllocateMemory(BitmapDataSize);
            if (BitmapData == NULL)
            {
                MmFreeMemory(IndexRecord);
                MmFreeMemory(MftRecord);
                return FALSE;
            }
            NtfsReadAttribute(&IndexBitmapCtx, 0, BitmapData, BitmapDataSize);

            if (!NtfsFindAttribute(&IndexAllocationCtx, MftRecord, ATTR_TYPE_INDEX_ALLOCATION, L"$I30"))
            {
                DbgPrint((DPRINT_FILESYSTEM, "Corrupted filesystem!\n"));
                MmFreeMemory(BitmapData);
                MmFreeMemory(IndexRecord);
                MmFreeMemory(MftRecord);
                return FALSE;
            }
            if (IndexAllocationCtx.Record->IsNonResident)
                IndexAllocationSize = IndexAllocationCtx.Record->NonResident.DataSize;
            else
                IndexAllocationSize = IndexAllocationCtx.Record->Resident.ValueLength;

            RecordOffset = 0;

            for (;;)
            {
                DbgPrint((DPRINT_FILESYSTEM, "RecordOffset: %x IndexAllocationSize: %x\n", RecordOffset, IndexAllocationSize));
                for (; RecordOffset < IndexAllocationSize;)
                {
                    U8 Bit = 1 << ((RecordOffset / IndexBlockSize) & 3);
                    U32 Byte = (RecordOffset / IndexBlockSize) >> 3;
                    if ((BitmapData[Byte] & Bit))
                        break;
                    RecordOffset += IndexBlockSize;
                }
            
                if (RecordOffset >= IndexAllocationSize)
                    break;

                NtfsReadAttribute(&IndexAllocationCtx, RecordOffset, IndexRecord, IndexBlockSize);

                /* FIXME */
                IndexEntry = (PNTFS_INDEX_ENTRY)(IndexRecord + 0x18 + *(U16 *)(IndexRecord + 0x18));
	        IndexEntryEnd = (PNTFS_INDEX_ENTRY)(IndexRecord + IndexBlockSize);

                while (IndexEntry < IndexEntryEnd &&
                       !(IndexEntry->Flags & INDEX_ENTRY_END))
                {        
                    if (NtfsCompareFileName(FileName, IndexEntry))
                    {
                        DbgPrint((DPRINT_FILESYSTEM, "File found\n"));
                        *OutMFTIndex = IndexEntry->Data.Directory.IndexedFile;
                        MmFreeMemory(BitmapData);
                        MmFreeMemory(IndexRecord);
                        MmFreeMemory(MftRecord);
                        return TRUE;
                    }
                    IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)IndexEntry + IndexEntry->Length);
                }

                RecordOffset += IndexBlockSize;
            }

            MmFreeMemory(BitmapData);
        }
        
        MmFreeMemory(IndexRecord);
    }
    else
    {
        DbgPrint((DPRINT_FILESYSTEM, "Can't read MFT record\n"));
    }
    MmFreeMemory(MftRecord);

    return FALSE;
}

BOOL NtfsLookupFile(PUCHAR FileName, PNTFS_MFT_RECORD MftRecord, PNTFS_ATTR_CONTEXT DataContext)
{
    U32 NumberOfPathParts;
    UCHAR PathPart[261];
    U32 CurrentMFTIndex;
    U8 i;

    DbgPrint((DPRINT_FILESYSTEM, "NtfsLookupFile() FileName = %s\n", FileName));

    CurrentMFTIndex = FILE_ROOT;
    NumberOfPathParts = FsGetNumPathParts(FileName);
    for (i = 0; i < NumberOfPathParts; i++)
    {
        FsGetFirstNameFromPath(PathPart, FileName);

        for (; (*FileName != '\\') && (*FileName != '/') && (*FileName != '\0'); FileName++)
            ;
        FileName++;

        DbgPrint((DPRINT_FILESYSTEM, "- Lookup: %s\n", PathPart));
        if (!NtfsFindMftRecord(CurrentMFTIndex, PathPart, &CurrentMFTIndex))
        {
            DbgPrint((DPRINT_FILESYSTEM, "- Failed\n"));
            return FALSE;
        }
        DbgPrint((DPRINT_FILESYSTEM, "- Lookup: %x\n", CurrentMFTIndex));
    }

    if (!NtfsReadMftRecord(CurrentMFTIndex, MftRecord))
    {
        DbgPrint((DPRINT_FILESYSTEM, "NtfsLookupFile: Can't read MFT record\n"));
        return FALSE;
    }

    if (!NtfsFindAttribute(DataContext, MftRecord, ATTR_TYPE_DATA, L""))
    {
        DbgPrint((DPRINT_FILESYSTEM, "NtfsLookupFile: Can't find data attribute\n"));
        return FALSE;
    }

    return TRUE;
}

BOOL NtfsOpenVolume(U32 DriveNumber, U32 VolumeStartSector)
{
    NtfsBootSector = (PNTFS_BOOTSECTOR)DISKREADBUFFER;

    DbgPrint((DPRINT_FILESYSTEM, "NtfsOpenVolume() DriveNumber = 0x%x VolumeStartSector = 0x%x\n", DriveNumber, VolumeStartSector));

    if (!DiskReadLogicalSectors(DriveNumber, VolumeStartSector, 1, (PCHAR)DISKREADBUFFER))
    {
        FileSystemError("Failed to read the boot sector.");
        return FALSE;
    }

    if (RtlCompareMemory(NtfsBootSector->SystemId, "NTFS", 4))
    {
        FileSystemError("Invalid NTFS signature.");
        return FALSE;
    }

    NtfsBootSector = MmAllocateMemory(NtfsBootSector->BytesPerSector);
    if (NtfsBootSector == NULL)
    {
        return FALSE;
    }

    RtlCopyMemory(NtfsBootSector, (PCHAR)DISKREADBUFFER, ((PNTFS_BOOTSECTOR)DISKREADBUFFER)->BytesPerSector);

    NtfsClusterSize = NtfsBootSector->SectorsPerCluster * NtfsBootSector->BytesPerSector;
    if (NtfsBootSector->ClustersPerMftRecord > 0)
        NtfsMftRecordSize = NtfsBootSector->ClustersPerMftRecord * NtfsClusterSize;
    else
        NtfsMftRecordSize = 1 << (-NtfsBootSector->ClustersPerMftRecord);
    if (NtfsBootSector->ClustersPerIndexRecord > 0)
        NtfsIndexRecordSize = NtfsBootSector->ClustersPerIndexRecord * NtfsClusterSize;
    else
        NtfsIndexRecordSize = 1 << (-NtfsBootSector->ClustersPerIndexRecord);

    DbgPrint((DPRINT_FILESYSTEM, "NtfsClusterSize: 0x%x\n", NtfsClusterSize));
    DbgPrint((DPRINT_FILESYSTEM, "ClustersPerMftRecord: %d\n", NtfsBootSector->ClustersPerMftRecord));
    DbgPrint((DPRINT_FILESYSTEM, "ClustersPerIndexRecord: %d\n", NtfsBootSector->ClustersPerIndexRecord));
    DbgPrint((DPRINT_FILESYSTEM, "NtfsMftRecordSize: 0x%x\n", NtfsMftRecordSize));
    DbgPrint((DPRINT_FILESYSTEM, "NtfsIndexRecordSize: 0x%x\n", NtfsIndexRecordSize));

    NtfsDriveNumber = DriveNumber;
    NtfsSectorOfClusterZero = VolumeStartSector;

    DbgPrint((DPRINT_FILESYSTEM, "Reading MFT index...\n"));
    if (!DiskReadLogicalSectors(DriveNumber,
                                NtfsSectorOfClusterZero +
                                (NtfsBootSector->MftLocation * NtfsBootSector->SectorsPerCluster),
                                NtfsMftRecordSize / NtfsBootSector->BytesPerSector, (PCHAR)DISKREADBUFFER))
    {
        FileSystemError("Failed to read the Master File Table record.");
        return FALSE;
    }

    NtfsMasterFileTable = MmAllocateMemory(NtfsMftRecordSize);
    if (NtfsMasterFileTable == NULL)
    {
        MmFreeMemory(NtfsBootSector);
        return FALSE;
    }

    RtlCopyMemory(NtfsMasterFileTable, (PCHAR)DISKREADBUFFER, NtfsMftRecordSize);

    DbgPrint((DPRINT_FILESYSTEM, "Searching for DATA attribute...\n"));
    if (!NtfsFindAttribute(&NtfsMFTContext, NtfsMasterFileTable, ATTR_TYPE_DATA, L""))
    {
        FileSystemError("Can't find data attribute for Master File Table.");
        return FALSE;
    }

    return TRUE;
}

FILE* NtfsOpenFile(PUCHAR FileName)
{
    PNTFS_FILE_HANDLE FileHandle;
    PNTFS_MFT_RECORD MftRecord;

    FileHandle = MmAllocateMemory(sizeof(NTFS_FILE_HANDLE) + NtfsMftRecordSize);
    if (FileHandle == NULL)
    {
        return NULL;
    }

    MftRecord = (PNTFS_MFT_RECORD)(FileHandle + 1);
    if (!NtfsLookupFile(FileName, MftRecord, &FileHandle->DataContext))
    {
        MmFreeMemory(FileHandle);
        return NULL;
    }

    FileHandle->Offset = 0;

    return (FILE*)FileHandle;
}

BOOL NtfsReadFile(FILE *File, U32 BytesToRead, U32* BytesRead, PVOID Buffer)
{
    PNTFS_FILE_HANDLE FileHandle = (PNTFS_FILE_HANDLE)File;
    U64 BytesRead64;
    BytesRead64 = NtfsReadAttribute(&FileHandle->DataContext, FileHandle->Offset, Buffer, BytesToRead);
    if (BytesRead64)
    {
        *BytesRead = (U32)BytesRead64;
        FileHandle->Offset += BytesRead64;
        return TRUE;
    }
    return FALSE;
}

U32 NtfsGetFileSize(FILE *File)
{
    PNTFS_FILE_HANDLE FileHandle = (PNTFS_FILE_HANDLE)File;
    if (FileHandle->DataContext.Record->IsNonResident)
        return (U32)FileHandle->DataContext.Record->NonResident.DataSize;
    else
        return (U32)FileHandle->DataContext.Record->Resident.ValueLength;
}

VOID NtfsSetFilePointer(FILE *File, U32 NewFilePointer)
{
    PNTFS_FILE_HANDLE FileHandle = (PNTFS_FILE_HANDLE)File;
    FileHandle->Offset = NewFilePointer;
}

U32 NtfsGetFilePointer(FILE *File)
{
    PNTFS_FILE_HANDLE FileHandle = (PNTFS_FILE_HANDLE)File;
    return FileHandle->Offset;
}
