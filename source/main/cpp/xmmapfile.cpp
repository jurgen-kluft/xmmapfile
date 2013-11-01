#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xmmapfile/xmmapfile.h"

#ifdef TARGET_PC

// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER				// Allow use of features specific to Windows XP or later.
#define WINVER 0x0501		// Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif						

#ifndef _WIN32_WINDOWS		// Allow use of features specific to Windows 98 or later.
#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
#endif

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <stdio.h>
#include <tchar.h>

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers
#endif

#include <Windows.h>

namespace xcore
{
	/* Points of interest
		The Windows SDK function WriteFile allows the caller to write beyond the EOF by extending 
		the file size as necessary. This facility is not available directly in memory mapped I/O. 
		In order to write and extend the file beyond the current EOF, the view needs to be unmapped 
		and the mapping object must be closed. After this, extend the file size (say by 64K) using 
		the SetEndOfFile function, recreate the mapping object, and remap the file. 
		The file must be restored to its actual length whenever the write operation completes.
	*/

	class MMFIO
	{
	public:
		xbyte			m_cRefCount;
		HANDLE			m_hFileMapping;
		xbyte*			m_pbFile;
		u64				m_dwBytesInView;
		u64				mFileSize;
		u64				m_nViewBegin;//from beginning of file
		u64				m_nCurPos;//from beginning of file
		HANDLE			m_hFile;
		u32				m_dwAllocGranularity;
		LONG			m_lExtendOnWriteLength;
		u32				m_dwflagsFileMapping;
		u32				m_dwflagsView;
		bool			m_bFileExtended;
		char const*		m_strErrMsg;

		bool			_open(const char* strfile, bool readonly);
		void			_flush(void* p, u64 size);
		bool			_checkFileExtended();
		bool			_seek(u64 lOffset);
		void			_close();

	public:
						MMFIO();
						~MMFIO();

		/* Construction */
		bool			OpenReadOnly(const char* strfile);
		bool			OpenReadWrite(const char* strfile);
		bool			Close();

		/* I/O */
		u64				Read(void* pBuf, u64 nCount);
		u64				Write(void* pBuf, u64 nCount);

		/* Position */
		bool			Seek(u64 lOffset);
		u64				GetPosition();

		/* Length */
		u64				GetLength();
		bool			SetLength(const u64& nLength);

		/*error*/
		void			GetMMFLastError(const char* strErr)		{ strErr = m_strErrMsg; }

		XCORE_CLASS_PLACEMENT_NEW_DELETE
	};


	// MMFIO.cpp : Defines the entry point for the console application.
	//

	#ifdef _DEBUG
	#define new DEBUG_NEW
	#endif

	#define MMF_ERR_ZERO_BYTE_FILE           _T("Cannot open zero byte file.")
	#define MMF_ERR_INVALID_SET_FILE_POINTER _T("The file pointer cannot be set to specified location.")
	#define MMF_ERR_WRONG_OPEN               _T("Close previous file before opening another.")
	#define MMF_ERR_OPEN_FILE                _T("Error encountered during file open.")
	#define MMF_ERR_CREATEFILEMAPPING        _T("Failed to create file mapping object.")
	#define MMF_ERR_MAPVIEWOFFILE            _T("Failed to map view of file.")
	#define MMF_ERR_SETENDOFFILE             _T("Failed to set end of file.")
	#define MMF_ERR_INVALIDSEEK              _T("Seek request lies outside file boundary.")
	#define MMF_ERR_WRONGSEEK                _T("Offset must be negative while seeking from file end.");

	//~~~ MMFIO implementation ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	MMFIO::MMFIO()
	{
		SYSTEM_INFO sinf;
		GetSystemInfo(&sinf);
		m_dwAllocGranularity = sinf.dwAllocationGranularity;
		m_lExtendOnWriteLength = m_dwAllocGranularity;

		m_dwBytesInView = m_dwAllocGranularity;
		m_nCurPos = 0;
		m_nViewBegin = 0;
		m_pbFile = 0;
		m_hFileMapping = INVALID_HANDLE_VALUE;
		m_cRefCount = 0;
		m_bFileExtended = false;
	}

	void MMFIO::_close()
	{
		_checkFileExtended();

		if(m_pbFile)
		{
			//unmap view 
			FlushViewOfFile(m_pbFile, 0);
			UnmapViewOfFile(m_pbFile);
			m_pbFile = NULL;
		}

		if(m_hFileMapping)
		{
			//close mapping object handle
			CloseHandle(m_hFileMapping);
			m_hFileMapping = NULL;
		}

		if(m_hFile)
		{
			CloseHandle(m_hFile);
			m_hFile = NULL;
		}
	}

	MMFIO::~MMFIO()
	{
		_close();
	}

	bool MMFIO::_open(const char* strfile, bool readonly)
	{
	#ifdef _DEBUG
		ASSERT(m_cRefCount == 0);
	#endif

		if(m_cRefCount!=0)
		{
			m_strErrMsg = MMF_ERR_WRONG_OPEN;
			return false;
		}

		m_cRefCount++;

		// Note:
		// One might think it's a good idea to pass in FILE_FLAG_RANDOM_ACCESS.
		// It turns out that it isn't. That flag will break your operating system:
		//   http://support.microsoft.com/kb/2549369

		// Open the data file.
		u32 dwflags;
		dwflags = (readonly) ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE;
		m_hFile = CreateFile(strfile, dwflags, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

		HANDLE hFile=m_hFile;

		if (INVALID_HANDLE_VALUE == hFile)
		{
			m_strErrMsg = MMF_ERR_OPEN_FILE;
			m_cRefCount = 0;
			return false;
		}
		
		unsigned long dwFileSizeHigh;
		mFileSize = GetFileSize(hFile, &dwFileSizeHigh);
		mFileSize += (((s64) dwFileSizeHigh) << 32);

		if (mFileSize == 0)
		{
			CloseHandle(hFile);
			m_strErrMsg = MMF_ERR_ZERO_BYTE_FILE;
			m_cRefCount = 0;
			return false;
		}

		// Create the file-mapping object.
		m_dwflagsFileMapping = (readonly) ? PAGE_READONLY : PAGE_READWRITE;
		m_hFileMapping = CreateFileMapping(hFile, NULL, m_dwflagsFileMapping, 0, 0, 0);

		if (NULL == m_hFileMapping)
		{
			if(INVALID_HANDLE_VALUE == hFile)
				CloseHandle(hFile);
			m_strErrMsg = MMF_ERR_CREATEFILEMAPPING;
			m_cRefCount = 0;
			return false;
		}

		if (m_dwBytesInView > mFileSize)
			m_dwBytesInView = mFileSize;

		m_dwflagsView = FILE_MAP_ALL_ACCESS;
		m_pbFile = (xbyte*)MapViewOfFile(m_hFileMapping, m_dwflagsView, 0, 0, (SIZE_T)m_dwBytesInView);

		if (NULL == m_pbFile)
		{
			CloseHandle(m_hFileMapping);
			m_strErrMsg = MMF_ERR_MAPVIEWOFFILE;
			m_cRefCount = 0;
			return false;
		}

		m_nCurPos = 0;
		m_nViewBegin = 0;
		m_bFileExtended = false;

		return true;
	}

	bool MMFIO::OpenReadOnly(const char* strfile)
	{
		return _open(strfile, true);
	}

	bool MMFIO::OpenReadWrite(const char* strfile)
	{
		return _open(strfile, false);
	}


	bool MMFIO::Close()
	{
	#ifdef _DEBUG
		ASSERT(m_cRefCount == 1);
	#endif

		m_cRefCount--;
		_close();
		return true;
	}


	u64 MMFIO::Read(void* pBuf, u64 nCountIn)
	{
		if(nCountIn ==0)
			return 0;

		_checkFileExtended();

		if(m_nCurPos >= mFileSize)
			return 0;

		u64 nCount = nCountIn;//int is used to detect any bug

		m_dwBytesInView = m_dwAllocGranularity;
		//check if m_nViewBegin+m_dwBytesInView crosses file size
		if(m_nViewBegin + m_dwBytesInView > mFileSize)
		{
			m_dwBytesInView = mFileSize - m_nViewBegin;
		}

		u64 nDataEndPos = m_nCurPos + nCount;
		if(nDataEndPos >= mFileSize)
		{
			nDataEndPos = mFileSize;
			nCount = mFileSize - m_nCurPos;
		}

		ASSERT(nCount >= 0);		//nCount is int, if -ve then error

		u64 nViewEndPos = m_nViewBegin + m_dwBytesInView;

		if(nDataEndPos < nViewEndPos)
		{
			memcpy_s(pBuf, (u32)nCountIn, m_pbFile + (m_nCurPos-m_nViewBegin), (u32)nCount);
			m_nCurPos += nCount;
		}
		else if(nDataEndPos == nViewEndPos)
		{
			//Last exact bytes are read from the view
			memcpy_s(pBuf, (u32)nCountIn, m_pbFile + (m_nCurPos-m_nViewBegin), (u32)nCount);
			m_nCurPos += nCount;

			_seek(m_nCurPos);
			nViewEndPos = m_nViewBegin + m_dwBytesInView;
		}
		else
		{
			LPBYTE pBufRead = (LPBYTE)pBuf;
			if(nDataEndPos > nViewEndPos)
			{
				//nDataEndPos can span multiple view blocks
				while(m_nCurPos < nDataEndPos)
				{
					u64 nReadBytes = nViewEndPos - m_nCurPos;

					if(nViewEndPos > nDataEndPos)
						nReadBytes = nDataEndPos - m_nCurPos;

					memcpy_s(pBufRead, (u32)nCountIn, m_pbFile + (m_nCurPos-m_nViewBegin), (u32)nReadBytes);
					pBufRead += nReadBytes;

					m_nCurPos += nReadBytes;
					//seeking does view remapping if m_nCurPos crosses view boundary
					_seek(m_nCurPos);
					nViewEndPos = m_nViewBegin + m_dwBytesInView;
				}
			}
		}

		return nCount;
	}

	bool MMFIO::SetLength(const u64& nLength)
	{
		//unmap view 
		UnmapViewOfFile(m_pbFile);
		//close mapping object handle
		CloseHandle(m_hFileMapping);

		LONG nLengthHigh = (nLength >> 32);
		u32 dwPtrLow = SetFilePointer(m_hFile, (LONG) (nLength & 0xFFFFFFFF), &nLengthHigh, FILE_BEGIN);

		if(INVALID_SET_FILE_POINTER == dwPtrLow && GetLastError() != NO_ERROR)
		{
			m_strErrMsg = MMF_ERR_INVALID_SET_FILE_POINTER;
			return false;
		}
		//set the eof to the file pointer position
		if(SetEndOfFile(m_hFile) == 0)
		{
			m_strErrMsg = MMF_ERR_SETENDOFFILE;
			return false;
		}

		mFileSize = nLength;

		//call CreateFileMapping 
		m_hFileMapping = CreateFileMapping(m_hFile, NULL, m_dwflagsFileMapping, 0, 0, _T("SMP"));

		//remap here
		m_pbFile = (xbyte*)MapViewOfFile(m_hFileMapping, m_dwflagsView, (u32) (m_nViewBegin >> 32), (u32) (m_nViewBegin & 0xFFFFFFFF), (u32)m_dwBytesInView);

		return true;
	}

	u64 MMFIO::Write(void* pBuf, u64 nCount)
	{
		if(nCount == 0)
			return 0;

		u64 nViewEndPos = m_nViewBegin + m_dwBytesInView;
		u64 nDataEndPos = m_nCurPos + nCount;

		if(nDataEndPos > nViewEndPos)
		{
			if(nDataEndPos >= mFileSize)
			{
				//Extend the end position by m_lExtendOnWriteLength bytes
				s64 nNewFileSize = nDataEndPos + m_lExtendOnWriteLength;

				if(SetLength(nNewFileSize))
				{
					m_bFileExtended = true;
				}
			}

			LPBYTE pBufWrite = (LPBYTE)pBuf;
			while(m_nCurPos < nDataEndPos)
			{
				u64 nWriteBytes = nViewEndPos - m_nCurPos;

				if(nViewEndPos > nDataEndPos)
					nWriteBytes = nDataEndPos - m_nCurPos;

				memcpy_s(&m_pbFile[m_nCurPos-m_nViewBegin], (u32)m_dwBytesInView, pBufWrite, (u32)nWriteBytes);
				pBufWrite += nWriteBytes;

				m_nCurPos += nWriteBytes;
				//seeking does view remapping if m_nCurPos crosses view boundary
				_seek(m_nCurPos);
				nViewEndPos = m_nViewBegin + m_dwBytesInView;
			}
		}
		else
		{
			//nCount bytes lie within the current view
			memcpy_s(&m_pbFile [m_nCurPos-m_nViewBegin], (u32)nCount, pBuf, (u32)nCount);
			m_nCurPos += nCount;
		}

		return nCount;
	}

	void MMFIO::_flush(void* p, u64 size)
	{
		if (m_cRefCount != 0)
		{
			SIZE_T sizet = (SIZE_T)size;
			FlushViewOfFile(p, sizet);
		}
	}

	bool MMFIO::Seek(u64 lOffset)
	{
		_checkFileExtended();
		bool bRet = _seek(lOffset);
		return bRet;
	}

	bool MMFIO::_seek(u64 lOffset)
	{
		//lOffset must be less than the file size
		if(!(lOffset >= 0 && lOffset < mFileSize))
		{
			m_strErrMsg = MMF_ERR_INVALIDSEEK;
			return false;
		}

		if(!(lOffset >= m_nViewBegin && lOffset < m_nViewBegin + m_dwBytesInView))
		{
			//lOffset lies outside the mapped view, remap the view
			u64 _N = (u64)((double)lOffset/((double)m_dwAllocGranularity));
			m_nViewBegin = _N*m_dwAllocGranularity;
			m_dwBytesInView = m_dwAllocGranularity;
			//check if m_nViewBegin+m_dwBytesInView crosses filesize
			if(m_nViewBegin + m_dwBytesInView > mFileSize)
			{
				m_dwBytesInView = mFileSize - m_nViewBegin;
			}
			if(m_dwBytesInView != 0 && m_pbFile)
			{
				//Unmap old view
				UnmapViewOfFile(m_pbFile);
				//Remap with new starting address
				m_pbFile = (xbyte*)MapViewOfFile(m_hFileMapping, m_dwflagsView, (u32) (m_nViewBegin >> 32), (u32) (m_nViewBegin & 0xFFFFFFFF), (u32)m_dwBytesInView);

				//u32 err = GetLastError();
			}
		}

		m_nCurPos = lOffset;
		return true;
	}

	u64 MMFIO::GetLength()
	{
		_checkFileExtended();
		return mFileSize;
	}

	u64 MMFIO::GetPosition()
	{
		return m_nCurPos;
	}

	/*
	If file is extended in Write function then this must be called to re-adjust
	the file to its actual length before Seek or Read or any such function.
	*/
	bool MMFIO::_checkFileExtended()
	{
		bool bRet = true;
		if(m_bFileExtended)
		{
			//remove extra bytes
			bRet = SetLength(m_nCurPos);
		}
		m_bFileExtended = false;
		return bRet;
	}

	#undef new 

	xmmemro::xmmemro()
		: imp(0)
	{
		void* p = impmem;
		imp = new (p) MMFIO();
	}

	xmmemro::~xmmemro()
	{
		imp->~MMFIO();
	}

	void			xmmemro::open(const char* filename)
	{
		imp->OpenReadWrite(filename);
	}

	void			xmmemro::open(const char* filename, u64 memsize)
	{
		imp->OpenReadWrite(filename);
		imp->SetLength(memsize);
	}

	void			xmmemro::close()
	{
		imp->Close();
	}

	void const*		xmmemro::mem(u64& size) const
	{
		size = imp->m_dwBytesInView;
		return imp->m_pbFile;
	}


	xmmemrw::xmmemrw()
		: imp(0)
	{
		void* p = impmem;
		imp = new (p) MMFIO();
	}

	xmmemrw::~xmmemrw()
	{
		imp->~MMFIO();
	}

	void			xmmemrw::open(const char* filename)
	{
		imp->OpenReadWrite(filename);
	}

	void			xmmemrw::open(const char* filename, u64 memsize)
	{
		imp->OpenReadWrite(filename);
		imp->SetLength(memsize);
	}

	void			xmmemrw::flush(void* base, u64 size)
	{
		imp->_flush(base, size);
	}

	void			xmmemrw::close()
	{
		imp->Close();
	}

	void*			xmmemrw::mem(u64& size)
	{
		size = imp->m_dwBytesInView;
		return imp->m_pbFile;
	}

}


#endif