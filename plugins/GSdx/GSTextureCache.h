/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include "GSRenderer.h"
#include "GSDirtyRect.h"

class GSTextureCache
{
public:
	enum {RenderTarget, DepthStencil};

	class Surface : public GSAlignedClass<32>
	{
	protected:
		GSRenderer* m_renderer;

	public:
		GSTexture* m_texture;
		GIFRegTEX0 m_TEX0;
		GIFRegTEXA m_TEXA;
		int m_age;
		uint8* m_temp;
		bool m_32_bits_fmt; // Allow to detect the casting of 32 bits as 16 bits texture
		bool m_shared_texture;

	public:
		Surface(GSRenderer* r, uint8* temp);
		virtual ~Surface();

		virtual void Update();
	};

	class Source : public Surface
	{
		struct {GSVector4i* rect; uint32 count;} m_write;

		void Write(const GSVector4i& r);
		void Flush(uint32 count);

	public:
		GSTexture* m_palette;
		bool m_initpalette;
		uint32 m_valid[MAX_PAGES]; // each uint32 bits map to the 32 blocks of that page
		uint32* m_clut;
		bool m_target;
		bool m_complete;
		bool m_repeating;
		bool m_spritehack_t;
		vector<GSVector2i>* m_p2t;
		// Keep a trace of the target origin. There is no guarantee that pointer will
		// still be valid on future. However it ought to be good when the source is created
		// so it can be used to access un-converted data for the current draw call.
		GSTexture* m_from_target;

	public:
		Source(GSRenderer* r, const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, uint8* temp, bool dummy_container = false);
		virtual ~Source();

		virtual void Update(const GSVector4i& rect);
	};

	class Target : public Surface
	{
	public:
		int m_type;
		bool m_used;
		GSDirtyRectList m_dirty;
		GSVector4i m_valid;
		bool m_depth_supported;
		bool m_dirty_alpha;
		uint32 m_end_block; // Hint of the target area

	public:
		Target(GSRenderer* r, const GIFRegTEX0& TEX0, uint8* temp, bool depth_supported);

		void UpdateValidity(const GSVector4i& rect);
		bool Inside(uint32 bp, uint32 bw, uint32 psm, const GSVector4i& rect);

		virtual void Update();
	};

	class SourceMap
	{
	public:
		hash_set<Source*> m_surfaces;
		hash_map<uint64, uint32*> m_pages_coverage;
		list<Source*> m_map[MAX_PAGES];
		uint32 m_pages[16]; // bitmap of all pages
		bool m_used;

		SourceMap() : m_used(false) {memset(m_pages, 0, sizeof(m_pages));}

		void Add(Source* s, const GIFRegTEX0& TEX0, GSOffset* off);
		void RemoveAll();
		void RemovePartial();
		void RemoveAt(Source* s);

		uint32* GetPagesCoverage(const GIFRegTEX0& TEX0, GSOffset* off);
	};

protected:
	GSRenderer* m_renderer;
	SourceMap m_src;
	list<Target*> m_dst[2];
	bool m_paltex;
	int m_spritehack;
	bool m_preload_frame;
	uint8* m_temp;
	bool m_can_convert_depth;
	int m_crc_hack_level;
	static bool m_disable_partial_invalidation;
	bool m_texture_inside_rt;

	virtual Source* CreateSource(const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, Target* t = NULL, bool half_right = false, int x_offset = 0, int y_offset = 0);
	virtual Target* CreateTarget(const GIFRegTEX0& TEX0, int w, int h, int type);

	virtual int Get8bitFormat() = 0;

	// TODO: virtual void Write(Source* s, const GSVector4i& r) = 0;
	// TODO: virtual void Write(Target* t, const GSVector4i& r) = 0;

	virtual bool CanConvertDepth() { return m_can_convert_depth; }

public:
	GSTextureCache(GSRenderer* r);
	virtual ~GSTextureCache();
	virtual void Read(Target* t, const GSVector4i& r) = 0;
	virtual void Read(Source* t, const GSVector4i& r) = 0;
	void RemoveAll();
	void RemovePartial();

	Source* LookupSource(const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, const GSVector4i& r);
	Source* LookupDepthSource(const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, const GSVector4i& r, bool palette = false);

	Target* LookupTarget(const GIFRegTEX0& TEX0, int w, int h, int type, bool used);
	Target* LookupTarget(const GIFRegTEX0& TEX0, int w, int h, int real_h);

	void InvalidateVideoMemType(int type, uint32 bp);
	void InvalidateVideoMemSubTarget(GSTextureCache::Target* rt);
	void InvalidateVideoMem(GSOffset* off, const GSVector4i& r, bool target = true);
	void InvalidateLocalMem(GSOffset* off, const GSVector4i& r);

	void IncAge();
	bool UserHacks_HalfPixelOffset;

	const char* to_string(int type) {
		return (type == DepthStencil) ? "Depth" : "Color";
	}

	void PrintMemoryUsage();
};
