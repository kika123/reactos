
#include <w32k.h>

#define NDEBUG
#include <debug.h>


//
//
// Gdi Batch Flush support functions.
//


//
// Process the batch.
//
ULONG
FASTCALL
GdiFlushUserBatch(HDC hDC, PGDIBATCHHDR pHdr)
{
  PDC dc = NULL;
  PDC_ATTR Dc_Attr = NULL;
  if (hDC)
  {
    dc = DC_LockDc(hDC);
    Dc_Attr = dc->pDc_Attr;
    if (!Dc_Attr) Dc_Attr = &dc->Dc_Attr;
  }
  // The thread is approaching the end of sunset.
  switch(pHdr->Cmd)
  {
     case GdiBCPatBlt: // Highest pri first!
        break;
     case GdiBCPolyPatBlt:
        break;
     case GdiBCTextOut:
        break;
     case GdiBCExtTextOut:
        break;
     case GdiBCSetBrushOrg:
     {
        if (!dc) break;
        PGDIBSSETBRHORG pgSBO = (PGDIBSSETBRHORG) pHdr;
        Dc_Attr->ptlBrushOrigin = pgSBO->ptlBrushOrigin;
        break;
     }
     case GdiBCExtSelClipRgn:
        break;
     case GdiBCSelObj:
        break;
     case GdiBCDelObj:
     case GdiBCDelRgn:
     {
        PGDIBSOBJECT pgO = (PGDIBSOBJECT) pHdr;
        NtGdiDeleteObject( pgO->hgdiobj );
        break;
     }
     default:
        break;
  }
  if (dc) DC_UnlockDc(dc);
  return pHdr->Size; // Return the full size of the structure.
}

/*
 * NtGdiFlush
 *
 * Flushes the calling thread's current batch.
 */
VOID
APIENTRY
NtGdiFlush(VOID)
{
  UNIMPLEMENTED;
}

/*
 * NtGdiFlushUserBatch
 *
 * Callback for thread batch flush routine.
 *
 * Think small & fast!
 */
NTSTATUS
APIENTRY
NtGdiFlushUserBatch(VOID)
{
  PTEB pTeb = NtCurrentTeb();
  ULONG GdiBatchCount = pTeb->GdiBatchCount;

  if( (GdiBatchCount > 0) && (GdiBatchCount <= (GDIBATCHBUFSIZE/4)))
  {
    HDC hDC = (HDC) pTeb->GdiTebBatch.HDC;
//
//  If hDC is zero and the buffer fills up with delete objects we need to run
//  anyway. So, hard code to the system batch limit.
//
    if ((hDC) || ((!hDC) && (GdiBatchCount >= GDI_BATCH_LIMIT)))
    {
       PULONG pHdr = &pTeb->GdiTebBatch.Buffer[0];
       // No need to init anything, just go!
       for (; GdiBatchCount > 0; GdiBatchCount--)
       {
           // Process Gdi Batch!
           pHdr += GdiFlushUserBatch( hDC, (PGDIBATCHHDR) pHdr );
       }
       // Exit and clear out for the next round.
       pTeb->GdiTebBatch.Offset = 0;
       pTeb->GdiBatchCount = 0;
       pTeb->GdiTebBatch.HDC = 0;
    }
  }
  return STATUS_SUCCESS;
}

