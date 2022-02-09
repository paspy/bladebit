#pragma once
#include "plotshared/PlotTools.h"
#include "Util.h"
#include <vector>

class FileStream;

enum class PlotTable
{
    Table1 = 0,
    Table2,
    Table3,
    Table4,
    Table5,
    Table6,
    Table7,
    C1,
    C2,
    C3,
}; ImplementArithmeticOps( PlotTable );

struct PlotHeader
{
    byte   id  [BB_PLOT_ID_LEN]        = { 0 };
    byte   memo[BB_PLOT_MEMO_MAX_SIZE] = { 0 };
    uint   memoLength     = 0;
    uint32 k              = 0;
    uint64 tablePtrs[10]  = { 0 };
};

// Base Abstract class for read-only plot files
class IPlotFile
{
public:

    inline uint K() const { return _header.k; }

    inline const byte* PlotId() const { return _header.id; }

    inline uint PlotMemoSize() const { return _header.memoLength; }

    inline const byte* PlotMemo() const { return _header.memo; }

    inline uint64 TableAddress( PlotTable table ) const
    {
        ASSERT( table >= PlotTable::Table1 && table <= PlotTable::C3 );
        return _header.tablePtrs[(int)table];
    }

    inline size_t TableSize( PlotTable table )
    {
        ASSERT( table >= PlotTable::Table1 && table <= PlotTable::C3 );

        const uint64 address    = _header.tablePtrs[(int)table];
        uint64       endAddress = PlotSize();

        // Check all table entris where we find and address that is 
        // greater than ours and less than the current end address
        for( int i = 0; i < 10; i++ )
        {
            const uint64 a = _header.tablePtrs[i];
            if( a > address && a < endAddress )
                endAddress = a;
        }

        return (size_t)( endAddress - address );
    }

    inline uint16 ReadUInt16()
    {
        // #NOTE: Unsage, does not check for read errors
        uint16 value = 0;
        Read( sizeof( value ), &value );

        return Swap16( value );
    }

    // Abstract Interface
public:
    virtual bool Open( const char* path ) = 0;
    virtual bool IsOpen() = 0;

    // Plot size in bytes
    virtual size_t PlotSize() const = 0;
    
    // Read data from the plot file
    virtual ssize_t Read( size_t size, void* buffer ) = 0;

    // Seek to the specific location on the plot stream,
    // whatever the underlying implementation may be.
    virtual bool Seek( SeekOrigin origin, int64 offset ) = 0;

    // Get last error ocurred
    virtual int GetError() = 0;

protected:

    // Implementors can call this to load the header
    bool ReadHeader( int& error );

protected:
    PlotHeader _header;
};

class MemoryPlot : public IPlotFile
{
public:
    MemoryPlot();
    ~MemoryPlot();

    bool Open( const char* path ) override;
    bool IsOpen() override;

    size_t PlotSize() const override;
    
    ssize_t Read( size_t size, void* buffer ) override;

    bool Seek( SeekOrigin origin, int64 offset ) override;


private:
    Span<byte>  _bytes;  // Plot bytes
    int         _err      = 0;
    ssize_t     _position = 0;
    std::string _plotPath = "";
};

class PlotReader
{
public:
    PlotReader( IPlotFile& plot );

    uint64 GetC3ParkCount() const;
    uint64 GetF7EntryCount() const;

    bool ReadC3Park( uint64 parkIndex, uint64* f7Buffer );

    uint64 GetFullProofForF7Index( uint64 f7Index, byte* fullProof );

    // void   FindF7ParkIndices( uintt64 f7, std::vector<uint64> indices );

private:
    IPlotFile& _plot;

    uint64 _c3ParkBuffer[CalculateC3Size()];
};
