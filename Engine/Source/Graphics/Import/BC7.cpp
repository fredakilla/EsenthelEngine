/******************************************************************************

   Have to keep this as a separate file, so it won't be linked if unused.

   BC7 - If entire block has the same alpha value, then it's best when that alpha is equal to 255,
      because in that mode, RGB will have more precision.

/******************************************************************************/
#include "stdafx.h"

#include "../../../../ThirdPartyLibs/begin.h"

#if (WINDOWS && !ARM) || MAC
   #include "../../../../ThirdPartyLibs/BC7/ispc_texcomp/ispc_texcomp.h" // Windows and Mac link against precompiled lib's generated by Intel Compiler, so all we need is a header
#else // other platforms need source
   #include "../../../../ThirdPartyLibs/BC7/ispc_texcomp/ispc_texcomp.cpp"
   #pragma warning(push)
   #pragma warning(disable:4715) // not all control paths return a value
   #include "../../../../ThirdPartyLibs/BC7/kernel.ispc.cpp"
   #pragma warning(pop)
#endif

#include "../../../../ThirdPartyLibs/end.h"

namespace EE{
/******************************************************************************/
static struct BCThreads
{
   Threads  threads;
   Bool     initialized;
   SyncLock lock;

   void init()
   {
      if(!initialized)
      {
         SyncLocker locker(lock);
         if(!initialized)
         {
            threads.create(false, Cpu.threads()-1); // -1 because we will do processing on the caller thread too
            initialized=true; // enable at the end
         }
      }
   }
}BC;
/******************************************************************************/
struct Data
{
   bc7_enc_settings settings;
 C Image           &src;
   Image           &dest;
   Int              total_blocks, thread_blocks, threads;

   Data(C Image &src, Image &dest) : src(src), dest(dest)
   {
      total_blocks=src.h()/4;
      threads=Min(total_blocks, BC.threads.threads1()); // +1 because we will do processing on the caller thread too
      thread_blocks=total_blocks/threads;
   #if 0 // 3x slower and only small quality difference
      GetProfile_alpha_slow(&settings);
   #else
      GetProfile_alpha_basic(&settings);
   #endif
   }
};
/******************************************************************************/
static void CompressBC7Block(IntPtr elm_index, Data &data, Int thread_index)
{
   rgba_surface surf;
   Int block_start=elm_index*data.thread_blocks, y_start=block_start*4;
   surf.ptr   =ConstCast(data.src.data()+y_start*data.src.pitch());
   surf.stride=data.src.pitch();
   surf.width =data.src.w    ();
   surf.height=((elm_index==data.threads-1) ? data.src.h()-y_start : data.thread_blocks*4); // last thread must process all remaining blocks
#if 1 // Intel
   CompressBlocksBC7(&surf, data.dest.data() + block_start*data.dest.pitch(), &data.settings);
#else // DirectX
   REPD(by, surf.height/4)
   REPD(bx, surf.width /4)
   {
      XMVECTOR dx_rgba[4][4]; ASSERT(SIZE(XMVECTOR)==SIZE(Vec4));
      Int      px=bx*4, py=by*4, // pixel
               xo[4], yo[4];
      REP(4)
      {
         xo[i]=px+i;
         yo[i]=py+i+y_start;
      }
      data.src.gather((Vec4*)&dx_rgba[0][0], xo, Elms(xo), yo, Elms(yo));
      DirectX::D3DXEncodeBC7(data.dest.data() + bx*16 + (by+block_start)*data.dest.pitch(), &dx_rgba[0][0], 0);
   }
#endif
}
/******************************************************************************/
Bool _CompressBC7(C Image &src, Image &dest)
{
   Bool ok=false;
   if(dest.hwType()==IMAGE_BC7)
   {
      Image temp; C Image *s=&src;
      if(s->hwType()!=IMAGE_R8G8B8A8 || s->w()!=dest.hwW() || s->h()!=dest.hwH())
      {
         if(s->copyTry(temp, dest.hwW(), dest.hwH(), 1, IMAGE_R8G8B8A8, IMAGE_SOFT, 1, FILTER_NO_STRETCH, true))s=&temp;else return false; // we need to cover the area for entire HW size, to process partial and Pow2Padded blocks too
      }
      if(s->lockRead())
      {
         if(dest.lock(LOCK_WRITE))
         {
            ok=true;
            BC.init();
            Data data(*s, dest); // !! call after 'BC.init' !!
            BC.threads.process1(data.threads, CompressBC7Block, data, INT_MAX); // use all available threads, including this one
            dest.unlock();
         }
         s->unlock();
      }
   }
   return ok;
}
/******************************************************************************/
}
/******************************************************************************/