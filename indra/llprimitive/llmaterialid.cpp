/** 
* @file llmaterialid.cpp
* @brief Implementation of llmaterialid
* @author Stinson@lindenlab.com
*
* $LicenseInfo:firstyear=2012&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2012, Linden Research, Inc.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation;
* version 2.1 of the License only.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*
* Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
* $/LicenseInfo$
*/

#include "linden_common.h"

#include "llmaterialid.h"

#include <string>

#include "llformat.h"

const LLMaterialID LLMaterialID::null;

LLMaterialID::LLMaterialID(const LLSD& pMaterialID)
{
	llassert(pMaterialID.isBinary());
	parseFromBinary(pMaterialID.asBinary());
}

LLMaterialID::LLMaterialID(const LLSD::Binary& pMaterialID)
{
	parseFromBinary(pMaterialID);
}

LLMaterialID::LLMaterialID(const void* pMemory)
{
	set(pMemory);
}

LLMaterialID::LLMaterialID(const LLUUID& lluid)
{
	set(lluid.mData);
}

const U8* LLMaterialID::get() const
{
	return mID;
}

void LLMaterialID::set(const void* pMemory)
{
	llassert(pMemory != NULL);

	// assumes that the required size of memory is available
	memcpy(mID, pMemory, MATERIAL_ID_SIZE * sizeof(U8));
}

void LLMaterialID::clear()
{
	memset(mID, 0, MATERIAL_ID_SIZE * sizeof(U8));
}

LLSD LLMaterialID::asLLSD() const
{
	LLSD::Binary materialIDBinary;

	materialIDBinary.resize(MATERIAL_ID_SIZE * sizeof(U8));
	memcpy(materialIDBinary.data(), mID, MATERIAL_ID_SIZE * sizeof(U8));

	LLSD materialID = materialIDBinary;
	return materialID;
}

std::string LLMaterialID::asString() const
{
	return fmt::format(FMT_COMPILE("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}"),
		(U8)(mID[0]),
		(U8)(mID[1]),
		(U8)(mID[2]),
		(U8)(mID[3]),
		(U8)(mID[4]),
		(U8)(mID[5]),
		(U8)(mID[6]),
		(U8)(mID[7]),
		(U8)(mID[8]),
		(U8)(mID[9]),
		(U8)(mID[10]),
		(U8)(mID[11]),
		(U8)(mID[12]),
		(U8)(mID[13]),
		(U8)(mID[14]),
		(U8)(mID[15]));
}

std::ostream& operator<<(std::ostream& s, const LLMaterialID &material_id)
{
	s << material_id.asString();
	return s;
}

void LLMaterialID::parseFromBinary (const LLSD::Binary& pMaterialID)
{
	llassert(pMaterialID.size() == (MATERIAL_ID_SIZE * sizeof(U8)));
	memcpy(mID, &pMaterialID[0], MATERIAL_ID_SIZE * sizeof(U8));
}
