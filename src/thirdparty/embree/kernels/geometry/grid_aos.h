// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "primitive.h"
#include "discrete_tessellation.h"
#include "../bvh/bvh.h"
#include "../subdiv/patch_eval.h"
#include "../subdiv/subdivpatch1base.h"

#define GRID_COMPRESS_BOUNDS 1

namespace embree
{
  namespace isa 
  {
    struct GridAOS
    {
      struct CompressedBounds16;
      struct UncompressedBounds16;
      
#if GRID_COMPRESS_BOUNDS
      typedef CompressedBounds16 Bounds16;
#else
      typedef UncompressedBounds16 Bounds16;
#endif
      
      struct UncompressedBounds16
      {
        static const size_t N = 4;
        
        /*! Sets bounding box of child. */
        __forceinline void clear() 
        {
          for (size_t i=0; i<4; i++) {
            lower_x[i] = lower_y[i] = lower_z[i] = pos_inf;
            upper_x[i] = upper_y[i] = upper_z[i] = neg_inf;
          }
        }
        
        /*! set bounds of all children (only required for compression) */
        __forceinline void set(const BBox3fa& bounds) {
        }
        
        /*! Sets bounding box of child. */
        __forceinline void set(size_t i, const BBox3fa& bounds) 
        {
          assert(i < 4*N);
          ((float*)&lower_x)[i] = bounds.lower.x; 
          ((float*)&lower_y)[i] = bounds.lower.y; 
          ((float*)&lower_z)[i] = bounds.lower.z;
          ((float*)&upper_x)[i] = bounds.upper.x; 
          ((float*)&upper_y)[i] = bounds.upper.y; 
          ((float*)&upper_z)[i] = bounds.upper.z;
        }
        
        /*! intersection with single rays */
        template<bool robust>
        __forceinline size_t intersect(size_t i, size_t _nearX, size_t _nearY, size_t _nearZ,
                                       const Vec3vf4& org, const Vec3vf4& rdir, const Vec3vf4& org_rdir, const vfloat4& tnear, const vfloat4& tfar) const
        {
          const size_t nearX = 4*_nearX, nearY = 4*_nearY, nearZ = 4*_nearZ; 
          const size_t farX  = nearX ^ (4*sizeof(vfloat4)), farY  = nearY ^ (4*sizeof(vfloat4)), farZ  = nearZ ^ (4*sizeof(vfloat4));
#if defined (__AVX2__)
          const vfloat4 tNearX = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+nearX)), rdir.x, org_rdir.x);
          const vfloat4 tNearY = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+nearY)), rdir.y, org_rdir.y);
          const vfloat4 tNearZ = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+nearZ)), rdir.z, org_rdir.z);
          const vfloat4 tFarX  = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+farX )), rdir.x, org_rdir.x);
          const vfloat4 tFarY  = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+farY )), rdir.y, org_rdir.y);
          const vfloat4 tFarZ  = msub(vfloat4::load((const float*)((const char*)&lower_x[i]+farZ )), rdir.z, org_rdir.z);
#else
          const vfloat4 tNearX = (vfloat4::load((const float*)((const char*)&lower_x[i]+nearX)) - org.x) * rdir.x;
          const vfloat4 tNearY = (vfloat4::load((const float*)((const char*)&lower_x[i]+nearY)) - org.y) * rdir.y;
          const vfloat4 tNearZ = (vfloat4::load((const float*)((const char*)&lower_x[i]+nearZ)) - org.z) * rdir.z;
          const vfloat4 tFarX  = (vfloat4::load((const float*)((const char*)&lower_x[i]+farX )) - org.x) * rdir.x;
          const vfloat4 tFarY  = (vfloat4::load((const float*)((const char*)&lower_x[i]+farY )) - org.y) * rdir.y;
          const vfloat4 tFarZ  = (vfloat4::load((const float*)((const char*)&lower_x[i]+farZ )) - org.z) * rdir.z;
#endif
          
          if (robust) {
            const float round_down = 1.0f-2.0f*float(ulp);
            const float round_up   = 1.0f+2.0f*float(ulp);
            const vfloat4 tNear = max(tNearX,tNearY,tNearZ,tnear);
            const vfloat4 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
            const vbool4 vmask = round_down*tNear <= round_up*tFar;
            const size_t mask = movemask(vmask);
            return mask;
          }
          
#if defined(__SSE4_1__)
          const vfloat4 tNear = maxi(maxi(tNearX,tNearY),maxi(tNearZ,tnear));
          const vfloat4 tFar  = mini(mini(tFarX ,tFarY ),mini(tFarZ ,tfar ));
          const vbool4 vmask = asInt(tNear) > asInt(tFar);
          const size_t mask = movemask(vmask)^0xf;
#else
          const vfloat4 tNear = max(tNearX,tNearY,tNearZ,tnear);
          const vfloat4 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
          const vbool4 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#endif
          return mask;
        }
        
        template<bool robust>
        __forceinline size_t intersect(size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf4& org, const Vec3vf4& rdir, const Vec3vf4& org_rdir, const vfloat4& tnear, const vfloat4& tfar) const
        {
          const size_t mask0 = intersect<robust>(0, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask1 = intersect<robust>(1, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask2 = intersect<robust>(2, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask3 = intersect<robust>(3, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          return mask0 | (mask1 << 4) | (mask2 << 8) | (mask3 << 12);
        }
        
#if defined (__AVX__)
        
        /*! intersection with single rays */
        template<bool robust>
        __forceinline size_t intersect(size_t i, size_t _nearX, size_t _nearY, size_t _nearZ,
                                       const Vec3vf8& org, const Vec3vf8& rdir, const Vec3vf8& org_rdir, const vfloat8& tnear, const vfloat8& tfar) const
        {
          const size_t nearX = 4*_nearX, nearY = 4*_nearY, nearZ = 4*_nearZ; 
          const size_t farX  = nearX ^ (4*sizeof(vfloat4)), farY  = nearY ^ (4*sizeof(vfloat4)), farZ  = nearZ ^ (4*sizeof(vfloat4));
#if defined (__AVX2__)
          const vfloat8 tNearX = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+nearX)), rdir.x, org_rdir.x);
          const vfloat8 tNearY = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+nearY)), rdir.y, org_rdir.y);
          const vfloat8 tNearZ = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+nearZ)), rdir.z, org_rdir.z);
          const vfloat8 tFarX  = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+farX )), rdir.x, org_rdir.x);
          const vfloat8 tFarY  = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+farY )), rdir.y, org_rdir.y);
          const vfloat8 tFarZ  = msub(vfloat8::load((const float*)((const char*)&lower_x[i]+farZ )), rdir.z, org_rdir.z);
#else
          const vfloat8 tNearX = (vfloat8::load((const float*)((const char*)&lower_x[i]+nearX)) - org.x) * rdir.x;
          const vfloat8 tNearY = (vfloat8::load((const float*)((const char*)&lower_x[i]+nearY)) - org.y) * rdir.y;
          const vfloat8 tNearZ = (vfloat8::load((const float*)((const char*)&lower_x[i]+nearZ)) - org.z) * rdir.z;
          const vfloat8 tFarX  = (vfloat8::load((const float*)((const char*)&lower_x[i]+farX )) - org.x) * rdir.x;
          const vfloat8 tFarY  = (vfloat8::load((const float*)((const char*)&lower_x[i]+farY )) - org.y) * rdir.y;
          const vfloat8 tFarZ  = (vfloat8::load((const float*)((const char*)&lower_x[i]+farZ )) - org.z) * rdir.z;
#endif
          
          if (robust) {
            const float round_down = 1.0f-2.0f*float(ulp);
            const float round_up   = 1.0f+2.0f*float(ulp);
            const vfloat8 tNear = max(tNearX,tNearY,tNearZ,tnear);
            const vfloat8 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
            const vbool8 vmask = round_down*tNear <= round_up*tFar;
            const size_t mask = movemask(vmask);
            return mask;
          }
          
#if defined(__AVX2__)
          const vfloat8 tNear = maxi(maxi(tNearX,tNearY),maxi(tNearZ,tnear));
          const vfloat8 tFar  = mini(mini(tFarX ,tFarY ),mini(tFarZ ,tfar ));
          const vbool8 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#else
          const vfloat8 tNear = max(tNearX,tNearY,tNearZ,tnear);
          const vfloat8 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
          const vbool8 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#endif
          return mask;
        }
        
        template<bool robust>
        __forceinline size_t intersect(size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf8& org, const Vec3vf8& rdir, const Vec3vf8& org_rdir, const vfloat8& tnear, const vfloat8& tfar) const
        {
          const size_t mask01 = intersect<robust>(0, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask23 = intersect<robust>(2, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          return mask01 | (mask23 << 8);
        }
        
#endif
        
      public:
        vfloat4 lower_x[4];           //!< X dimension of lower bounds of all 4 children.
        vfloat4 upper_x[4];           //!< X dimension of upper bounds of all 4 children.
        vfloat4 lower_y[4];           //!< Y dimension of lower bounds of all 4 children.
        vfloat4 upper_y[4];           //!< Y dimension of upper bounds of all 4 children.
        vfloat4 lower_z[4];           //!< Z dimension of lower bounds of all 4 children.
        vfloat4 upper_z[4];           //!< Z dimension of upper bounds of all 4 children.
      };
      
      struct CompressedBounds16
      {
        static const size_t N = 4;
        
        /*! Sets bounding box of child. */
        __forceinline void clear() 
        {
          for (size_t i=0; i<16; i++) {
            lower_x[i] = lower_y[i] = lower_z[i] = 255;
            upper_x[i] = upper_y[i] = upper_z[i] = 0;
          }
        }
        
        __forceinline void set(const BBox3fa& bounds) {
          offset = bounds.lower;
          scale = max(Vec3fa(1E-20f),bounds.size()/255.0f);
        }
        
        /*! Sets bounding box of child. */
        __forceinline void set(size_t i, const BBox3fa& bounds) 
        {
          assert(i < 4*N);
          const Vec3fa lower = clamp(floor((bounds.lower-offset)/scale),Vec3fa(0.0f),Vec3fa(255.0f));
          const Vec3fa upper = clamp(ceil ((bounds.upper-offset)/scale),Vec3fa(0.0f),Vec3fa(255.0f));
          lower_x[i] = (unsigned char) lower.x;
          lower_y[i] = (unsigned char) lower.y;
          lower_z[i] = (unsigned char) lower.z;
          upper_x[i] = (unsigned char) upper.x;
          upper_y[i] = (unsigned char) upper.y;
          upper_z[i] = (unsigned char) upper.z;
        }
        
        /*! intersection with single rays */
        template<bool robust>
        __forceinline size_t intersect(size_t i, size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf4& org, const Vec3vf4& rdir, const Vec3vf4& org_rdir, const vfloat4& tnear, const vfloat4& tfar) const
        {
          const size_t farX  = nearX ^ 16, farY  = nearY ^ 16, farZ  = nearZ ^ 16;
          
          const Vec3vf4 vscale(scale), voffset(offset);
          const vfloat4 near_x = madd(vfloat4::load(&this->all_planes[i]+nearX),vscale.x,voffset.x);
          const vfloat4 near_y = madd(vfloat4::load(&this->all_planes[i]+nearY),vscale.y,voffset.y);
          const vfloat4 near_z = madd(vfloat4::load(&this->all_planes[i]+nearZ),vscale.z,voffset.z);
          const vfloat4 far_x  = madd(vfloat4::load(&this->all_planes[i]+farX),vscale.x,voffset.x);
          const vfloat4 far_y  = madd(vfloat4::load(&this->all_planes[i]+farY),vscale.y,voffset.y);
          const vfloat4 far_z  = madd(vfloat4::load(&this->all_planes[i]+farZ),vscale.z,voffset.z);
          
#if defined (__AVX2__)
          const vfloat4 tNearX = msub(near_x, rdir.x, org_rdir.x);
          const vfloat4 tNearY = msub(near_y, rdir.y, org_rdir.y);
          const vfloat4 tNearZ = msub(near_z, rdir.z, org_rdir.z);
          const vfloat4 tFarX  = msub(far_x , rdir.x, org_rdir.x);
          const vfloat4 tFarY  = msub(far_y , rdir.y, org_rdir.y);
          const vfloat4 tFarZ  = msub(far_z , rdir.z, org_rdir.z);
#else
          const vfloat4 tNearX = (near_x - org.x) * rdir.x;
          const vfloat4 tNearY = (near_y - org.y) * rdir.y;
          const vfloat4 tNearZ = (near_z - org.z) * rdir.z;
          const vfloat4 tFarX  = (far_x  - org.x) * rdir.x;
          const vfloat4 tFarY  = (far_y  - org.y) * rdir.y;
          const vfloat4 tFarZ  = (far_z  - org.z) * rdir.z;
#endif
          
          if (robust) {
            const float round_down = 1.0f-2.0f*float(ulp);
            const float round_up   = 1.0f+2.0f*float(ulp);
            const vfloat4 tNear = max(tNearX,tNearY,tNearZ,tnear);
            const vfloat4 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
            const vbool4 vmask = round_down*tNear <= round_up*tFar;
            const size_t mask = movemask(vmask);
            return mask;
          }
          
#if defined(__SSE4_1__)
          const vfloat4 tNear = maxi(maxi(tNearX,tNearY),maxi(tNearZ,tnear));
          const vfloat4 tFar  = mini(mini(tFarX ,tFarY ),mini(tFarZ ,tfar ));
          const vbool4 vmask = asInt(tNear) > asInt(tFar);
          const size_t mask = movemask(vmask)^0xf;
#else
          const vfloat4 tNear = max(tNearX,tNearY,tNearZ,tnear);
          const vfloat4 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
          const vbool4 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#endif
          return mask;
        }
        
        template<bool robust>
        __forceinline size_t intersect(size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf4& org, const Vec3vf4& rdir, const Vec3vf4& org_rdir, const vfloat4& tnear, const vfloat4& tfar) const
        {
          const size_t mask0 = intersect<robust>( 0, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask1 = intersect<robust>( 4, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask2 = intersect<robust>( 8, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask3 = intersect<robust>(12, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          return mask0 | (mask1 << 4) | (mask2 << 8) | (mask3 << 12);
        }
        
#if defined (__AVX__)
        
        /*! intersection with single rays */
        template<bool robust>
        __forceinline size_t intersect(size_t i, size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf8& org, const Vec3vf8& rdir, const Vec3vf8& org_rdir, const vfloat8& tnear, const vfloat8& tfar) const
        {
          const size_t farX  = nearX ^ 16, farY  = nearY ^ 16, farZ  = nearZ ^ 16;
          
          const Vec3vf8 vscale(scale), voffset(offset);
          const vfloat8 near_x = madd(vfloat8::load(&this->all_planes[i]+nearX),vscale.x,voffset.x);
          const vfloat8 near_y = madd(vfloat8::load(&this->all_planes[i]+nearY),vscale.y,voffset.y);
          const vfloat8 near_z = madd(vfloat8::load(&this->all_planes[i]+nearZ),vscale.z,voffset.z);
          const vfloat8 far_x  = madd(vfloat8::load(&this->all_planes[i]+farX),vscale.x,voffset.x);
          const vfloat8 far_y  = madd(vfloat8::load(&this->all_planes[i]+farY),vscale.y,voffset.y);
          const vfloat8 far_z  = madd(vfloat8::load(&this->all_planes[i]+farZ),vscale.z,voffset.z);
          
#if defined (__AVX2__)
          const vfloat8 tNearX = msub(near_x, rdir.x, org_rdir.x);
          const vfloat8 tNearY = msub(near_y, rdir.y, org_rdir.y);
          const vfloat8 tNearZ = msub(near_z, rdir.z, org_rdir.z);
          const vfloat8 tFarX  = msub(far_x , rdir.x, org_rdir.x);
          const vfloat8 tFarY  = msub(far_y , rdir.y, org_rdir.y);
          const vfloat8 tFarZ  = msub(far_z , rdir.z, org_rdir.z);
#else
          const vfloat8 tNearX = (near_x - org.x) * rdir.x;
          const vfloat8 tNearY = (near_y - org.y) * rdir.y;
          const vfloat8 tNearZ = (near_z - org.z) * rdir.z;
          const vfloat8 tFarX  = (far_x  - org.x) * rdir.x;
          const vfloat8 tFarY  = (far_y  - org.y) * rdir.y;
          const vfloat8 tFarZ  = (far_z  - org.z) * rdir.z;
#endif
          
          if (robust) {
            const float round_down = 1.0f-2.0f*float(ulp);
            const float round_up   = 1.0f+2.0f*float(ulp);
            const vfloat8 tNear = max(tNearX,tNearY,tNearZ,tnear);
            const vfloat8 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
            const vbool8 vmask = round_down*tNear <= round_up*tFar;
            const size_t mask = movemask(vmask);
            return mask;
          }
          
#if defined(__AVX2__)
          const vfloat8 tNear = maxi(maxi(tNearX,tNearY),maxi(tNearZ,tnear));
          const vfloat8 tFar  = mini(mini(tFarX ,tFarY ),mini(tFarZ ,tfar ));
          const vbool8 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#else
          const vfloat8 tNear = max(tNearX,tNearY,tNearZ,tnear);
          const vfloat8 tFar  = min(tFarX ,tFarY ,tFarZ ,tfar);
          const vbool8 vmask = tNear <= tFar;
          const size_t mask = movemask(vmask);
#endif
          return mask;
        }
        
        template<bool robust>
        __forceinline size_t intersect(size_t nearX, size_t nearY, size_t nearZ,
                                       const Vec3vf8& org, const Vec3vf8& rdir, const Vec3vf8& org_rdir, const vfloat8& tnear, const vfloat8& tfar) const
        {
          const size_t mask01 = intersect<robust>(0, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          const size_t mask23 = intersect<robust>(8, nearX, nearY, nearZ, org, rdir, org_rdir, tnear, tfar);
          return mask01 | (mask23 << 8);
        }
        
#endif
        
      public:
        Vec3fa offset;               //!< offset to decompress bounds
        Vec3fa scale;                //!< scale  to decompress bounds
        union {
          struct {
            unsigned char lower_x[16]; 
            unsigned char upper_x[16]; 
            unsigned char lower_y[16]; 
            unsigned char upper_y[16]; 
            unsigned char lower_z[16]; 
            unsigned char upper_z[16]; 
          };
          unsigned char all_planes[6*16];
        };
      };
      
      struct EagerLeaf
      {
        struct Quads
        {
          enum { QUAD2X2 = 0, QUAD1X2 = 1, QUAD2X1 = 2, QUAD1X1 = 3, NONE = 4 };
          
          __forceinline Quads () : type(NONE), ofs(0) {}
          __forceinline Quads (unsigned char type, unsigned char ofs) : type(type), ofs(ofs) {}
          
          friend std::ostream& operator<<(std::ostream& cout, const Quads& a) {
            return cout << "{ type = " << (int) a.type << ", ofs = " << (int) a.ofs << " }";
          }
          
          unsigned char type;
          unsigned char ofs;
        };
        
        const BBox3fa getBounds(const size_t x0, const size_t x1, const size_t y0, const size_t y1) const 
        {
          BBox3fa bounds = empty;
          for (size_t y=y0; y<=y1; y++)
            for (size_t x=x0; x<=x1; x++) 
              bounds.extend(grid.point(x,y));
          
          return bounds;
        }
        
        __forceinline EagerLeaf (const GridAOS& grid) 
          : grid(grid) {}
        
        const BBox3fa init (size_t x0, size_t x1, size_t y0, size_t y1) 
        {
          BBox3fa box_list[16];
          
          BBox3fa box = empty;
          bounds.clear();
          size_t i=0;
          for (size_t y=y0; y<y1; y+=2)
          {
            for (size_t x=x0; x<x1; x+=2)
            {
              assert(i < 16);
              const bool right = x+1 == x1;
              const bool bottom = y+1 == y1;
              const size_t kx0 = x, kx1 = min(x+2,x1);
              const size_t ky0 = y, ky1 = min(y+2,y1);
              new (&quads[i]) Quads(char(2)*bottom+right,(unsigned char)(y*grid.width+x));
              const BBox3fa b = getBounds(kx0,kx1,ky0,ky1);
              box_list[i++] = b;
              box.extend(b);
            }
          }
          
          bounds.set(box);
          for (size_t j=0; j<i; j++)
            bounds.set(j,box_list[j]);
          
          return box;
        }
        
        friend std::ostream& operator<<(std::ostream& cout, const EagerLeaf& a) {
          cout << "{ " << std::endl;
          //cout << "  bounds = " << a.bounds << std::endl;
          for (size_t i=0; i<16; i++) cout << "  quads[" << i << "] = " << a.quads[i] << ", " << std::endl;
          cout << "  grid = " << &a.grid << std::endl;
          return cout << "}";
        }
        
        Bounds16 bounds;
        Quads quads[16];
        const GridAOS& grid;
      };
      
    public:
      
      __forceinline GridAOS(unsigned width, unsigned height, unsigned geomID, unsigned primID)
        : width(width), height(height), geomID(geomID), primID(primID) 
      {
        assert(width <= 17);
        assert(height <= 17);
      }
      
      template<typename Allocator>
      static __forceinline GridAOS* create(Allocator& alloc, const unsigned width, const unsigned height, const unsigned geomID, const unsigned primID) {
        return new (alloc(sizeof(GridAOS)-17*17*sizeof(Vec3fa)+width*height*sizeof(Vec3fa))) GridAOS(width,height,geomID,primID);
      }
      
      __forceinline       Vec3fa& point(const size_t x, const size_t y)       { assert(y*width+x < width*height); return P[y*width+x]; }
      __forceinline const Vec3fa& point(const size_t x, const size_t y) const { assert(y*width+x < width*height); return P[y*width+x]; }
      
      BBox3fa bounds() const
      {
        BBox3fa bounds = empty;
        for (size_t y=0; y<height; y++)
          for (size_t x=0; x<width; x++)
            bounds.extend(point(x,y));
        return bounds;
      }
      
      static size_t getNumEagerLeaves(size_t width, size_t height) {
        const size_t w = (((width +1)/2)+3)/4;
        const size_t h = (((height+1)/2)+3)/4;
        return w*h;
      }
      
      template<int N, typename Allocator>
      size_t createEagerPrims(Allocator& alloc, PrimRef* prims, 
                              const size_t x0, const size_t x1,
                              const size_t y0, const size_t y1)
      {
        size_t i=0;
        for (size_t y=y0; y<y1; y+=8) {
          for (size_t x=x0; x<x1; x+=8) {
            const size_t rx0 = x-x0, rx1 = min(x+8,x1)-x0;
            const size_t ry0 = y-y0, ry1 = min(y+8,y1)-y0;
            EagerLeaf* leaf = new (alloc(sizeof(EagerLeaf))) EagerLeaf(*this);
            const BBox3fa bounds = leaf->init(rx0,rx1,ry0,ry1);
            prims[i++] = PrimRef(bounds,BVHN<N>::encodeTypedLeaf(leaf,0));
          }
        }
        return i;
      }
      
      void build(Scene* scene, SubdivMesh* mesh, unsigned primID,
                 const SubdivPatch1Base& patch, 
                 const size_t x0, const size_t x1,
                 const size_t y0, const size_t y1)
      {
        __aligned(64) float grid_x[17*17+16]; 
        __aligned(64) float grid_y[17*17+16];
        __aligned(64) float grid_z[17*17+16];         
        __aligned(64) float grid_u[17*17+16]; 
        __aligned(64) float grid_v[17*17+16];
        evalGrid(patch,unsigned(x0),unsigned(x1),unsigned(y0),unsigned(y1),patch.grid_u_res,patch.grid_v_res,grid_x,grid_y,grid_z,grid_u,grid_v,mesh);
        
        const size_t dwidth  = x1-x0+1;
        const size_t dheight = y1-y0+1;
        size_t i;
        for (i=0; i+3<dwidth*dheight; i+=4) 
        {
          const vfloat4 xi = vfloat4::load(&grid_x[i]);
          const vfloat4 yi = vfloat4::load(&grid_y[i]);
          const vfloat4 zi = vfloat4::load(&grid_z[i]);
          const vint4   ui = (vint4)clamp(vfloat4::load(&grid_u[i]) * 0xFFFF, vfloat4(0.0f), vfloat4(0xFFFF)); 
          const vint4   vi = (vint4)clamp(vfloat4::load(&grid_v[i]) * 0xFFFF, vfloat4(0.0f), vfloat4(0xFFFF)); 
          const vfloat4 uv = asFloat((vi << 16) | ui);
          vfloat4 xyzuv0, xyzuv1, xyzuv2, xyzuv3;
          transpose(xi,yi,zi,uv,xyzuv0, xyzuv1, xyzuv2, xyzuv3);
          P[i+0] = Vec3fa(xyzuv0); 
          P[i+1] = Vec3fa(xyzuv1); 
          P[i+2] = Vec3fa(xyzuv2);
          P[i+3] = Vec3fa(xyzuv3);
        }
        for (; i<dwidth*dheight; i++) 
        {
          const float xi = grid_x[i];
          const float yi = grid_y[i];
          const float zi = grid_z[i];
          const int   ui = int(clamp(grid_u[i] * 0xFFFF, 0.0f, float(0xFFFF))); 
          const int   vi = int(clamp(grid_v[i] * 0xFFFF, 0.0f, float(0xFFFF))); 
          P[i] = Vec3fa(xi,yi,zi);
          P[i].a = (vi << 16) | ui;
        }
      }
      
      template<int N, typename Allocator>
      static size_t createEager(const SubdivPatch1Base& patch, Scene* scene, SubdivMesh* mesh, unsigned primID, Allocator& alloc, PrimRef* prims)
      {
        size_t num = 0;
        const unsigned x0 = 0, x1 = patch.grid_u_res-1;
        const unsigned y0 = 0, y1 = patch.grid_v_res-1;
        
        for (unsigned y=y0; y<y1; y+=16)
        {
          for (unsigned x=x0; x<x1; x+=16) 
          {
            const unsigned lx0 = x, lx1 = min(lx0+16,x1);
            const unsigned ly0 = y, ly1 = min(ly0+16,y1);
            GridAOS* leaf = GridAOS::create(alloc,lx1-lx0+1,ly1-ly0+1,mesh->id,primID);
            leaf->build(scene,mesh,primID,patch,lx0,lx1,ly0,ly1);
            size_t n = leaf->createEagerPrims<N>(alloc,prims,lx0,lx1,ly0,ly1);
            prims += n;
            num += n;
          }
        }
        return num;
      }
      
      template<int N, typename Allocator>
      static void* create(SubdivPatch1Base* const patch, Scene* scene, Allocator& alloc)
      {
        PrimRef prims[32];
        size_t num MAYBE_UNUSED = createEager<N>(*patch,scene,scene->getSubdivMesh(patch->geom),patch->prim,alloc,prims);
        assert(num == 1);
        return (void*) prims[0].ID();
      }
      
    public:
      unsigned width;
      unsigned height;
      unsigned geomID;
      unsigned primID;
      Vec3fa P[17*17];
    };
  }
}
