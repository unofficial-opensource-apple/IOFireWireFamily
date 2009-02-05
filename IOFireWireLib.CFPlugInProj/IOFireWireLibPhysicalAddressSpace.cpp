/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *  IOFireWireLibPhysicalAddressSpace.cpp
 *  IOFireWireLib
 *
 *  Created by NWG on Tue Dec 12 2000.
 *  Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 */

#import "IOFireWireLibPhysicalAddressSpace.h"
#import "IOFireWireLibDevice.h"
#import <exception>

namespace IOFireWireLib {
	
	// COM
	
	PhysicalAddressSpace::Interface PhysicalAddressSpace::sInterface =
	{
		INTERFACEIMP_INTERFACE,
		1, 0,	//vers, rev
		& PhysicalAddressSpace::SGetPhysicalSegments,
		& PhysicalAddressSpace::SGetPhysicalSegment,
		& PhysicalAddressSpace::SGetPhysicalAddress,
		
		SGetFWAddress,
		SGetBuffer,
		SGetBufferSize
	} ;
	
	HRESULT
	PhysicalAddressSpace::QueryInterface(REFIID iid, void **ppv)
	{
		HRESULT		result = S_OK ;
		*ppv = nil ;
	
		CFUUIDRef	interfaceID	= CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, iid) ;
	
		if ( CFEqual(interfaceID, IUnknownUUID) ||  CFEqual(interfaceID, kIOFireWirePhysicalAddressSpaceInterfaceID) )
		{
			*ppv = & GetInterface() ;
			AddRef() ;
		}
		else
		{
			*ppv = nil ;
			result = E_NOINTERFACE ;
		}	
		
		CFRelease(interfaceID) ;
		return result ;
	}
	
	IUnknownVTbl**
	PhysicalAddressSpace::Alloc(
		Device& 	inUserClient,
		UserObjectHandle		inAddrSpaceRef,
		UInt32		 					inSize, 
		void* 							inBackingStore, 
		UInt32 							inFlags)
	{
		PhysicalAddressSpace* me = nil ;
		try {
			me = new PhysicalAddressSpace(inUserClient, inAddrSpaceRef, inSize, inBackingStore, inFlags) ;
		} catch (...) {
		}

		return ( nil == me ) ? nil : reinterpret_cast<IUnknownVTbl**>(& me->GetInterface()) ;
	}
	
#pragma mark -
	void
	PhysicalAddressSpace::SGetPhysicalSegments(
		IOFireWireLibPhysicalAddressSpaceRef self,
		UInt32*				ioSegmentCount,
		IOByteCount			outSegments[],
		IOPhysicalAddress	outAddresses[])
	{
		IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->GetPhysicalSegments(ioSegmentCount, outSegments, outAddresses) ;
	}
	
	IOPhysicalAddress
	PhysicalAddressSpace::SGetPhysicalSegment(
		IOFireWireLibPhysicalAddressSpaceRef self,
		IOByteCount 		offset,
		IOByteCount*		length)
	{
		return IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->GetPhysicalSegment(offset, length) ;
	}
	
	IOPhysicalAddress
	PhysicalAddressSpace::SGetPhysicalAddress(
		IOFireWireLibPhysicalAddressSpaceRef self)
	{
		return IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->mSegments[0] ;
	}
	
	void
	PhysicalAddressSpace::SGetFWAddress(IOFireWireLibPhysicalAddressSpaceRef self, FWAddress* outAddr )
	{
		bcopy(& IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->mFWAddress, outAddr, sizeof(*outAddr)) ;
	}
	
	void*
	PhysicalAddressSpace::SGetBuffer(IOFireWireLibPhysicalAddressSpaceRef self)
	{
		return IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->mBackingStore ;
	}
	
	const UInt32
	PhysicalAddressSpace::SGetBufferSize(IOFireWireLibPhysicalAddressSpaceRef self)
	{
		return IOFireWireIUnknown::InterfaceMap<PhysicalAddressSpace>::GetThis(self)->mSize ;
	}
	
#pragma mark -
	PhysicalAddressSpace::PhysicalAddressSpace( Device& inUserClient, UserObjectHandle inKernPhysicalAddrSpaceRef,
				UInt32 inSize, void* inBackingStore, UInt32 inFlags)
	: IOFireWireIUnknown( reinterpret_cast<const IUnknownVTbl &>( sInterface ) ),
	  mUserClient(inUserClient),
	  mKernPhysicalAddrSpaceRef(inKernPhysicalAddrSpaceRef),
	  mSize(inSize),
	  mBackingStore(inBackingStore),
	  mSegments(0),
	  mSegmentLengths(0),
	  mSegmentCount(0)
	{
		inUserClient.AddRef() ;

		if (!mKernPhysicalAddrSpaceRef)
			throw kIOReturnNoMemory ;
		
		IOReturn error = ::IOConnectMethodScalarIScalarO(   mUserClient.GetUserClientConnection(), 
															mUserClient.MakeSelectorWithObject( kPhysicalAddrSpace_GetSegmentCount_d, 
																mKernPhysicalAddrSpaceRef ), 
															0, 1, & mSegmentCount) ;
		if ( error || mSegmentCount == 0)
			throw error ;
			
		mSegments = new IOPhysicalAddress[mSegmentCount] ;
		if (!mSegments)
			throw kIOReturnNoMemory ;

		mSegmentLengths = new IOByteCount[mSegmentCount] ;
		{
			if (!mSegmentLengths)
			{
				delete mSegments ;
				throw kIOReturnNoMemory ;
			}
		}
		
		error = ::IOConnectMethodScalarIScalarO( mUserClient.GetUserClientConnection(), 
						kPhysicalAddrSpace_GetSegments, 4, 1, mKernPhysicalAddrSpaceRef,
						mSegmentCount, mSegments, mSegmentLengths, & mSegmentCount) ;

		if (error)
			throw error ;

		mFWAddress = FWAddress(0, mSegments[0], 0) ;
	}
	
	PhysicalAddressSpace::~PhysicalAddressSpace()
	{
		// call user client to delete our addr space ref here (if not yet released)
		IOConnectMethodScalarIScalarO(  mUserClient.GetUserClientConnection(), 
										kReleaseUserObject, 1, 0, mKernPhysicalAddrSpaceRef ) ;
		
		delete[] mSegments ;
		delete[] mSegmentLengths ;
	
		mUserClient.Release() ;
	}
	
	void
	PhysicalAddressSpace::GetPhysicalSegments(
		UInt32*				ioSegmentCount,
		IOByteCount			outSegmentLengths[],
		IOPhysicalAddress	outSegments[])
	{
		*ioSegmentCount = *ioSegmentCount <? mSegmentCount ;
		
		bcopy(mSegmentLengths, outSegmentLengths, (*ioSegmentCount)*sizeof(UInt32)) ;
		bcopy(mSegments, outSegments, (*ioSegmentCount) * sizeof(IOPhysicalAddress)) ;
	}
	
	IOPhysicalAddress
	PhysicalAddressSpace::GetPhysicalSegment(
		IOByteCount 		offset,
		IOByteCount*		length)
	{
		IOPhysicalAddress result = 0 ;
	
		if (mSegmentCount > 0)
		{		
			IOByteCount traversed = mSegmentLengths[0] ;
			UInt32		currentSegment = 0 ;
			
			while((traversed <= offset) && (currentSegment < mSegmentCount))
			{
				traversed += mSegmentLengths[currentSegment] ;
				++currentSegment ;
			}	
			
			if (currentSegment <= mSegmentCount)
			{
				*length = mSegmentLengths[currentSegment] ;
				result =  mSegments[currentSegment] ;
			}
		}
		
		return result ;
	}
	
}
