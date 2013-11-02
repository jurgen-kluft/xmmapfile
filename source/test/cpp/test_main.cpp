#include "xbase\x_target.h"
#include "xbase\x_allocator.h"
#include "xbase\x_string_std.h"

#include "xunittest\xunittest.h"
#include "xbase\x_console.h"

UNITTEST_SUITE_LIST(xMMapFileUnitTest);
UNITTEST_SUITE_DECLARE(xMMapFileUnitTest, xmmapfile);

class UnitTestAllocator : public UnitTest::Allocator
{
public:
	xcore::x_iallocator*	mAllocator;
	int						mNumAllocations;

	UnitTestAllocator(xcore::x_iallocator* allocator)
		: mNumAllocations(0)
	{
		mAllocator = allocator;
	}

	void*	Allocate(int size)
	{
		++mNumAllocations;
		return mAllocator->allocate(size, 4);
	}
	void	Deallocate(void* ptr)
	{
		--mNumAllocations;
		mAllocator->deallocate(ptr);
	}
	void	Release()
	{
		mAllocator->release();
	}
};

bool gRunUnitTest(UnitTest::TestReporter& reporter)
{
	UnitTestAllocator unittestAllocator( xcore::gCreateSystemAllocator() );
	UnitTest::SetAllocator(&unittestAllocator);

	xcore::xconsole::addDefault();

	int r = UNITTEST_SUITE_RUN(reporter, xMMapFileUnitTest);
	if (unittestAllocator.mNumAllocations!=0)
	{
		reporter.reportFailure(__FILE__, __LINE__, "xunittest", "memory leaks detected!");
		r = -1;
	}

	unittestAllocator.Release();
	return r==0;
}
