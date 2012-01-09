/**\file lumpdirectory.c
 *\section License
 * License: GPL
 * Online License Link: http://www.gnu.org/licenses/gpl.html
 *
 *\author Copyright © 2003-2012 Jaakko Keränen <jaakko.keranen@iki.fi>
 *\author Copyright © 2006-2012 Daniel Swanson <danij@dengine.net>
 *\author Copyright © 2006 Jamie Jones <jamie_jones_au@yahoo.com.au>
 *\author Copyright © 1999-2006 by Colin Phipps, Florian Schulze, Neil Stevens, Andrey Budko (PrBoom 2.2.6)
 *\author Copyright © 1999-2001 by Jess Haas, Nicolas Kalkhof (PrBoom 2.2.6)
 *\author Copyright © 1999 Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman (PrBoom 2.2.6)
 *\author Copyright © 1993-1996 by id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include <ctype.h>

#include "de_base.h"
#include "de_console.h"
#include "de_filesys.h"

#include "lumpdirectory.h"

/**
 * @ingroup lumpDirectoryFlags
 * @{
 */
#define LDF_INTERNAL_MASK               0xff000000
#define LDF_RECORDS_HASHDIRTY           0x80000000 // Hash needs a rebuild.
/**@}*/

typedef struct {
    // killough 1/31/98: hash table fields, used for ultra-fast hash table lookup
    lumpnum_t hashRoot, hashNext;
    lumpnum_t presortIndex;
    abstractfile_t* fsObject;
    int fsLumpIdx;
} lumpdirectory_lumprecord_t;

struct lumpdirectory_s {
    int _flags; /// @see lumpDirectoryFlags
    int _numRecords;
    lumpdirectory_lumprecord_t* _records;
};

/**
 * This is a hash function. It uses the eight-character lump name to generate
 * a somewhat-random number suitable for use as a hash key.
 *
 * Originally DOOM used a sequential search for locating lumps by name. Large
 * wads with > 1000 lumps meant an average of over 500 were probed during every
 * search. Rewritten by Lee Killough to use a hash table for performance and
 * now the average is under 2 probes per search.
 *
 * @param  s  Lump name to be hashed.
 * @return  The generated hash key.
 */
static uint hashLumpShortName(const lumpname_t lumpName)
{
    const char* s = lumpName;
    uint hash;
    (void) ((hash =          toupper(s[0]), s[1]) &&
            (hash = hash*3 + toupper(s[1]), s[2]) &&
            (hash = hash*2 + toupper(s[2]), s[3]) &&
            (hash = hash*2 + toupper(s[3]), s[4]) &&
            (hash = hash*2 + toupper(s[4]), s[5]) &&
            (hash = hash*2 + toupper(s[5]), s[6]) &&
            (hash = hash*2 + toupper(s[6]),
             hash = hash*2 + toupper(s[7]))
           );
    return hash;
}

lumpdirectory_t* LumpDirectory_New(void)
{
    lumpdirectory_t* ld = (lumpdirectory_t*)malloc(sizeof(lumpdirectory_t));
    ld->_numRecords = 0;
    ld->_records = NULL;
    ld->_flags = 0;
    return ld;
}

void LumpDirectory_Delete(lumpdirectory_t* ld)
{
    assert(ld);
    LumpDirectory_Clear(ld);
    free(ld);
}

boolean LumpDirectory_IsValidIndex(lumpdirectory_t* ld, lumpnum_t lumpNum)
{
    assert(ld);
    return (lumpNum >= 0 && lumpNum < ld->_numRecords);
}

static const lumpdirectory_lumprecord_t* LumpDirectory_Record(lumpdirectory_t* ld, lumpnum_t lumpNum)
{
    assert(ld);
    if(LumpDirectory_IsValidIndex(ld, lumpNum))
    {
        return ld->_records + lumpNum;
    }
    Con_Error("LumpDirectory::Record: Invalid lumpNum #%i (valid range: [0...%i).", lumpNum, ld->_numRecords);
    exit(1); // Unreachable.
}

const LumpInfo* LumpDirectory_LumpInfo(lumpdirectory_t* ld, lumpnum_t lumpNum)
{
    const lumpdirectory_lumprecord_t* rec = LumpDirectory_Record(ld, lumpNum);
    return F_LumpInfo(rec->fsObject, rec->fsLumpIdx);
}

abstractfile_t* LumpDirectory_SourceFile(lumpdirectory_t* ld, lumpnum_t lumpNum)
{
    return LumpDirectory_Record(ld, lumpNum)->fsObject;
}

int LumpDirectory_LumpIndex(lumpdirectory_t* ld, lumpnum_t lumpNum)
{
    return LumpDirectory_Record(ld, lumpNum)->fsLumpIdx;
}

int LumpDirectory_NumLumps(lumpdirectory_t* ld)
{
    assert(ld);
    return ld->_numRecords;
}

/**
 * Moves @a count lumps starting beginning at @a from.
 * \assume LumpDirectory::records is large enough for this operation!
 */
static void LumpDirectory_Move(lumpdirectory_t* ld, uint from, uint count, int offset)
{
    assert(ld);
    // Check that our information is valid.
    if(offset == 0 || count == 0 || from >= (unsigned)ld->_numRecords-1)
        return;
    memmove(ld->_records + from + offset, ld->_records + from, sizeof(lumpdirectory_lumprecord_t) * count);
    // We'll need to rebuild the hash.
    ld->_flags |= LDF_RECORDS_HASHDIRTY;
}

static void LumpDirectory_Resize(lumpdirectory_t* ld, int numItems)
{
    assert(ld);
    if(numItems < 0) numItems = 0;
    if(0 != numItems)
    {
        ld->_records = (lumpdirectory_lumprecord_t*) realloc(ld->_records, sizeof(*ld->_records) * numItems);
        if(NULL == ld->_records)
            Con_Error("LumpDirectory::Resize: Failed on (re)allocation of %lu bytes for "
                "LumpInfo record list.", (unsigned long) (sizeof(*ld->_records) * numItems));
    }
    else if(ld->_records)
    {
        free(ld->_records), ld->_records = NULL;
    }
}

int LumpDirectory_PruneByFile(lumpdirectory_t* ld, abstractfile_t* fsObject)
{
    assert(ld);
    {
    int i, origNumLumps = ld->_numRecords;

    if(!fsObject || 0 == ld->_numRecords) return 0;

    // Do this one lump at a time, respecting the possibly-sorted order.
    for(i = 1; i < ld->_numRecords+1; ++i)
    {
        if(ld->_records[i-1].fsObject != fsObject)
            continue;

        // Move the data in the lump storage.
        LumpDirectory_Move(ld, i, ld->_numRecords-i, -1);
        --ld->_numRecords;
        --i;
        // We'll need to rebuild the info short name hash chains.
        ld->_flags |= LDF_RECORDS_HASHDIRTY;
    }
    // Resize the lump storage to match numRecords.
    LumpDirectory_Resize(ld, ld->_numRecords);
    return origNumLumps - ld->_numRecords;
    }
}

void LumpDirectory_Append(lumpdirectory_t* ld, abstractfile_t* fsObject,
    int lumpIdxBase, int lumpIdxCount)
{
    assert(ld && fsObject);
    {
    int maxRecords = ld->_numRecords + lumpIdxCount; // This must be enough.
    int newRecordBase = ld->_numRecords;
    int i;

    if(0 == lumpIdxCount)
        return;

    // Allocate more memory for the new records.
    LumpDirectory_Resize(ld, maxRecords);

    for(i = 0; i < lumpIdxCount; ++i)
    {
        lumpdirectory_lumprecord_t* record = ld->_records + newRecordBase + i;
        record->fsObject = fsObject;
        record->fsLumpIdx = lumpIdxBase + i;
    }

    ld->_numRecords += lumpIdxCount;

    // It may be that all lumps weren't added. Make sure we don't have
    // excess memory allocated (=synchronize the storage size with the
    // real numRecords).
    LumpDirectory_Resize(ld, ld->_numRecords);

    // We'll need to rebuild the info short name hash chains.
    ld->_flags |= LDF_RECORDS_HASHDIRTY;
    }
}

static void LumpDirectory_BuildHash(lumpdirectory_t* ld)
{
    int i;
    assert(ld);

    if(!(ld->_flags & LDF_RECORDS_HASHDIRTY)) return;

    // First mark slots empty.
    for(i = 0; i < ld->_numRecords; ++i)
    {
        ld->_records[i].hashRoot = -1;
    }

    // Insert nodes to the beginning of each chain, in first-to-last
    // lump order, so that the last lump of a given name appears first
    // in any chain, observing pwad ordering rules. killough
    for(i = 0; i < ld->_numRecords; ++i)
    {
        const LumpInfo* info = F_LumpInfo(ld->_records[i].fsObject, ld->_records[i].fsLumpIdx);
        uint j = hashLumpShortName(info->name) % ld->_numRecords;
        ld->_records[i].hashNext = ld->_records[j].hashRoot; // Prepend to list
        ld->_records[j].hashRoot = i;
    }

    ld->_flags &= ~LDF_RECORDS_HASHDIRTY;
#if _DEBUG
    VERBOSE2( Con_Message("Rebuilt record hash for LumpDirectory %p.\n", ld) )
#endif
}

void LumpDirectory_Clear(lumpdirectory_t* ld)
{
    assert(ld);
    if(ld->_numRecords)
    {
        free(ld->_records), ld->_records = NULL;
    }
    ld->_numRecords = 0;
    ld->_flags &= ~LDF_RECORDS_HASHDIRTY;
}

int LumpDirectory_Iterate2(lumpdirectory_t* ld, abstractfile_t* fsObject,
    int (*callback) (const LumpInfo*, void*), void* paramaters)
{
    int result = 0;
    assert(ld);
    if(callback)
    {
        lumpdirectory_lumprecord_t* record;
        const LumpInfo* info;
        int i;
        for(i = 0; i < ld->_numRecords; ++i)
        {
            record = ld->_records + i;

            // Are we only interested in the lumps from a particular file?
            if(fsObject && record->fsObject != fsObject) continue;

            info = F_LumpInfo(record->fsObject, record->fsLumpIdx);
            result = callback(info, paramaters);
            if(result) break;
        }
    }
    return result;
}

int LumpDirectory_Iterate(lumpdirectory_t* ld, abstractfile_t* fsObject,
    int (*callback) (const LumpInfo*, void*))
{
    return LumpDirectory_Iterate2(ld, fsObject, callback, 0);
}

static lumpnum_t LumpDirectory_IndexForName2(lumpdirectory_t* ld, const char* name,
    boolean matchLumpName)
{
    assert(ld);
    if(!(!name || !name[0]) && ld->_numRecords != 0)
    {
        PathMap searchPattern;
        register int idx;

        // Do we need to rebuild the name hash chains?
        LumpDirectory_BuildHash(ld);

        // Can we use the lump name hash?
        if(matchLumpName)
        {
            const int hash = hashLumpShortName(name) % ld->_numRecords;
            idx = ld->_records[hash].hashRoot;
            while(idx != -1 && strnicmp(F_LumpInfo(ld->_records[idx].fsObject, ld->_records[idx].fsLumpIdx)->name, name, LUMPNAME_T_LASTINDEX))
                idx = ld->_records[idx].hashNext;
            return idx;
        }

        /// \todo Do not resort to a linear search.
        PathMap_Initialize(&searchPattern, name);
        for(idx = ld->_numRecords; idx-- > 0; )
        {
            const lumpdirectory_lumprecord_t* rec = ld->_records + idx;
            PathDirectoryNode* node = F_LumpDirectoryNode(rec->fsObject, rec->fsLumpIdx);

            if(PathDirectoryNode_MatchDirectory(node, 0, &searchPattern, NULL/*no paramaters*/))
            {
                // This is the lump we are looking for.
                break;
            }
        }
        PathMap_Destroy(&searchPattern);
        return idx;
    }
    return -1;
}

lumpnum_t LumpDirectory_IndexForPath(lumpdirectory_t* ld, const char* name)
{
    return LumpDirectory_IndexForName2(ld, name, false);
}

lumpnum_t LumpDirectory_IndexForName(lumpdirectory_t* ld, const char* name)
{
    return LumpDirectory_IndexForName2(ld, name, true);
}

int C_DECL LumpDirectory_CompareRecordName(const void* a, const void* b)
{
    const lumpdirectory_lumprecord_t* recordA = (const lumpdirectory_lumprecord_t*)a;
    const lumpdirectory_lumprecord_t* recordB = (const lumpdirectory_lumprecord_t*)b;
    const LumpInfo* infoA = F_LumpInfo(recordA->fsObject, recordA->fsLumpIdx);
    const LumpInfo* infoB = F_LumpInfo(recordB->fsObject, recordB->fsLumpIdx);
    int result = strnicmp(infoA->name, infoB->name, LUMPNAME_T_LASTINDEX);
    if(0 != result) return result;

    // Still matched; try the file load order indexes.
    result = (AbstractFile_LoadOrderIndex(recordA->fsObject) - AbstractFile_LoadOrderIndex(recordB->fsObject));
    if(0 != result) return result;

    // Still matched (i.e., present in the same package); use the pre-sort indexes.
    return (recordB->presortIndex > recordA->presortIndex);
}

int C_DECL LumpDirectory_CompareRecordPath(const void* a, const void* b)
{
    const lumpdirectory_lumprecord_t* recordA = (const lumpdirectory_lumprecord_t*)a;
    const lumpdirectory_lumprecord_t* recordB = (const lumpdirectory_lumprecord_t*)b;
    ddstring_t* pathA = F_ComposeLumpPath(recordA->fsObject, recordA->fsLumpIdx);
    ddstring_t* pathB = F_ComposeLumpPath(recordB->fsObject, recordB->fsLumpIdx);
    int result;

    result = Str_CompareIgnoreCase(pathA, Str_Text(pathB));

    Str_Delete(pathA);
    Str_Delete(pathB);
    if(0 != result) return result;

    // Still matched; try the file load order indexes.
    result = (AbstractFile_LoadOrderIndex(recordA->fsObject) - AbstractFile_LoadOrderIndex(recordB->fsObject));
    if(0 != result) return result;

    // Still matched (i.e., present in the same package); use the pre-sort indexes.
    return (recordB->presortIndex > recordA->presortIndex);
}

static int LumpDirectory_CompareRecords(const lumpdirectory_lumprecord_t* recordA,
    const lumpdirectory_lumprecord_t* recordB, boolean matchLumpName)
{
    ddstring_t* pathA, *pathB;
    int result;
    assert(recordA && recordB);

    if(matchLumpName)
    {
        return strnicmp(F_LumpInfo(recordA->fsObject, recordA->fsLumpIdx)->name,
                        F_LumpInfo(recordB->fsObject, recordB->fsLumpIdx)->name, LUMPNAME_T_MAXLEN);
    }

    pathA = F_ComposeLumpPath(recordA->fsObject, recordA->fsLumpIdx);
    pathB = F_ComposeLumpPath(recordB->fsObject, recordB->fsLumpIdx);

    result = Str_CompareIgnoreCase(pathA, Str_Text(pathB));

    Str_Delete(pathA);
    Str_Delete(pathB);
    return result;
}

void LumpDirectory_PruneDuplicateRecords(lumpdirectory_t* ld, boolean matchLumpName)
{
    int i, j, sortedNumRecords;
    assert(ld);

    if(ld->_numRecords <= 1)
        return; // Obviously no duplicates.

    // Mark with pre-sort indices; so we can determine if qsort changed element ordinals.
    for(i = 0; i < ld->_numRecords; ++i)
        ld->_records[i].presortIndex = i;

    // Sort entries in descending load order for pruning.
    qsort(ld->_records, ld->_numRecords, sizeof(*ld->_records),
        (matchLumpName? LumpDirectory_CompareRecordName : LumpDirectory_CompareRecordPath));

    // Peform the prune. One scan through the directory is enough (left relative).
    sortedNumRecords = ld->_numRecords;
    for(i = 1; i < sortedNumRecords; ++i)
    {
        // Is this entry equal to one or more of the next?
        j = i;
        while(j < sortedNumRecords &&
              !LumpDirectory_CompareRecords(ld->_records + (j-1),
                                            ld->_records + (j), matchLumpName))
        { ++j; }
        if(j == i) continue; // No.

        // Shift equal-range -1 out of the set.
        if(i != sortedNumRecords-1)
            memmove(&ld->_records[i], &ld->_records[j],
                sizeof(lumpdirectory_lumprecord_t) * (sortedNumRecords-j));
        sortedNumRecords -= j-i;

        // We'll need to rebuild the hash.
        ld->_flags |= LDF_RECORDS_HASHDIRTY;

        // We want to re-test with the new candidate.
        --i;
    }

    // Do we need to invalidate the hash?
    if(!(ld->_flags & LDF_RECORDS_HASHDIRTY))
    {
        // As yet undecided. Compare the original ordinals.
        for(i = 0; i < sortedNumRecords; ++i)
        {
            if(i != ld->_records[i-1].presortIndex)
                ld->_flags |= LDF_RECORDS_HASHDIRTY;
        }
    }

    // Resize record storage to match?
    if(sortedNumRecords != ld->_numRecords)
        LumpDirectory_Resize(ld, ld->_numRecords = sortedNumRecords);
}

void LumpDirectory_Print(lumpdirectory_t* ld)
{
    int i;
    assert(ld);

    printf("LumpDirectory %p (%i records):\n", ld, ld->_numRecords);

    for(i = 0; i < ld->_numRecords; ++i)
    {
        lumpdirectory_lumprecord_t* rec = ld->_records + i;
        const LumpInfo* info = F_LumpInfo(rec->fsObject, rec->fsLumpIdx);
        printf("%04i - \"%s\" (size: %lu bytes%s)\n", i,
               F_PrettyPath(Str_Text(AbstractFile_Path(rec->fsObject))),
               (unsigned long) info->size, (info->compressedSize != info->size? " compressed" : ""));
    }
    printf("---End of lumps---\n");
}
