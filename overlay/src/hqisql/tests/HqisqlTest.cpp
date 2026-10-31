/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by IBSurgeon
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2026 IBSurgeon
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "firebird/Interface.h"
#include "../common/gdsassert.h"

#define BOOST_TEST_MODULE HqisqlTest
#include "boost/test/included/unit_test.hpp"


int ISC_EXPORT fb_shutdown(unsigned int, const int)
{
	fb_assert(false);
	return 0;
}

namespace Firebird
{
	IMaster* API_ROUTINE fb_get_master_interface()
	{
		fb_assert(false);
		return nullptr;
	}
}
