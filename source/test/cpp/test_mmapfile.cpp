#include "xbase\x_target.h"
#include "xbase\x_string_std.h"
#include "xbase\x_endian.h"
#include "xmmapfile\xmmapfile.h"

#include "xunittest\xunittest.h"

using namespace xcore;

UNITTEST_SUITE_BEGIN(xmmapfile)
{
	UNITTEST_FIXTURE(main)
	{
		UNITTEST_FIXTURE_SETUP() {}
		UNITTEST_FIXTURE_TEARDOWN() {}

		UNITTEST_TEST(open_close)
		{
			xmmapfile mmf;

			mmf.open("D:/Temp/CTG.zip", true);
			mmf.close();
		}

		UNITTEST_TEST(open_map_unmap_close)
		{
			xmmapfile mmf;

			mmf.open("D:/Temp/CTG.zip", true);

			void* mem;
			u32 size = 100 * 1024;
			CHECK_TRUE(mmf.map(0, size, mem));
			CHECK_NOT_NULL(mem);

			if (mem != NULL) 
			{
				u32 h1 = *(u32*)mem;

				mmf.unmap(mem, size);
			}


			mmf.close();
		}

		UNITTEST_TEST(create_map_unmap_close)
		{
			xmmapfile mmf;

			mmf.create("test.bin", 1024*1024);

			void* mem;
			u32 size = 0;	// map all
			CHECK_TRUE(mmf.map(0, size, mem));
			CHECK_NOT_NULL(mem);

			if (mem != NULL) 
			{
				u32 h1 = *(u32*)mem;

				mmf.unmap(mem, size);
			}


			mmf.close();
		}


	}
}
UNITTEST_SUITE_END