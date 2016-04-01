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


template<typename T>
class VectorList
{
	public:

	class iterator
	{
		T* m_ptr;

		public:
		iterator(T* ptr) : m_ptr(ptr) {}
		iterator& operator++()   { ++m_ptr; return *this; }
		iterator operator++(int) { iterator me(*this); ++m_ptr; return me; }
		iterator& operator--()   { --m_ptr; return *this; }
		iterator operator--(int) { iterator me(*this); --m_ptr; return me; }
		T& operator*()  const { return *m_ptr; }
		T* operator->() const { return m_ptr; }
		bool operator==(const iterator& rhs) const { return m_ptr == rhs.m_ptr; }
		bool operator!=(const iterator& rhs) const { return m_ptr != rhs.m_ptr; }
	};

	class const_iterator
	{
		T* m_ptr;

		public:
		const_iterator(T* ptr) : m_ptr(ptr) {}
		const_iterator& operator++()   { ++m_ptr; return *this; }
		const_iterator operator++(int) { const_iterator me(*this); ++m_ptr; return me; }
		const_iterator& operator--()   { --m_ptr; return *this; }
		const_iterator operator--(int) { const_iterator me(*this); --m_ptr; return me; }
		const T& operator*()  const { return *m_ptr; }
		const T* operator->() const { return m_ptr; }
		bool operator==(const const_iterator& rhs) const { return m_ptr == rhs.m_ptr; }
		bool operator!=(const const_iterator& rhs) const { return m_ptr != rhs.m_ptr; }
	};

	class reverse_iterator
	{
		T* m_ptr;

		public:
		reverse_iterator(T* ptr) : m_ptr(ptr) {}
		reverse_iterator& operator++()   { --m_ptr; return *this; }
		reverse_iterator operator++(int) { reverse_iterator me(*this); --m_ptr; return me; }
		reverse_iterator& operator--()   { ++m_ptr; return *this; }
		reverse_iterator operator--(int) { reverse_iterator me(*this); ++m_ptr; return me; }
		T& operator*()  const { return *m_ptr; }
		T* operator->() const { return m_ptr; }
		bool operator==(const reverse_iterator& rhs) const { return m_ptr == rhs.m_ptr; }
		bool operator!=(const reverse_iterator& rhs) const { return m_ptr != rhs.m_ptr; }
	};


	T* m_storage;
	size_t m_end;
	size_t m_start;
	size_t m_capacity;

	void do_realloc() {
		size_t size = m_end - m_start;
		if (size < (m_capacity / 2)) {
			// Repack the data at the beginning of the storage
			// TODO maybe we can force some data alignment
			memcpy(m_storage, &m_storage[m_start], size * sizeof(T));
		} else {
			m_capacity *= 2;
			T* m_temp = (T*)_aligned_malloc(m_capacity * sizeof(T), 32);
			memcpy(m_temp, &m_storage[m_start], size * sizeof(T));
			_aligned_free(m_storage);
			m_storage = m_temp;
		}
		m_start = 0;
		m_end = size;
	}

	public:
	VectorList() {
		m_capacity = 512;
		m_storage  = (T*)_aligned_malloc(m_capacity * sizeof(T), 32);
		m_start    = 0;
		m_end      = 0;
	}

	~VectorList() {
		_aligned_free(m_storage);
	}

	inline iterator begin() const {
		return iterator(m_storage + m_start);
	}

	inline iterator end() const {
		return iterator(m_storage + m_end);
	}

	inline reverse_iterator rbegin() const {
		return reverse_iterator(m_storage + m_end - 1);
	}

	inline reverse_iterator rend() const {
		return reverse_iterator(m_storage + m_start - 1);
	}

	inline const_iterator cbegin() const {
		return const_iterator(m_storage + m_start);
	}

	inline const_iterator cend() const {
		return const_iterator(m_storage + m_end);
	}

	void push_front(T e) {
		if (m_end >= m_capacity) {
			do_realloc();
		}
		m_storage[m_end] = e;
		m_end++;
	}

	inline void erase(iterator position) {
		*position = m_storage[m_start];
		m_start++;
	}

	inline void erase(reverse_iterator position) {
		*position = m_storage[m_start];
		m_start++;
	}

	inline void move_first(iterator position) {
		T e = *position;
		erase(position);
		// iterator could be invalidated after a push_front
		push_front(e);
	}

	inline void move_first(reverse_iterator position) {
		T e = *position;
		erase(position);
		// iterator could be invalidated after a push_front
		push_front(e);
	}

	void clear() {
		// FIXME do I need to delete element?
		m_start = m_end = 0;
	}

};

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

	public:
		Source(GSRenderer* r, const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, uint8* temp);
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

	public:
		Target(GSRenderer* r, const GIFRegTEX0& TEX0, uint8* temp, bool depth_supported);

		virtual void Update();
	};

	class SourceMap
	{
	public:
		hash_set<Source*> m_surfaces;
		list<Source*> m_map[MAX_PAGES];
		uint32 m_pages[16]; // bitmap of all pages
		bool m_used;

		SourceMap() : m_used(false) {memset(m_pages, 0, sizeof(m_pages));}

		void Add(Source* s, const GIFRegTEX0& TEX0, const GSOffset* off);
		void RemoveAll();
		void RemovePartial();
		void RemoveAt(Source* s);
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

	virtual Source* CreateSource(const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, Target* t = NULL, bool half_right = false);
	virtual Target* CreateTarget(const GIFRegTEX0& TEX0, int w, int h, int type);

	virtual int Get8bitFormat() = 0;

	// TODO: virtual void Write(Source* s, const GSVector4i& r) = 0;
	// TODO: virtual void Write(Target* t, const GSVector4i& r) = 0;
#ifndef DISABLE_HW_TEXTURE_CACHE
	virtual void Read(Target* t, const GSVector4i& r) = 0;
#endif

	virtual bool CanConvertDepth() { return m_can_convert_depth; }

public:
	GSTextureCache(GSRenderer* r);
	virtual ~GSTextureCache();
#ifdef DISABLE_HW_TEXTURE_CACHE
	virtual void Read(Target* t, const GSVector4i& r) = 0;
#endif
	void RemoveAll();
	void RemovePartial();

	Source* LookupSource(const GIFRegTEX0& TEX0, const GIFRegTEXA& TEXA, const GSVector4i& r);
	Target* LookupTarget(const GIFRegTEX0& TEX0, int w, int h, int type, bool used);
	Target* LookupTarget(const GIFRegTEX0& TEX0, int w, int h, int real_h);

	void InvalidateVideoMemType(int type, uint32 bp);
	void InvalidateVideoMem(GSOffset* off, const GSVector4i& r, bool target = true);
	void InvalidateLocalMem(GSOffset* off, const GSVector4i& r);

	void IncAge();
	bool UserHacks_HalfPixelOffset;

	const char* to_string(int type) {
		return (type == DepthStencil) ? "Depth" : "Color";
	}

	void PrintMemoryUsage();
};
