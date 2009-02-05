/*
 * Copyright (c) 1998-2002 Apple Computer, Inc. All rights reserved.
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
 
//#define IOASSERT 1	// Set to 1 to activate assert()

// public
#include <IOKit/firewire/IOFWCommand.h>
#include <IOKit/firewire/IOFireWireController.h>
#include <IOKit/firewire/IOFireWireNub.h>
#include <IOKit/firewire/IOLocalConfigDirectory.h>

// system
#include <IOKit/assert.h>
#include <IOKit/IOSyncer.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommand.h>

OSDefineMetaClassAndStructors(IOFWWriteCommand, IOFWAsyncCommand)
OSMetaClassDefineReservedUnused(IOFWWriteCommand, 0);
OSMetaClassDefineReservedUnused(IOFWWriteCommand, 1);

#pragma mark -

// initWithController
//
//

bool IOFWWriteCommand::initWithController(IOFireWireController *control)
{
	bool success = true;
	
    fWrite = true;
	
    success = IOFWAsyncCommand::initWithController(control);
						  
	// create member variables
	
	if( success )
	{
		success = createMemberVariables();
	}
	
	return success;
}

// initAll
//
//

bool IOFWWriteCommand::initAll(	IOFireWireNub *			device, 
								FWAddress 				devAddress,
								IOMemoryDescriptor *	hostMem, 
								FWDeviceCallback 		completion,
								void *					refcon, 
								bool 					failOnReset )
{
	bool success = true;
	
    fWrite = true;
	
    success = IOFWAsyncCommand::initAll( device, devAddress, hostMem, 
										 completion, refcon, failOnReset);
						  
	// create member variables
	
	if( success )
	{
		success = createMemberVariables();
	}
	
	return success;
}

// initAll
//
//

bool IOFWWriteCommand::initAll(	IOFireWireController *	control,
								UInt32 					generation, 
								FWAddress 				devAddress,
								IOMemoryDescriptor *	hostMem, 
								FWDeviceCallback 		completion, 
								void *					refcon )
{
	bool success = true;
	
    fWrite = true;

    success = IOFWAsyncCommand::initAll(control, generation, devAddress,
										hostMem, completion, refcon);
						  
	// create member variables
	
	if( success )
	{
		success = createMemberVariables();
	}
	
	return success;
}

// createMemberVariables
//
//

bool IOFWWriteCommand::createMemberVariables( void )
{
	bool success = true;
	
	if( fMembers == NULL )
	{
		success = IOFWAsyncCommand::createMemberVariables();
	}
	
	if( fMembers )
	{
		if( success )
		{
			fMembers->fSubclassMembers = IOMalloc( sizeof(MemberVariables) );
			if( fMembers->fSubclassMembers == NULL )
				success = false;
		}
		
		// zero member variables
		
		if( success )
		{
			bzero( fMembers->fSubclassMembers, sizeof(MemberVariables) );
		}
		
		// clean up on failure
		
		if( !success )
		{
			destroyMemberVariables();
		}
	}
	
	return success;
}

// destroyMemberVariables
//
//

void IOFWWriteCommand::destroyMemberVariables( void )
{
	if( fMembers->fSubclassMembers != NULL )
	{		
		// free member variables
		
		IOFree( fMembers->fSubclassMembers, sizeof(MemberVariables) );
		fMembers->fSubclassMembers = NULL;
	}
}

// free
//
//

void IOFWWriteCommand::free()
{	
	destroyMemberVariables();
	
	IOFWAsyncCommand::free();
}

// reinit
//
//

IOReturn IOFWWriteCommand::reinit(	FWAddress 				devAddress,
									IOMemoryDescriptor *	hostMem,
									FWDeviceCallback 		completion, 
									void *					refcon, 
									bool failOnReset )
{
    return IOFWAsyncCommand::reinit(	devAddress,
										hostMem, 
										completion, 
										refcon, 
										failOnReset );
}

// reinit
//
//

IOReturn IOFWWriteCommand::reinit(	UInt32 					generation, 
									FWAddress 				devAddress,
									IOMemoryDescriptor *	hostMem,
									FWDeviceCallback 		completion, 
									void *					refcon )
{
    return IOFWAsyncCommand::reinit(	generation, 
										devAddress,
										hostMem, 
										completion, 
										refcon );
}

// execute
//
//

IOReturn IOFWWriteCommand::execute()
{
    IOReturn result;
    fStatus = kIOReturnBusy;

    if( !fFailOnReset ) 
	{
        // Update nodeID and generation
        fDevice->getNodeIDGeneration( fGeneration, fNodeID );
    }

    fPackSize = fSize;
    if( fPackSize > fMaxPack )
		fPackSize = fMaxPack;

    // Do this when we're in execute, not before,
    // so that Reset handling knows which commands are waiting a response.
    fTrans = fControl->allocTrans( this );
    if( fTrans ) 
	{
		IOFWWriteFlags flags = kIOFWWriteFlagsNone;
		
		if( fMembers && 
			fMembers->fSubclassMembers &&
			((MemberVariables*)fMembers->fSubclassMembers)->fDeferredNotify )
		{
			flags = kIOFWWriteFlagsDeferredNotify;
		}
		
        result = fControl->asyncWrite(	fGeneration, 
										fNodeID, 
										fAddressHi, 
										fAddressLo, 
										fSpeed,
										fTrans->fTCode, 
										fMemDesc, 
										fBytesTransferred, 
										fPackSize, 
										this,
										flags );
    }
    else 
	{
		IOLog("IOFWReadCommand::execute: Out of tLabels?\n");
        result = kIOFireWireOutOfTLabels;
    }

	// complete could release us so protect fStatus with retain and release
	IOReturn status = fStatus;	
    if( result != kIOReturnSuccess )
	{
		retain();
        complete( result );
		status = fStatus;
		release();
	}
	
	return status;
}

// gotPacket
//
//

void IOFWWriteCommand::gotPacket( int rcode, const void* data, int size )
{
    if( rcode != kFWResponseComplete ) 
	{
        complete( kIOFireWireResponseBase+rcode );
        return;
    }
    else 
	{
		fBytesTransferred += fPackSize;
        fSize -= fPackSize;
    }

    if( fSize > 0 ) 
	{
        fAddressLo += fPackSize;

        updateTimer();
        fCurRetries = fMaxRetries;
        fControl->freeTrans( fTrans );  // Free old tcode
        execute();
    }
    else 
	{
        complete( kIOReturnSuccess );
    }
}
