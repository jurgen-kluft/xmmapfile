// xmmapfile.h - Memory Mapped File
#ifndef __XMMAPFILE_MMAPFILE_H__
#define __XMMAPFILE_MMAPFILE_H__
#include "xbase\x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase\x_types.h"

namespace xcore
{
	// Memory mapped to a file on disk
	class xmmem
	{
	public:
		void		open(const char* filename);
		void		open(const char* filename, u32 memsize);
		void		flush();
		void		close();

		void*		memptr();
		void		resize(u32 newsize);
	};

}
#endif	// __XMMAPFILE_MMAPFILE_H__