#include "DiskPlotPhase2.h"
#include "util/BitField.h"
#include "algorithm/RadixSort.h"
#include "transforms/FxTransform.h"
#include "plotdisk/DiskPlotInfo.h"
#include "util/StackAllocator.h"
#include "DiskPlotInfo.h"
#include "plotdisk/DiskPairReader.h"

// #DEBUG
#include "jobs/IOJob.h"
#include "io/FileStream.h"
#include "DiskPlotDebug.h"

//-----------------------------------------------------------
template<TableId table>
inline void MarkTableEntries( int64 i, const int64 entryCount, BitField lTable, const BitField rTable,
                              uint64 lTableOffset, const Pair* pairs, const uint64* map );

// Fence ids used when loading buckets
struct FenceId
{
    enum
    {
        None = 0,
        MapLoaded,
        PairsLoaded,

        FenceCount
    };
};

struct MarkJob : MTJob<MarkJob>
{
    TableId          table;
    uint32           entryCount;
    Pairs            pairs;
    const uint32*    map;

    DiskPlotContext* context;

    uint64*          lTableMarkedEntries;
    uint64*          rTableMarkedEntries;

    uint64           lTableOffset;
    uint32           pairBucket;
    uint32           pairBucketOffset;


public:
    void Run() override;

    template<TableId table>
    void MarkEntries();

    template<TableId table>
    inline int32 MarkStep( int32 i, const int32 entryCount, BitField lTable, const BitField rTable,
                           uint64 lTableOffset, const Pairs& pairs, const uint32* map );
};

//-----------------------------------------------------------
DiskPlotPhase2::DiskPlotPhase2( DiskPlotContext& context )
    : _context( context )
{
    memset( _bucketBuffers, 0, sizeof( _bucketBuffers ) );

    DiskBufferQueue& ioQueue = *context.ioQueue;

    // #TODO: Give the cache to the marks? Probably not needed for sucha small write...
    //        Then we would need to re-distribute the cache on Phase 3.
    // #TODO: We need to specify the temporary file location
    ioQueue.InitFileSet( FileId::MARKED_ENTRIES_2, "table_2_marks", 1, FileSetOptions::DirectIO, nullptr );
    ioQueue.InitFileSet( FileId::MARKED_ENTRIES_3, "table_3_marks", 1, FileSetOptions::DirectIO, nullptr );
    ioQueue.InitFileSet( FileId::MARKED_ENTRIES_4, "table_4_marks", 1, FileSetOptions::DirectIO, nullptr );
    ioQueue.InitFileSet( FileId::MARKED_ENTRIES_5, "table_5_marks", 1, FileSetOptions::DirectIO, nullptr );
    ioQueue.InitFileSet( FileId::MARKED_ENTRIES_6, "table_6_marks", 1, FileSetOptions::DirectIO, nullptr );

    ioQueue.SeekFile( FileId::T1, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T2, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T3, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T4, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T5, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T6, 0, 0, SeekOrigin::Begin );
    ioQueue.SeekFile( FileId::T7, 0, 0, SeekOrigin::Begin );

    ioQueue.SeekBucket( FileId::MAP2, 0, SeekOrigin::Begin );
    ioQueue.SeekBucket( FileId::MAP3, 0, SeekOrigin::Begin );
    ioQueue.SeekBucket( FileId::MAP4, 0, SeekOrigin::Begin );
    ioQueue.SeekBucket( FileId::MAP5, 0, SeekOrigin::Begin );
    ioQueue.SeekBucket( FileId::MAP6, 0, SeekOrigin::Begin );
    ioQueue.SeekBucket( FileId::MAP7, 0, SeekOrigin::Begin );

    ioQueue.CommitCommands();
}

//-----------------------------------------------------------
DiskPlotPhase2::~DiskPlotPhase2() {}

//-----------------------------------------------------------
void DiskPlotPhase2::Run()
{
    switch( _context.numBuckets )
    {
        case 128 : RunWithBuckets<128 >(); break;
        case 256 : RunWithBuckets<256 >(); break;
        case 512 : RunWithBuckets<512 >(); break;
        case 1024: RunWithBuckets<1024>(); break;
        
        default:
        ASSERT( 0 );
            break;
    }
}

//-----------------------------------------------------------
template<uint32 _numBuckets>
void DiskPlotPhase2::RunWithBuckets()
{
    DiskPlotContext& context = _context;
    DiskBufferQueue& queue   = *context.ioQueue;

    StackAllocator allocator( context.heapBuffer, context.heapSize );
    
    Fence readFence;
    Fence bitFieldFence;
    

    const uint64 maxBucketEntries = (uint64)DiskPlotInfo<TableId::Table1, _numBuckets>::MaxBucketEntries;
    Pair*   pairs = allocator.CAlloc<Pair>  ( maxBucketEntries );
    uint64* map   = allocator.CAlloc<uint64>( maxBucketEntries );

    uint64 maxEntries = context.entryCounts[0];
    for( TableId table = TableId::Table2; table <= TableId::Table7; table++ )
        maxEntries = std::max( maxEntries, context.entryCounts[(int)table] );

    const size_t blockSize     = _context.tmp2BlockSize;
    const size_t markfieldSize = RoundUpToNextBoundaryT( maxEntries / 8, (uint64)blockSize );

    // Prepare 2 marking bitfields for dual-buffering
    uint64* bitFields[2];
    bitFields[0] = allocator.AllocT<uint64>( markfieldSize, blockSize );
    bitFields[1] = allocator.AllocT<uint64>( markfieldSize, blockSize );


    // reader.LoadNextBucket();
    // reader.LoadNextBucket();
    // reader.UnpackBucket( 0, pairs, map );
    // reader.UnpackBucket( 1, pairs, map );

    // _phase3Data.bitFieldSize   = bitFieldSize;
    // _phase3Data.maxTableLength = maxEntries;

    #if _DEBUG && BB_DP_DBG_SKIP_PHASE_2
        return;
    #endif

    // // Prepare map buffer
    // _tmpMap = (uint32*)( context.heapBuffer + bitFieldSize*2 );

    // // Prepare our fences
    // Fence bitFieldFence, bucketLoadFence, mapWriteFence;
    // _bucketReadFence = &bucketLoadFence;
    // _mapWriteFence   = &mapWriteFence;

    // // Set write fence as signalled initially
    // _mapWriteFence->Signal();

    // Mark all tables
    FileId  lTableFileId  = FileId::MARKED_ENTRIES_6;

    uint64* lMarkingTable = bitFields[0];
    uint64* rMarkingTable = bitFields[1];

    for( TableId table = TableId::Table7; table > TableId::Table2; table = table-1 )
    {
        readFence.Reset( 0 );

        const auto timer = TimerBegin();
        
        const size_t stackMarker = allocator.Size();
        DiskPairAndMapReader<_numBuckets> reader( context, context.p2ThreadCount, readFence, table, allocator );

        ASSERT( allocator.Size() < context.heapSize );

        // Log::Line( "Allocated work heap of %.2lf GiB out of %.2lf GiB.", 
        //     (double)(allocator.Size() + markfieldSize*2 ) BtoGB, (double)context.heapSize BtoGB );

        // MarkTable<_numBuckets>( table, reader, pairs, map, lMarkingTable, rMarkingTable );

        //
        // #TEST
        //
        // #if 0
        // if( 0 )
        {
            Debug::ValidatePairs<_numBuckets>( _context, TableId::Table7 );

            Pair*   pairBuf       = bbcvirtalloc<Pair>( maxEntries );
            uint64* pairReadBuf   = bbcvirtalloc<uint64>( maxEntries );
            byte*   rMarkedBuffer = bbcvirtalloc<byte>( maxEntries );
            byte*   lMarkedBuffer = bbcvirtalloc<byte>( maxEntries );

            Pair* pairRef    = bbcvirtalloc<Pair>( 1ull << _K );
            Pair* pairRefTmp = bbcvirtalloc<Pair>( 1ull << _K );

            
            
            for( TableId rTable = TableId::Table7; rTable > TableId::Table1; rTable = rTable-1 )
            {
                const TableId lTable = rTable-1;

                const uint64 rEntryCount = context.entryCounts[(int)rTable];
                const uint64 lEntryCount = context.entryCounts[(int)lTable];

                // BitField rMarkedEntries( (uint64*)rMarkedBuffer );
                // BitField lMarkedEntries( (uint64*)lMarkedBuffer );

                Log::Line( "Reading R table %u...", rTable+1 );
                {
                    const uint32 savedBits    = bblog2( _numBuckets );
                    const uint32 pairBits     = _K + 1 - savedBits + 9;
                    const size_t blockSize    = queue.BlockSize( FileId::T1 );
                    const size_t pairReadSize = (size_t)CDiv( rEntryCount * pairBits, (int)blockSize*8 ) * blockSize;
                    const FileId rTableId     = FileId::T1 + (FileId)rTable;

                    Fence fence;
                    queue.ReadFile( rTableId, 0, pairReadBuf, pairReadSize );
                    queue.SignalFence( fence );
                    queue.CommitCommands();
                    fence.Wait();

                    AnonMTJob::Run( *_context.threadPool, [=]( AnonMTJob* self ) {

                        uint64 count, offset, end;
                        GetThreadOffsets( self, rEntryCount, count, offset, end );

                        BitReader reader( pairReadBuf, rEntryCount * pairBits, offset * pairBits );
                        const uint32 lBits  = _K - savedBits + 1;
                        const uint32 rBits  = 9;
                        for( uint64 i = offset; i < end; i++ )
                        {
                            const uint32 left  = (uint32)reader.ReadBits64( lBits );
                            const uint32 right = left +  (uint32)reader.ReadBits64( rBits );
                            pairBuf[i] = { .left = left, .right = right };
                        }
                    });
                }

                uint64 refEntryCount = 0;
                {
                    Log::Line( "Loading reference pairs." );
                    FatalIf( !Debug::LoadRefTable( "/mnt/p5510a/reference/p1.t7.tmp", pairRef, refEntryCount ),
                        "Failed to load reference table." );

                    ASSERT( refEntryCount == rEntryCount );
                    RadixSort256::Sort<BB_DP_MAX_JOBS,uint64,4>( *_context.threadPool, (uint64*)pairRef, (uint64*)pairRefTmp, refEntryCount );
                }

                const Pair* pairPtr = pairBuf;
                
                uint64 lEntryOffset = 0;
                uint64 rTableOffset = 0;

                Log::Line( "Marking entries..." );
                for( uint32 bucket = 0; bucket < _numBuckets; bucket++ )
                {
                    const uint32 rBucketCount = context.ptrTableBucketCounts[(int)rTable][bucket];
                    const uint32 lBucketCount = context.bucketCounts[(int)lTable][bucket];

                    for( uint e = 0; e < rBucketCount; e++ )
                    {
                        // #NOTE: The bug is related to this.
                        //        Somehow the entries we get from the R table
                        //        are not filtering properly...
                        //        We tested without this and got the exact same
                        //        results from the reference implementation
                        if( rTable < TableId::Table7 )
                        {
                            const uint64 rIdx = rTableOffset + e;
                            // if( !rMarkedEntries.Get( rIdx ) )
                            if( !rMarkedBuffer[rIdx] )
                                continue;
                        }

                        uint64 l = (uint64)pairPtr[e].left  + lEntryOffset;
                        uint64 r = (uint64)pairPtr[e].right + lEntryOffset;

                        ASSERT( l < lEntryCount );
                        ASSERT( r < lEntryCount );

                        lMarkedBuffer[l] = 1;
                        lMarkedBuffer[r] = 1;
                        // lMarkedEntries.Set( l );
                        // lMarkedEntries.Set( r );
                    }

                    pairPtr += rBucketCount;

                    lEntryOffset += lBucketCount;
                    rTableOffset += context.bucketCounts[(int)rTable][bucket];
                }

                uint64 prunedEntryCount = 0;
                Log::Line( "Counting entries." );
                for( uint64 e = 0; e < lEntryCount; e++ )
                {
                    if( lMarkedBuffer[e] )
                        prunedEntryCount++;
                    // if( lMarkedEntries.Get( e ) )
                }

                Log::Line( " %llu/%llu (%.2lf%%)", prunedEntryCount, lEntryCount,
                    ((double)prunedEntryCount / lEntryCount) * 100.0 );
                Log::Line("");

                // Swap marking tables and zero-out the left one.
                std::swap( lMarkedBuffer, rMarkedBuffer );
                memset( lMarkedBuffer, 0, 1ull << _K );
            }
        }
        // #endif

        // Ensure the last table finished writing to the bitfield
        Duration writeWaitTime = Duration::zero();

        if( table < TableId::Table7 )
            bitFieldFence.Wait( writeWaitTime );

        // Submit l marking table for writing
        queue.WriteFile( lTableFileId, 0, lMarkingTable, markfieldSize );
        queue.SignalFence( bitFieldFence );
        queue.CommitCommands();

        // Swap marking tables
        std::swap( lMarkingTable, rMarkingTable );
        lTableFileId = (FileId)( (int)lTableFileId - 1 );

        const double elapsed = TimerEnd( timer );
        Log::Line( "Finished marking table %d in %.2lf seconds.", table, elapsed );
        Log::Line( " IO write wait time: %.2lf seconds.", TicksToSeconds( writeWaitTime ) );
        _context.writeWaitTime += writeWaitTime;

        allocator.PopToMarker( stackMarker );
        ASSERT( allocator.Size() == stackMarker );

        // Log::Line( " Table %u IO Aggregate Wait Time | READ: %.4lf | WRITE: %.4lf | BUFFERS: %.4lf", table,
        //     TicksToSeconds( context.readWaitTime ), TicksToSeconds( context.writeWaitTime ), context.ioQueue->IOBufferWaitTime() );

        // #TEST:
        // if( table < TableId::Table7 )
        // if( 0 )
        {
            BitField markedEntries( rMarkingTable );
            uint64 lTableEntries = context.entryCounts[(int)table-1];

            uint64 bucketsTotalCount = 0;
            for( uint64 e = 0; e < _numBuckets; ++e )
                bucketsTotalCount += context.ptrTableBucketCounts[(int)table-1][e];

            ASSERT( bucketsTotalCount == lTableEntries );

            uint64 lTablePrunedEntries = 0;

            for( uint64 e = 0; e < lTableEntries; ++e )
            {
                if( markedEntries.Get( e ) )
                    lTablePrunedEntries++;
            }

            Log::Line( "Table %u entries: %llu/%llu (%.2lf%%)", table,
                       lTablePrunedEntries, lTableEntries, ((double)lTablePrunedEntries / lTableEntries ) * 100.0 );
            Log::Line( "" );
        }
    }

    // bitFieldFence.Wait( _context.writeWaitTime );
    // queue.CompletePendingReleases();

    // // Unpack table 2 and 7's map here to to make Phase 3 easier, though this will issue more read/writes
    // UnpackTableMap( TableId::Table7 );
    // UnpackTableMap( TableId::Table2 );

    

    // Log::Line( " Phase 2 Total IO Aggregate Wait Time | READ: %.4lf | WRITE: %.4lf | BUFFERS: %.4lf", 
    //         TicksToSeconds( context.readWaitTime ), TicksToSeconds( context.writeWaitTime ), context.ioQueue->IOBufferWaitTime() );
}

//-----------------------------------------------------------
template<uint32 _numBuckets>
void DiskPlotPhase2::MarkTable( const TableId rTable, DiskPairAndMapReader<_numBuckets> reader,
                                Pair* pairs, uint64* map, uint64* lTableMarks, uint64* rTableMarks )
{
    switch( rTable )
    {
        case TableId::Table7: MarkTableBuckets<TableId::Table7, _numBuckets>( reader, pairs, map, lTableMarks, rTableMarks ); break;
        case TableId::Table6: MarkTableBuckets<TableId::Table6, _numBuckets>( reader, pairs, map, lTableMarks, rTableMarks ); break;
        case TableId::Table5: MarkTableBuckets<TableId::Table5, _numBuckets>( reader, pairs, map, lTableMarks, rTableMarks ); break;
        case TableId::Table4: MarkTableBuckets<TableId::Table4, _numBuckets>( reader, pairs, map, lTableMarks, rTableMarks ); break;
        case TableId::Table3: MarkTableBuckets<TableId::Table3, _numBuckets>( reader, pairs, map, lTableMarks, rTableMarks ); break;
    
        default:
            ASSERT( 0 );
            break;
    }
}
//-----------------------------------------------------------
template<TableId table, uint32 _numBuckets>
void DiskPlotPhase2::MarkTableBuckets( DiskPairAndMapReader<_numBuckets> reader, 
                                       Pair* pairs, uint64* map, uint64* lTableMarks, uint64* rTableMarks )
{
    // Load initial bucket
    reader.LoadNextBucket();

    uint64 lTableOffset = 0;

    for( uint32 bucket = 0; bucket < _numBuckets; bucket++ )
    {
        reader.LoadNextBucket();
        reader.UnpackBucket( bucket, pairs, map );

        AnonMTJob::Run( *_context.threadPool, _context.p2ThreadCount, [=]( AnonMTJob* self ) { 
            
            BitField lTableMarkedEntries( lTableMarks );
            BitField rTableMarkedEntries( rTableMarks );

            const uint64 bucketEntryCount = _context.ptrTableBucketCounts[(int)table][bucket];

            if( bucket == 0 )
            {
                // Zero-out l marking table
                const uint64 lTableEntryCount = _context.entryCounts[(int)table];
                const size_t bitFieldSize     = RoundUpToNextBoundary( (size_t)lTableEntryCount / 8, 8 );  // Round up to 64-bit boundary

                size_t count, offset, _;
                GetThreadOffsets( self, bitFieldSize, count, offset, _ );

                if( count )
                    memset( ((byte*)lTableMarks) + offset, 0, count );

                self->SyncThreads();
            }

            // Mark entries
            int64 count, offset, _;
            GetThreadOffsets( self, (int64)bucketEntryCount, count, offset, _ );

            // We need to do 2 passes to ensure no 2 threads attempt to write to the same field at the same time
            const int64 firstPassCount = count / 2;
            
            MarkTableEntries<table>( offset, firstPassCount, lTableMarkedEntries, rTableMarkedEntries, lTableOffset, pairs, map );
            self->SyncThreads();
            MarkTableEntries<table>( offset + firstPassCount, count - firstPassCount, lTableMarkedEntries, rTableMarkedEntries, lTableOffset, pairs, map );

        });

        lTableOffset += _context.bucketCounts[(int)table-1][bucket];
    }
}

//-----------------------------------------------------------
template<TableId table>
inline void MarkTableEntries( int64 i, const int64 entryCount, BitField lTable, const BitField rTable,
                              uint64 lTableOffset, const Pair* pairs, const uint64* map )
{
    for( const int64 end = i + entryCount ; i < end; i++ )
    {
        if constexpr ( table < TableId::Table7 )
        {
            const uint64 rTableIdx = map[i];
            if( !rTable.Get( rTableIdx ) )
                continue;
        }

        const Pair& pair = pairs[i];
        
        const uint64 left  = lTableOffset + pair.left;
        const uint64 right = lTableOffset + pair.right;

        // #TODO Test with atomic sets so that we can write into the
        //       mapped index, and not have to do mapped readings when
        //       reading from the R table here, or in Phase 3.
        lTable.Set( left  );
        lTable.Set( right );
    }
}

//-----------------------------------------------------------
template<TableId table>
void MarkJob::MarkEntries()
{
    DiskPlotContext& context = *this->context;
    DiskBufferQueue& queue   = *context.ioQueue;

    const uint32 jobId               = this->JobId();
    const uint32 threadCount         = this->JobCount();
    const uint64 maxEntries          = 1ull << _K;
    const uint32 maxEntriesPerBucket = (uint32)( maxEntries / (uint64)BB_DP_BUCKET_COUNT );
    const uint64 tableEntryCount     = context.entryCounts[(int)table];

    BitField lTableMarkedEntries( this->lTableMarkedEntries );
    BitField rTableMarkedEntries( this->rTableMarkedEntries );

    // Zero-out our portion of the bit field and sync, do this only on the first run
    if( this->pairBucket == 0 && this->pairBucketOffset == 0 )
    {
        const size_t bitFieldSize  = RoundUpToNextBoundary( (size_t)maxEntries / 8, 8 );  // Round up to 64-bit boundary

              size_t sizePerThread = bitFieldSize / threadCount;
        const size_t sizeRemainder = bitFieldSize - sizePerThread * threadCount;

        byte* buffer = ((byte*)this->lTableMarkedEntries) + sizePerThread * jobId;

        if( jobId == threadCount - 1 )
            sizePerThread += sizeRemainder;

        memset( buffer, 0, sizePerThread );
        this->SyncThreads();
    }

    const uint32* map   = this->map;
    Pairs         pairs = this->pairs;
    
    uint32 bucketEntryCount = this->entryCount;

    // Determine how many passes we need to run for this bucket.
    // Passes are determined depending on the range were currently processing
    // on the pairs buffer. Since they have different starting offsets after each
    // L table bucket length that generated its pairs, we need to update that offset
    // after we reach the boundary of the buckets that generated the pairs.
    while( bucketEntryCount )
    {
        uint32 pairBucket           = this->pairBucket;
        uint32 pairBucketOffset     = this->pairBucketOffset;
        uint64 lTableOffset         = this->lTableOffset;

        uint32 pairBucketEntryCount = context.ptrTableBucketCounts[(int)table][pairBucket];

        uint32 passEntryCount       = std::min( pairBucketEntryCount - pairBucketOffset, bucketEntryCount );

        // Prune the table
        {
            // We need a minimum number of entries per thread to ensure that we don't,
            // write to the same qword in the bit field. So let's ensure that each thread
            // has at least more than 2 groups worth of entries.
            // There's an average of 284,190 entries per bucket, which means each group
            // has an about 236.1 entries. We round up to 280 entries.
            // We use minimum 3 groups and round up to 896 entries per thread which gives us
            // 14 QWords worth of area each threads can reference.
            const uint32 minEntriesPerThread = 896;
            
            uint32 threadsToRun     = threadCount;
            uint32 entriesPerThread = passEntryCount / threadsToRun;
            
            while( entriesPerThread < minEntriesPerThread && threadsToRun > 1 )
                entriesPerThread = passEntryCount / --threadsToRun;

            // Only run with as many threads as we have filtered
            if( jobId < threadsToRun )
            {
                const uint32* jobMap   = map; 
                Pairs         jobPairs = pairs;

                jobMap         += entriesPerThread * jobId;
                jobPairs.left  += entriesPerThread * jobId;
                jobPairs.right += entriesPerThread * jobId;

                // Add any trailing entries to the last thread
                // #NOTE: Ensure this is only updated after we get the pairs offset
                uint32 trailingEntries = passEntryCount - entriesPerThread * threadsToRun;
                uint32 lastThreadId    = threadsToRun - 1;
                if( jobId == lastThreadId )
                    entriesPerThread += trailingEntries;

                // Mark entries in 2 steps to ensure the previous thread does NOT
                // write to the same QWord at the same time as the current thread.
                // (May happen when the prev thread writes to the end entries & the 
                //   current thread is writing to its beginning entries)
                const int32 fistStepEntryCount = (int32)( entriesPerThread / 2 );
                int32 i = 0;

                // 1st step
                i = this->MarkStep<table>( i, fistStepEntryCount, lTableMarkedEntries, rTableMarkedEntries, lTableOffset, jobPairs, jobMap );
                this->SyncThreads();

                // 2nd step
                this->MarkStep<table>( i, entriesPerThread, lTableMarkedEntries, rTableMarkedEntries, lTableOffset, jobPairs, jobMap );
            }
            else
            {
                this->SyncThreads();    // Sync for 2nd step
            }

            this->SyncThreads();    // Sync after marking finished
        }


        // Update our position on the pairs table
        bucketEntryCount -= passEntryCount;
        pairBucketOffset += passEntryCount;

        map         += passEntryCount;
        pairs.left  += passEntryCount;
        pairs.right += passEntryCount;

        if( pairBucketOffset < pairBucketEntryCount )
        {
            this->pairBucketOffset = pairBucketOffset;
        }
        else
        {
            // Update our left entry offset by adding the number of entries in the
            // l table bucket index that matches our paid bucket index
            this->lTableOffset += context.bucketCounts[(int)table-1][pairBucket];

            // Move to next pairs bucket
            this->pairBucket ++;
            this->pairBucketOffset = 0;
        }
    }
}

