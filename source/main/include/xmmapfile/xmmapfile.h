// xmmapfile.h - Memory Mapped File
#ifndef __XMMAPFILE_MMAPFILE_H__
#define __XMMAPFILE_MMAPFILE_H__
#include "xbase\x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
	class MMFIO;

	// Memory mapped to a file on disk
	class xmmapfile
	{
	public:
						xmmapfile();
						~xmmapfile();

		void			open(const char* filename, bool readonly);
		void			create(const char* filename, u64 filesize);
		void			close();

		bool			map(u64 offset, u32 size, void*&);
		void			unmap(void*, u32 size);
		void			flush(void*, u32 size);

	private:
		MMFIO*			imp;
		u64				impmem[20];
	};

}
#endif	// __XMMAPFILE_MMAPFILE_H__