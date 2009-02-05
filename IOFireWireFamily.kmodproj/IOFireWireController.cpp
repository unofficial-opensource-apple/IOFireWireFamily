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
/*
 * Copyright (c) 1999-2002 Apple Computer, Inc.  All rights reserved.
 *
 * HISTORY
 * 27 April 99 wgulland created.
 *
 */

// public
#import <IOKit/firewire/IOFWUtils.h>
#import <IOKit/firewire/IOFireWireController.h>
#import <IOKit/firewire/IOLocalConfigDirectory.h>
#import <IOKit/firewire/IOFireWireNub.h>
#import <IOKit/firewire/IOFireWireDevice.h>
#import <IOKit/firewire/IOFWLocalIsochPort.h>
#import <IOKit/firewire/IOFWDCLProgram.h>
#import <IOKit/firewire/IOFireWirePowerManager.h>

// protected
#import <IOKit/firewire/IOFWWorkLoop.h>
#import <IOKit/firewire/IOFireWireLink.h>

// private
#import "FWDebugging.h"
#import "IOFireWireLocalNode.h"
#import "IOFWQEventSource.h"
#import "IOFireWireIRM.h"

#if FIRELOGCORE
#import "IOFireLogPriv.h"
#endif

// system
#import <IOKit/IOKitKeys.h>
#import <IOKit/IOBufferMemoryDescriptor.h>
#import <IOKit/IODeviceTreeSupport.h>
#import <IOKit/IOMessage.h>
#import <IOKit/IOTimerEventSource.h>

///////////////////////////////////////////////////////////////////////////////////
// timing constants
//

// 100 mSec delay after bus reset before scanning bus
// to make first generation Sony cameras happy
#define kScanBusDelay				100	

// 1000 mSec delay for a bus reset to occur after one was requested
#define kBusResetTimeout			1000

// 100 mSec delay for self ids to arrive after a bus reset
#define kSelfIDTimeout				1000

// 1000 mSec delay before pruning devices
// this will end up being kNormalDevicePruneDelay + kRepeatResetDelay because of gap count optimization
#define kNormalDevicePruneDelay		1000

// 2000 mSec delay between bus resets
// from the 1394a spec
#define kRepeatResetDelay			2000

// 3000 mSec delay before pruning last device 
// should generally equal kNormalDevicePruneDelay + kRepeatResetDelay
#define kOnlyNodeDevicePruneDelay	3000

// 15000 mSec delay before pruning devices after wake
// needs to be at least long enough for the iPod to reboot into disk mode
#define kWakeDevicePruneDelay		15000

///////////////////////////////////////////////////////////////////////////////////

#define kFireWireGenerationID		"FireWire Generation ID"
#define kFWBusScanInProgress		"-1"

#define FWAddressToID(addr) (addr & 63)

enum requestRefConBits 
{
    kRequestLabel = kFWAsynchTTotal-1,	// 6 bits
    kRequestExtTCodeShift = 6,
    kRequestExtTCodeMask = 0x3fffc0,	// 16 bits
    kRequestIsComplete = 0x20000000,	// Was write request already ack-complete?
    kRequestIsLock = 0x40000000,
    kRequestIsQuad = 0x80000000
};

const OSSymbol *gFireWireROM;
const OSSymbol *gFireWireNodeID;
const OSSymbol *gFireWireSelfIDs;
const OSSymbol *gFireWireUnit_Spec_ID;
const OSSymbol *gFireWireUnit_SW_Version;
const OSSymbol *gFireWireVendor_ID;
const OSSymbol *gFireWire_GUID;
const OSSymbol *gFireWireSpeed;
const OSSymbol *gFireWireVendor_Name;
const OSSymbol *gFireWireProduct_Name;
const OSSymbol *gFireWireModel_ID;

const IORegistryPlane * IOFireWireBus::gIOFireWirePlane = NULL;

// FireWire bus has two power states, off and on
#define number_of_power_states 2

// Note: This defines two states. off and on.
static IOPMPowerState ourPowerStates[number_of_power_states] = 
{
	{1,0,0,0,0,0,0,0,0,0,0,0},
	{1,IOPMDeviceUsable,IOPMPowerOn,IOPMPowerOn,0,0,0,0,0,0,0,0}
};

OSDefineMetaClassAndStructors(IOFireWireControllerAux, IOFireWireBusAux);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 0);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 1);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 2);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 3);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 4);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 5);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 6);
OSMetaClassDefineReservedUnused(IOFireWireControllerAux, 7);

#pragma mark -

// init
//
//

bool IOFireWireControllerAux::init( IOFireWireController * primary )
{
	bool success = true;		// assume success

	success = IOFireWireBusAux::init();
	
	fPrimary = primary;
	
	return success;
}

// free
//
//

void IOFireWireControllerAux::free()
{	    
	IOFireWireBusAux::free();
}

IOFWDCLPool *
IOFireWireControllerAux :: createDCLPool ( unsigned capacity ) const
{
	return fPrimary->getLink()->createDCLPool( capacity ) ;
}

#pragma mark -

OSDefineMetaClassAndStructors( IOFireWireController, IOFireWireBus )
OSMetaClassDefineReservedUnused(IOFireWireController, 0);
OSMetaClassDefineReservedUnused(IOFireWireController, 1);
OSMetaClassDefineReservedUnused(IOFireWireController, 2);
OSMetaClassDefineReservedUnused(IOFireWireController, 3);
OSMetaClassDefineReservedUnused(IOFireWireController, 4);
OSMetaClassDefineReservedUnused(IOFireWireController, 5);
OSMetaClassDefineReservedUnused(IOFireWireController, 6);
OSMetaClassDefineReservedUnused(IOFireWireController, 7);
OSMetaClassDefineReservedUnused(IOFireWireController, 8);

#pragma mark -

/////////////////////////////////////////////////////////////////////////////

// init
//
//

bool IOFireWireController::init( IOFireWireLink *fwim )
{
    if( !IOFireWireBus::init() )
        return false;

	fAuxiliary = createAuxiliary();
	if( fAuxiliary == NULL )
		return false;
		
    fFWIM = fwim;
    
    // Create firewire symbols.
    gFireWireROM = OSSymbol::withCString("FireWire Device ROM");
    gFireWireNodeID = OSSymbol::withCString("FireWire Node ID");
    gFireWireSelfIDs = OSSymbol::withCString("FireWire Self IDs");
    gFireWireUnit_Spec_ID = OSSymbol::withCString("Unit_Spec_ID");
    gFireWireUnit_SW_Version = OSSymbol::withCString("Unit_SW_Version");
    gFireWireVendor_ID = OSSymbol::withCString("Vendor_ID");
    gFireWire_GUID = OSSymbol::withCString("GUID");
    gFireWireSpeed = OSSymbol::withCString("FireWire Speed");
    gFireWireVendor_Name = OSSymbol::withCString("FireWire Vendor Name");
    gFireWireProduct_Name = OSSymbol::withCString("FireWire Product Name");
	gFireWireModel_ID = OSSymbol::withCString("Model_ID");

    if(NULL == gIOFireWirePlane) 
	{
        gIOFireWirePlane = IORegistryEntry::makePlane( kIOFireWirePlane );
    }

    fLocalAddresses = OSSet::withCapacity(5);	// Local ROM + CSR registers + SBP-2 ORBs + FCP + PCR
    if(fLocalAddresses)
        fSpaceIterator =  OSCollectionIterator::withCollection(fLocalAddresses);

    fAllocatedChannels = OSSet::withCapacity(1);	// DV channel.
    if(fAllocatedChannels)
        fAllocChannelIterator =  OSCollectionIterator::withCollection(fAllocatedChannels);

    fLastTrans = kMaxPendingTransfers-1;
	fDevicePruneDelay = kNormalDevicePruneDelay;

    UInt32 bad = 0xdeadbabe;
    fBadReadResponse = IOBufferMemoryDescriptor::withBytes(&bad, sizeof(bad), kIODirectionOutIn);

    fDelayedStateChangeCmdNeedAbort = false;
    fDelayedStateChangeCmd = createDelayedCmd(1000 * kScanBusDelay, delayedStateChange, NULL);

	fBusResetStateChangeCmd = createDelayedCmd(1000 * kRepeatResetDelay, resetStateChange, NULL);

	//
	// create the bus power manager
	//
	
	fBusPowerManager = IOFireWirePowerManager::createWithController( this );
	if( fBusPowerManager == NULL )
	{
		IOLog( "IOFireWireController::start - failed to create bus power manager!\n" );
		return false;
	}
	
    return (gFireWireROM != NULL &&  gFireWireNodeID != NULL &&
        gFireWireUnit_Spec_ID != NULL && gFireWireUnit_SW_Version != NULL && 
	fLocalAddresses != NULL && fSpaceIterator != NULL &&
            fAllocatedChannels != NULL && fAllocChannelIterator != NULL &&
            fBadReadResponse != NULL);
}

// createAuxiliary
//
// virtual method for creating auxiliary object.  subclasses needing to subclass 
// the auxiliary object can override this.

IOFireWireBusAux * IOFireWireController::createAuxiliary( void )
{
	IOFireWireControllerAux * auxiliary;
    
	auxiliary = new IOFireWireControllerAux;

    if( auxiliary != NULL && !auxiliary->init(this) ) 
	{
        auxiliary->release();
        auxiliary = NULL;
    }
	
    return auxiliary;
}

// free
//
//

void IOFireWireController::free()
{
    // Release everything I can think of.

	if( fIRM != NULL )
	{
		fIRM->release();
		fIRM = NULL;
	}
	
	if( fBusPowerManager != NULL )
	{
		fBusPowerManager->release();
		fBusPowerManager = NULL;
	}
	
    if( fROMAddrSpace != NULL ) 
	{
        fROMAddrSpace->release();
		fROMAddrSpace = NULL;
	}

    if( fRootDir != NULL )
    {
	    fRootDir->release();
		fRootDir = NULL;
	}
	    
    if( fBadReadResponse != NULL )
    {
	    fBadReadResponse->release();
		fBadReadResponse = NULL;
	}
	    
    if( fDelayedStateChangeCmd != NULL )
	{
        fDelayedStateChangeCmd->release();
		fDelayedStateChangeCmd = NULL;
	}
	
    if( fBusResetStateChangeCmd != NULL )
	{
        fBusResetStateChangeCmd->release();
		fBusResetStateChangeCmd = NULL;
	}
	
    if( fSpaceIterator != NULL ) 
	{
        fSpaceIterator->release();
		fSpaceIterator = NULL;
    }
        
    if( fLocalAddresses != NULL )
	{
        fLocalAddresses->release();
		fLocalAddresses = NULL;
	}
	
    if( fAllocChannelIterator != NULL ) 
	{
        fAllocChannelIterator->release();
		fAllocChannelIterator = NULL;
	}

    if( fAllocatedChannels != NULL )
	{
        fAllocatedChannels->release();
		fAllocatedChannels = NULL;
    }
	
	destroyTimeoutQ();
	destroyPendingQ();
	
	if( fWorkLoop != NULL )
	{
		fWorkLoop->release();
		fWorkLoop = NULL;
	}
	
	if( fAuxiliary != NULL )
	{
		fAuxiliary->release();
		fAuxiliary = NULL;
	}
	
    IOFireWireBus::free();
}

// start
//
//

bool IOFireWireController::start(IOService *provider)
{
    IOReturn res;

    if (!IOService::start(provider))
    {
	    return false;
    }
	
    CSRNodeUniqueID guid = fFWIM->getGUID();
    
    // blow away device tree children from where we've taken over
    // Note we don't add ourself to the device tree.
    IOService *parent = this;
    while(parent) {
        if(parent->inPlane(gIODTPlane))
            break;
        parent = parent->getProvider();
    }
    if(parent) {
        IORegistryEntry *    child;
        IORegistryIterator * children;

        children = IORegistryIterator::iterateOver(parent, gIODTPlane);
        if ( children != 0 ) {
            // Get all children before we start altering the plane!
            OSOrderedSet * set = children->iterateAll();
            if(set != 0) {
                OSIterator *iter = OSCollectionIterator::withCollection(set);
                if(iter != 0) {
                    while ( (child = (IORegistryEntry *)iter->getNextObject()) ) {
                        child->detachAll(gIODTPlane);
                    }
                    iter->release();
                }
                set->release();
            }
            children->release();
        }
    }

    fWorkLoop = fFWIM->getFireWireWorkLoop();
    fWorkLoop->retain();	// make sure workloop lives at least as long as we do.

	// workloop must be set up before creating queues
	createPendingQ();
	createTimeoutQ();
	
	//
	// setup initial security mode and state change notification
	//
	
	initSecurity();
	
    // Build local device ROM
    // Allocate address space for Configuration ROM and fill in Bus Info
    // block.
    fROMHeader[1] = kFWBIBBusName;
    fROMHeader[2] = fFWIM->getBusCharacteristics();

    // Zero out generation
    fROMHeader[2] &= ~kFWBIBGeneration;

    // Get max speed
    fMaxRecvLog = ((fROMHeader[2] & kFWBIBMaxRec) >> kFWBIBMaxRecPhase)+1;
    fMaxSendLog = fFWIM->getMaxSendLog();
    fROMHeader[3] = guid >> 32;
    fROMHeader[4] = guid & 0xffffffff;

    // Create root directory in FWIM data.//zzz should we have one for each FWIM or just one???
    fRootDir = IOLocalConfigDirectory::create();
    if(!fRootDir)
	{
        return false;
	}
	
    // Set our Config ROM generation.
    fRootDir->addEntry(kConfigGenerationKey, (UInt32)1);
    // Set our module vendor ID.
    fRootDir->addEntry(kConfigModuleVendorIdKey, 0x000a27, OSString::withCString("Apple Computer, Inc."));
    fRootDir->addEntry(kConfigModelIdKey, 10, OSString::withCString("Macintosh"));
    // Set our capabilities.
    fRootDir->addEntry(kConfigNodeCapabilitiesKey, 0x000083C0);

    // Set our node unique ID.
    OSData *t = OSData::withBytes(&guid, sizeof(guid));
    fRootDir->addEntry(kConfigNodeUniqueIdKey, t);
        
    fTimer->enable();

    // Create local node
    IOFireWireLocalNode *localNode = new IOFireWireLocalNode;
    
	localNode->setConfigDirectory( fRootDir );
	
    OSDictionary *propTable;
    do {
        OSObject * prop;
        propTable = OSDictionary::withCapacity(8);
        if (!propTable)
            continue;

        prop = OSNumber::withNumber(guid, 8*sizeof(guid));
        if(prop) {
            propTable->setObject(gFireWire_GUID, prop);
            prop->release();
        }
        
        localNode->init(propTable);
        localNode->attach(this);
        localNode->registerService();
    } while (false);
    if(propTable)
        propTable->release();	// done with it after init

#if FIRELOGCORE
    // enable FireLog
    fFireLogPublisher = IOFireLogPublisher::create( this );
	
#endif

	fIRM = IOFireWireIRM::create(this);
	FWPANICASSERT( fIRM != NULL );
    
	fWorkLoop->enableAllInterrupts();	// Enable the interrupt delivery.

    // register ourselves with superclass policy-maker
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, ourPowerStates, number_of_power_states);
    
    // No idle sleep
    res = changePowerStateTo(1);
    IOLog("Local FireWire GUID = 0x%lx:0x%lx\n", (UInt32)(guid >> 32), (UInt32)(guid & 0xffffffff));

    registerService();			// Enable matching with this object
	
    return res == kIOReturnSuccess;
}

// stop
//
//

void IOFireWireController::stop( IOService * provider )
{

    // Fake up disappearance of entire bus
    processBusReset();
    
	// tear down security state change notification
	freeSecurity();
		    
    PMstop();

    if(fBusState == kAsleep) {
        IOReturn sleepRes;
        sleepRes = fWorkLoop->wake(&fBusState);
        if(sleepRes != kIOReturnSuccess) {
            IOLog("IOFireWireController::stop - Can't wake FireWire workloop, error 0x%x\n", sleepRes);
        }
        
		openGate();
    }
	
    IOService::stop(provider);
}

// finalize
//
//

bool IOFireWireController::finalize( IOOptionBits options )
{
    bool res;

#if FIRELOGCORE
    if( fFireLogPublisher )
    {
        fFireLogPublisher->release();
        fFireLogPublisher = NULL;
    }
#endif

    res = IOService::finalize(options);

    return res;
}

// requestTerminate
//
// send our custom kIOFWMessageServiceIsRequestingClose to clients

bool IOFireWireController::requestTerminate( IOService * provider, IOOptionBits options )
{
    OSIterator *childIterator;

    childIterator = getClientIterator();
    if( childIterator ) 
	{
        OSObject *child;
        while( (child = childIterator->getNextObject()) ) 
		{
            IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
            if(found && !found->isTerminated() && found->isOpen()) 
			{
                // send our custom requesting close message
                messageClient( kIOFWMessageServiceIsRequestingClose, found );
            }
        }
		
        childIterator->release();
    }

    // delete local node
    IOFireWireLocalNode *localNode = getLocalNode(this);
    if(localNode) {
        localNode->release();
    }

    return IOService::requestTerminate(provider, options);
}

// setPowerState
//
//

IOReturn IOFireWireController::setPowerState( unsigned long powerStateOrdinal,
                                                IOService* whatDevice )
{
    IOReturn res;
    IOReturn sleepRes;
	
    // use gate to keep other threads off the hardware,
    // Either close gate or wake workloop.
    // First time through, we aren't really asleep.
    if( fBusState != kAsleep ) 
	{
        closeGate();
    }
    else 
	{
        sleepRes = fWorkLoop->wake(&fBusState);
        if(sleepRes != kIOReturnSuccess) 
		{
            IOLog("Can't wake FireWire workloop, error 0x%x\n", sleepRes);
        }
    }
    // Either way, we have the gate closed against invaders/lost sheep
    if(powerStateOrdinal != 0)
    {
		fDevicePruneDelay = kWakeDevicePruneDelay;
        fBusState = kRunning;	// Will transition to a bus reset state.
        if( fDelayedStateChangeCmdNeedAbort )
        {
            fDelayedStateChangeCmdNeedAbort = false;
            fDelayedStateChangeCmd->cancel(kIOReturnAborted);
        }
    }
    
    // Reset bus if we're sleeping, before turning hw off.
    if(powerStateOrdinal == 0)
    {
        fFWIM->setContender(false); 
		fFWIM->setRootHoldOff(false); 
        fFWIM->resetBus();
 		IOSleep(10);		// Reset bus may not be instantaneous anymore. Wait a bit to be sure
 							// it happened before turning off hardware.
 							// zzz how long to wait?
 	}  	
        
    res = fFWIM->setLinkPowerState(powerStateOrdinal);
    
    // reset bus 
    if( powerStateOrdinal == 1 && res == IOPMAckImplied )
	{
		if ( kIOReturnSuccess != UpdateROM() )
			IOLog(" %s %u: UpdateROM() got error\n", __FILE__, __LINE__ ) ;
	
        fFWIM->resetBus();	// Don't do this on startup until Config ROM built.
	}
	
    // Update power state, keep gate closed while we sleep.
    if(powerStateOrdinal == 0) {
        // Pretend we had a bus reset - we'll have a real one when we wake up.
        if( delayedStateCommandInUse() )
        {
			fDelayedStateChangeCmdNeedAbort = true;
		}
		
		if( fBusResetState == kResetStateDisabled )
		{
			// we'll cause a reset when we wake up and reset the state machine anyway
			fBusResetStateChangeCmd->cancel( kIOReturnAborted );
			fBusResetState = kResetStateResetting;
		}
		
        fBusState = kAsleep;
    }
    if((fBusState == kAsleep) && (fROMHeader[1] == kFWBIBBusName)) {
         sleepRes = fWorkLoop->sleep(&fBusState);
        if(sleepRes != kIOReturnSuccess) {
            IOLog("Can't sleep FireWire workloop, error 0x%x\n", sleepRes);
        }
    }
    else {
        openGate();
    }

    return res;
}

// resetBus
//
//

IOReturn IOFireWireController::resetBus()
{
    IOReturn res = kIOReturnSuccess;

	closeGate();
	
	switch( fBusResetState )
	{
		case kResetStateDisabled:
			// always schedule resets during the first 2 seconds
			fBusResetScheduled = true;
			break;
			
		case kResetStateArbitrated:
			if( fBusResetDisabledCount == 0 )
			{
				// cause a reset if no one has disabled resets
				doBusReset();
			}
			else if( !fBusResetScheduled )
			{
				// schedule the reset if resets are disabled
				fBusResetScheduled = true;
			}
			break;
		
		case kResetStateResetting:
			// we're in the middle of a reset now, no sense in doing another
			// fBusResetScheduled would be cleared on the transition out of this state anyway
		default:
			break;
	}

	openGate();
	
    return res;
}


// resetStateChange
//
// called 2 seconds after a bus reset to transition from the disabled state
// to the arbitrated state

void IOFireWireController::resetStateChange(void *refcon, IOReturn status,
								IOFireWireBus *bus, IOFWBusCommand *fwCmd)
{
    IOFireWireController *me = (IOFireWireController *)bus;
	
    if( status == kIOReturnTimeout ) 
	{
		// we should only transition here from the disabled state
#ifdef FWKLOGASSERT
		FWKLOGASSERT( me->fBusResetState == kResetStateDisabled );
#endif
		// transition to the arbitrated reset state
		me->fBusResetState = kResetStateArbitrated;
		
		// reset now if we need to and no one has disabled resets
		if( me->fBusResetScheduled && me->fBusResetDisabledCount == 0 )
		{
			me->doBusReset();
		}
	}
}

// doBusReset
//
//

void IOFireWireController::doBusReset( void )
{
	IOReturn 	status = kIOReturnSuccess;
	bool		useIBR = false;
	
	if( fDelayedPhyPacket )
	{
		fFWIM->sendPHYPacket( fDelayedPhyPacket );
		fDelayedPhyPacket = 0x00000000;

		// If the gap counts were mismatched and we're optimizing the
		// gap count, use in IBR instead of an ISBR
		
		// This works around a problem with certain devices when they
		// see an ISBR followed by an IBR due to ISBR promotion.
		
		// When this occurs the device is supposed to throw out the IBR.
		// Devices that don't do this latch the gap count with the ISBR
		// and then reset it with the subsequent IBR.
		
		// This gets us into a loop where we keep trying to optimize the
		// gap count only to find it reset on the next bus generation
		
		// Using an IBR avoids the problem.
		
		if( fGapCountMismatch )
		{
			useIBR = true;
		}
	}

	FWKLOG(( "IOFireWireController::doBusReset\n" ));

	fBusResetState = kResetStateResetting;
	
	//
	// start bus reset timer
	//
	
	if( delayedStateCommandInUse() )
	{
		fDelayedStateChangeCmd->cancel(kIOReturnAborted);
	}
	
	fBusState = kWaitingBusReset;
	fDelayedStateChangeCmd->reinit(1000 * kBusResetTimeout, delayedStateChange, NULL);
    fDelayedStateChangeCmd->submit();
	
	status = fFWIM->resetBus( useIBR );
}

// enterBusResetDisabledState
//
//

void IOFireWireController::enterBusResetDisabledState( )
{	
	if( fBusResetState == kResetStateDisabled )
		fBusResetStateChangeCmd->cancel( kIOReturnAborted );
	
	// start the reset disabled state
	fBusResetState = kResetStateDisabled;
	fBusResetStateChangeCmd->reinit( 1000 * kRepeatResetDelay, resetStateChange, NULL );
    fBusResetStateChangeCmd->submit();
}

// disableSoftwareBusResets
//
//

IOReturn IOFireWireController::disableSoftwareBusResets( void )
{
	IOReturn status = kIOReturnSuccess;
	
	closeGate();

	switch( fBusResetState )
	{
		case kResetStateDisabled:
			// bus resets have no priority in this state
			// we will always allow this call to succeed in these states
			fBusResetDisabledCount++;
			break;
			
		case kResetStateResetting:
			// we're in the process of bus resetting, but processBusReset has
			// not yet been called
			
			// if we allowed this call to succeed the client would receive a
			// bus reset notification after the disable call which is
			// probably not what the caller expects
			status = kIOFireWireBusReset;
			break;
			
		case kResetStateArbitrated:
			// bus resets have an equal priority in this state
			// this call will fail if we need to cause a reset in this state
			if( !fBusResetScheduled )
			{
				fBusResetDisabledCount++;
			}
			else
			{
				// we're trying to get a bus reset out, we're not allowing
				// any more disable calls
				status = kIOFireWireBusReset;
			}
			break;
		
		default:
			break;
	}
	
//	IOLog( "IOFireWireController::disableSoftwareBusResets = 0x%08lx\n", (UInt32)status );
			
	openGate();
	
	return status;
}

// enableSoftwareBusResets
//
//

void IOFireWireController::enableSoftwareBusResets( void )
{
	closeGate();

#ifdef FWKLOGASSERT
	FWKLOGASSERT( fBusResetDisabledCount != 0 );
#endif

	// do this in any state
	if( fBusResetDisabledCount > 0 )
	{
		fBusResetDisabledCount--;
//		IOLog( "IOFireWireController::enableSoftwareBusResets\n" );
	}

	switch( fBusResetState )
	{
		case kResetStateArbitrated:
			// this is the only state we're allowed to cause resets in
			if( fBusResetDisabledCount == 0 && fBusResetScheduled )
			{
				// reset the bus if the disabled count is down 
				// to zero and a bus reset is scheduled
				doBusReset();
			}
			break;
			
		case kResetStateResetting:
		case kResetStateDisabled:		
		default:
			break;
	}
	
	openGate();
}

// delayedStateChange
//
//

void IOFireWireController::delayedStateChange(void *refcon, IOReturn status,
                                IOFireWireBus *bus, IOFWBusCommand *fwCmd)
{
    IOFireWireController *me = (IOFireWireController *)bus;

    if(status == kIOReturnTimeout) {
        switch (me->fBusState) {
		case kWaitingBusReset:
			// timed out waiting for a bus reset
//			IOLog( "IOFireWireController::delayedStateChange - timed out waiting for a bus reset - resetting bus\n" );
			me->enterBusResetDisabledState();
			me->fBusState = kRunning;
			me->resetBus();
			break;
		case kWaitingSelfIDs:
			// timed out waiting for self ids
//			IOLog( "IOFireWireController::delayedStateChange - timed out waiting for self ids - resetting bus\n" );
			me->fBusState = kRunning;
			me->resetBus();
			break;
		case kWaitingScan:
            me->fBusState = kScanning;
            me->startBusScan();
            break;
        case kWaitingPrune:
            me->fBusState = kRunning;
            me->updatePlane();
            break;
        default:
            IOLog("State change timeout, state is %d\n", me->fBusState);
            break;
        }        
    }
}

// scanningBus
//
// Are we currently scanning the bus?

bool IOFireWireController::scanningBus() const
{
	return fBusState == kWaitingSelfIDs || fBusState == kWaitingScan || fBusState == kScanning;
}

// delayedStateCommandInUse
//
//

bool IOFireWireController::delayedStateCommandInUse() const
{
	return (fBusState == kWaitingBusReset) || (fBusState == kWaitingSelfIDs) || (fBusState == kWaitingScan) || (fBusState == kWaitingPrune);
}

// getResetTime
//
//

const AbsoluteTime * IOFireWireController::getResetTime() const 
{
	return &fResetTime;
}

// processBusReset
//
// Hardware detected a bus reset.
// At this point we don't know what the hardware addresses are

void IOFireWireController::processBusReset()
{
	FWKLOG(( "IOFireWireController::processBusReset\n" ));
	
	// set generation property to kFWBusScanInProgress ("-1")
	FWKLOG(("IOFireWireController::processBusReset set generation to '%s' realGen=0x%lx\n", kFWBusScanInProgress, fBusGeneration));
	setProperty( kFireWireGenerationID, kFWBusScanInProgress );
	
    clock_get_uptime(&fResetTime);	// Update even if we're already processing a reset
	
	// we got our bus reset, cancel any reset work in progress
	fBusResetScheduled = false;

	enterBusResetDisabledState();
	
	if( fBusState == kWaitingSelfIDs )
	{
		//
		// if we're already in the kWaitingSelfIDs state then we only need to
		// push the timer ahead.  cancel it here.  it will be restarted at 
		// the end of this method
		//
		
		fDelayedStateChangeCmd->cancel( kIOReturnAborted );
	}
	else
	{
		//
		// the following processing only needs to be done if we're not already in
		// the kWaitingSelfIDs state.  ie. on the first bus reset before self ids
		//
		
        if( delayedStateCommandInUse() )
		{
            fDelayedStateChangeCmd->cancel(kIOReturnAborted);
        }
		
		fBusState = kWaitingSelfIDs;
		
		fRequestedHalfSizePackets = false;
		
        unsigned int i;
        UInt32 oldgen = fBusGeneration;
    
		// Set all current device nodeIDs to something invalid
        fBusGeneration++;
        OSIterator * childIterator = getClientIterator();
        if( childIterator ) 
		{
            OSObject * child;
            while( (child = childIterator->getNextObject()) ) 
			{
                IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
                if( found && !found->isTerminated() )
				{
                    found->setNodeROM( oldgen, kFWBadNodeID, NULL );
                }
				else if( OSDynamicCast(IOFireWireLocalNode, child) ) 
				{
                    ((IOFireWireLocalNode *)child)->messageClients(kIOMessageServiceIsSuspended);
                }
            }

            childIterator->release();
        }

		// reset physical filters if necessary
		physicalAccessProcessBusReset();

		// IOLog("FireWire Bus Generation now %d\n", fBusGeneration);
        
		// Invalidate current topology and speed map
		bzero( fSpeedCodes, sizeof(fSpeedCodes) );
		bzero(fHopCounts, sizeof(fHopCounts) );
		
        // Zap all outstanding async requests
        for( i=0; i<kMaxPendingTransfers; i++ ) 
		{
            AsyncPendingTrans *t = &fTrans[i];
            if( t->fHandler ) 
			{
                IOFWAsyncCommand * cmd = t->fHandler;
                cmd->gotPacket(kFWResponseBusResetError, NULL, 0);
            }
        }

        // Clear out the old firewire plane
        if( fNodes[fRootNodeID] ) 
		{
            fNodes[fRootNodeID]->detachAll(gIOFireWirePlane);
        }
		
        for( i=0; i<=fRootNodeID; i++ ) 
		{
            if(fNodes[i]) 
			{
                fNodes[i]->release();
                fNodes[i] = NULL;
            }
        }
        
        // Cancel all commands in timeout queue that want to complete on bus reset
        fTimeoutQ.busReset();
    }

	// state will be set to kWaitingSelfIDs by now
	
	// set a timer
	fDelayedStateChangeCmd->reinit(1000 * kSelfIDTimeout, delayedStateChange, NULL);
    fDelayedStateChangeCmd->submit();
}

// processSelfIDs
//
//

void IOFireWireController::processSelfIDs(UInt32 *IDs, int numIDs, UInt32 *ownIDs, int numOwnIDs)
{
    int i;
    UInt32 id;
    UInt32 nodeID;
    UInt16 irmID;
    UInt16 ourID;
    IOFireWireLocalNode * localNode;

	FWKLOG(( "IOFireWireController::processSelfIDs entered\n" ));

#if (DEBUGGING_LEVEL > 0)
for(i=0; i<numIDs; i++)
    IOLog("ID %d: 0x%x <-> 0x%x\n", i, IDs[2*i], ~IDs[2*i+1]);

for(i=0; i<numOwnIDs; i++)
    IOLog("Own ID %d: 0x%x <-> 0x%x\n", i, ownIDs[2*i], ~ownIDs[2*i+1]);
#endif

	if( fBusState != kWaitingSelfIDs )
	{
		// If not processing a reset, then we should be
		// This can happen if we get two resets in quick successio
        processBusReset();
    }
	
	// we should now be in the kWaitingSelfIDs state
	
	if( fBusState != kWaitingSelfIDs )
	{
		IOLog( "IOFireWireController::processSelfIDs - fBusState != kWaitingSelfIDs\n" );
	}
	
	if( fBusState == kWaitingSelfIDs )
	{
		// cancel self id timeout
		fDelayedStateChangeCmd->cancel( kIOReturnAborted );
	}

	fBusState = kWaitingScan;
                      
    // Initialize root node to be our node, we'll update it below to be the highest node ID.
    fRootNodeID = ourID = (*ownIDs & kFWPhyPacketPhyID) >> kFWPhyPacketPhyIDPhase;
    fLocalNodeID = ourID | (kFWLocalBusAddress>>kCSRNodeIDPhase);

	fGapCountMismatch = false;

    // check for mismatched gap counts
    UInt32 gap_count = (*ownIDs & kFWSelfID0GapCnt) >> kFWSelfID0GapCntPhase;
    for( i = 0; i < numIDs; i++ )
    {
        UInt32 current_id = IDs[2*i];
        if( (current_id & kFWSelfIDPacketType) == 0 &&
            ((current_id & kFWSelfID0GapCnt) >> kFWSelfID0GapCntPhase) != gap_count )
        {
            // set the gap counts to 0x3F, if any gap counts are mismatched
            fFWIM->sendPHYPacket( (kFWConfigurationPacketID << kFWPhyPacketIDPhase) |
                                  (0x3f << kFWPhyConfigurationGapCntPhase) | kFWPhyConfigurationT );
			
			fGapCountMismatch = true;
            break;
        }
    }

    // Update the registry entry for our local nodeID,
    // which will have been updated by the device driver.
    // Find the local node (just avoiding adding a member variable)
    localNode = getLocalNode(this);
    if(localNode)
	{
        localNode->setNodeProperties(fBusGeneration, fLocalNodeID, ownIDs, numOwnIDs);
        fNodes[ourID] = localNode;
        localNode->retain();
    }
    
    // Copy over the selfIDs, checking validity and merging in our selfIDs if they aren't
    // already in the list.
    SInt16 prevID = -1;	// Impossible ID.
    UInt32 *idPtr = fSelfIDs;

	// zero out fNodeIDs so any gaps in the received self IDs will be
	// apparent later...
	bzero( fNodeIDs, sizeof(fNodeIDs) ) ;

    for(i=0; i<numIDs; i++)
	{
        UInt32 id = IDs[2*i];
        UInt16 currID = (id & kFWPhyPacketPhyID) >> kFWPhyPacketPhyIDPhase;

        if(id != ~IDs[2*i+1])
		{
            IOLog("Bad SelfID packet %d: 0x%lx != 0x%lx!\n", i, id, ~IDs[2*i+1]);
            resetBus();	// Could wait a bit in case somebody else spots the bad packet
			FWKLOG(( "IOFireWireController::processSelfIDs exited\n" ));
            return;
        }

        if(currID != prevID)
		{
            // Check for ownids not in main list
            if( prevID < ourID && currID > ourID )
			{
				int j;
				fNodeIDs[ourID] = idPtr;
				for(j=0; j<numOwnIDs; j++)
					*idPtr++ = ownIDs[2*j];
			}
			fNodeIDs[currID] = idPtr;
			prevID = currID;
			if(fRootNodeID < currID)
				fRootNodeID = currID;
        }
		*idPtr++ = id;
    }
	
    // Check for ownids at end & not in main list
    if(prevID < ourID)
	{
        int j;
        fNodeIDs[ourID] = idPtr;
        for(j=0; j<numOwnIDs; j++)
            *idPtr++ = ownIDs[2*j];
    }
    // Stick a known elephant at the end.
    fNodeIDs[fRootNodeID+1] = idPtr;

    // Check nodeIDs are monotonically increasing from 0.
    for(i = 0; i<=fRootNodeID; i++)
	{
        if ( NULL == fNodeIDs[i] )
		{
			IOLog("Missing self ID for node %d!\n", i ) ;
			resetBus();        	// Could wait a bit in case somebody else spots the bad packet

			return;				// done.
		}

		if( ((*fNodeIDs[i] & kFWPhyPacketPhyID) >> kFWPhyPacketPhyIDPhase) != (UInt32)i)
		{
			IOLog("No FireWire node %d (got ID packet 0x%lx)!\n", i, *fNodeIDs[i]);
			resetBus();        // Could wait a bit in case somebody else spots the bad packet

			return;				// done.
		}
    }
    
    // Store selfIDs
    OSObject * prop = OSData::withBytes( fSelfIDs, (numIDs+1) * sizeof(UInt32));
    setProperty(gFireWireSelfIDs, prop);
    prop->release();
    
    buildTopology(false);
	
#if (DEBUGGING_LEVEL > 0)
    for(i=0; i<numIDs; i++) {
        id = IDs[2*i];
        if(id != ~IDs[2*i+1]) {
            DEBUGLOG("Bad SelfID: 0x%x <-> 0x%x\n", id, ~IDs[2*i+1]);
            continue;
        }
	DEBUGLOG("SelfID: 0x%x\n", id);
    }
    DEBUGLOG("Our ID: 0x%x\n", *ownIDs);
#endif
    // Find isochronous resource manager, if there is one
    irmID = 0;
    for(i=0; i<=fRootNodeID; i++) {
        id = *fNodeIDs[i];
        // Get nodeID.
        nodeID = (id & kFWSelfIDPhyID) >> kFWSelfIDPhyIDPhase;
        nodeID |= kFWLocalBusAddress>>kCSRNodeIDPhase;

        if((id & (kFWSelfID0C | kFWSelfID0L)) == (kFWSelfID0C | kFWSelfID0L)) {
#if (DEBUGGING_LEVEL > 0)
            IOLog("IRM contender: %lx\n", nodeID);
#endif
            if(nodeID > irmID)
		irmID = nodeID;
	}
    }
    if(irmID != 0)
        fIRMNodeID = irmID;
    else
        fIRMNodeID = kFWBadNodeID;

    fBusMgr = false;	// Update if we find one.
    
	// inform IRM of updated bus generation and node ids
	fIRM->processBusReset( fLocalNodeID, fIRMNodeID, fBusGeneration );
	
    // OK for stuff that only needs our ID and the irmID to continue.
    if(localNode) {
        localNode->messageClients(kIOMessageServiceIsResumed);
    }
    fDelayedStateChangeCmd->reinit(1000 * kScanBusDelay, delayedStateChange, NULL);
    fDelayedStateChangeCmd->submit();

	FWKLOG(( "IOFireWireController::processSelfIDs exited\n" ));
}

// startBusScan
//
//

void IOFireWireController::startBusScan() 
{
    int i;

	FWKLOG(( "IOFireWireController::startBusScan entered\n" ));
	
    // Send global resume packet
	fFWIM->sendPHYPacket(((fLocalNodeID & 0x3f) << kFWPhyPacketPhyIDPhase) | 0x003c0000);

    fNumROMReads = 0;
    for(i=0; i<=fRootNodeID; i++) {
        UInt16 nodeID;
        UInt32 id;
        id = *fNodeIDs[i];
        // Get nodeID.
        nodeID = (id & kFWSelfIDPhyID) >> kFWSelfIDPhyIDPhase;
        nodeID |= kFWLocalBusAddress>>kCSRNodeIDPhase;
        if(nodeID == fLocalNodeID)
            continue;	// Skip ourself!

        // Read ROM header if link is active (MacOS8 turns link on, why?)
        if(true) { //id & kFWSelfID0L) {
            IOFWNodeScan *scan;
            scan = (IOFWNodeScan *)IOMalloc(sizeof(*scan));
            fNumROMReads++;

            scan->fControl = this;
            scan->fAddr.nodeID = nodeID;
            scan->fAddr.addressHi = kCSRRegisterSpaceBaseAddressHi;
            scan->fAddr.addressLo = kConfigBIBHeaderAddress;
            scan->fSelfIDs = fNodeIDs[i];
            scan->fNumSelfIDs = fNodeIDs[i+1] - fNodeIDs[i];
            scan->fRead = 0;
            scan->generation = fBusGeneration;
            scan->fCmd = new IOFWReadQuadCommand;
 			FWKLOG(( "IOFireWireController::startBusScan node %lx speed was %lx\n",(UInt32)nodeID,(UInt32)FWSpeed( nodeID ) ));	
           	
			if( FWSpeed( nodeID ) & kFWSpeedUnknownMask ) 
			{
           		
               	fSpeedCodes[(kFWMaxNodesPerBus + 1)*(scan->fAddr.nodeID & 63) + (fLocalNodeID & 63)] &= ~kFWSpeedUnknownMask;
                fSpeedCodes[(kFWMaxNodesPerBus + 1)*(fLocalNodeID & 63) + (scan->fAddr.nodeID & 63)] &= ~kFWSpeedUnknownMask;

				scan->fCmd->initAll(this, fBusGeneration, scan->fAddr, scan->fBuf, 1,
                                                &readROMGlue, scan);

          		FWKLOG(( "IOFireWireController::startBusScan speedchecking\n" ));	
            	scan->speedChecking = true;	// May need to try speeds slower than s800 if this fails
            								// zzz What about s1600?
            }
            else
            {
				scan->fCmd->initAll(this, fBusGeneration, scan->fAddr, scan->fBuf, 1,
                                                &readROMGlue, scan);

				scan->fCmd->setMaxSpeed( kFWSpeed100MBit );
            	scan->speedChecking = false;
            	FWKLOG(( "IOFireWireController::startBusScan not speedchecking\n" ));
            }	

            scan->fCmd->submit();
        }
    }
    if(fNumROMReads == 0) {
        finishedBusScan();
    }
	
	FWKLOG(( "IOFireWireController::startBusScan exited\n" ));	
}

// readROMGlue
//
//

void IOFireWireController::readROMGlue(void *refcon, IOReturn status,
			IOFireWireNub *device, IOFWCommand *fwCmd)
{
    IOFWNodeScan *scan = (IOFWNodeScan *)refcon;
    scan->fControl->readDeviceROM(scan, status);
}

// readDeviceROM
//
//

void IOFireWireController::readDeviceROM(IOFWNodeScan *scan, IOReturn status)
{
    bool done = true;
	
	FWKLOG(( "IOFireWireController::readDeviceROM entered\n" ));

    if(status != kIOReturnSuccess) 
	{
		// If status isn't bus reset, make a dummy registry entry.
        
		if(status == kIOFireWireBusReset) 
		{
            scan->fCmd->release();
            IOFree(scan, sizeof(*scan));
			FWKLOG(( "IOFireWireController::readDeviceROM exited\n" ));
			return;
        }
        
   		FWKLOG(( "IOFireWireController::readDeviceROM speedcheck %lx ; speed %lx\n", (UInt32)scan->speedChecking, (UInt32)FWSpeed( scan->fAddr.nodeID ) ));
        if( scan->speedChecking && FWSpeed( scan->fAddr.nodeID ) > kFWSpeed100MBit )
        {
        	if( scan->generation == fBusGeneration )
        	{
  				FWKLOG(( "IOFireWireController::readDeviceROM reseting speed for node %lx from local %lx\n", (UInt32)scan->fAddr.nodeID, (UInt32)fLocalNodeID));
              	fSpeedCodes[(kFWMaxNodesPerBus + 1)*(scan->fAddr.nodeID & 63) + (fLocalNodeID & 63)]--;
                fSpeedCodes[(kFWMaxNodesPerBus + 1)*(fLocalNodeID & 63) + (scan->fAddr.nodeID & 63)]--;
               
               	// Retry command at slower speed
				scan->fCmd->reinit(scan->fAddr, scan->fBuf, 1, &readROMGlue, scan, true);
              	scan->fCmd->submit();
  				return;
        	}
        }

		UInt32 nodeID = FWAddressToID(scan->fAddr.nodeID);
        fNodes[nodeID] = createDummyRegistryEntry( scan );
        
		fNumROMReads--;
        if(fNumROMReads == 0) 
		{
            finishedBusScan();
        }

        scan->fCmd->release();
        IOFree(scan, sizeof(*scan));
		FWKLOG(( "IOFireWireController::readDeviceROM exited\n" ));
		return;
    }

    if(scan->fRead == 0) 
	{
		if( ((scan->fBuf[0] & kConfigBusInfoBlockLength) >> kConfigBusInfoBlockLengthPhase) == 1) 
		{
            // Minimal ROM
            scan->fROMSize = 4;
            done = true;
		}
		else 
		{
            scan->fROMSize = 4*((scan->fBuf[0] & kConfigROMCRCLength) >> kConfigROMCRCLengthPhase) + 4;
            if(scan->fROMSize > 20)
                scan->fROMSize = 20;	// Just read bus info block
            scan->fRead = 8;
            scan->fBuf[1] = kFWBIBBusName;	// No point reading this!
            scan->fAddr.addressLo = kConfigROMBaseAddress+8;
            scan->fCmd->reinit(scan->fAddr, scan->fBuf+2, 1,
                                                        &readROMGlue, scan, true);
            scan->fCmd->setMaxSpeed( kFWSpeed100MBit );
			scan->fCmd->submit();
            done = false;
		}
    }
    else if(scan->fRead < 16) 
	{
        if(scan->fROMSize > scan->fRead) 
		{
            scan->fRead += 4;
            scan->fAddr.addressLo = kConfigROMBaseAddress+scan->fRead;
            scan->fCmd->reinit(scan->fAddr, scan->fBuf+scan->fRead/4, 1,
                                                        &readROMGlue, scan, true);
            scan->fCmd->setMaxSpeed( kFWSpeed100MBit );
			scan->fCmd->submit();
            done = false;
        }
        else
            done = true;
    }
	
    if( done ) 
	{
        // See if this is a bus manager
        if(!fBusMgr)
            fBusMgr = scan->fBuf[2] & kFWBIBBmc;
        
	// Check if node exists, if not create it
#if (DEBUGGING_LEVEL > 0)
        DEBUGLOG("Finished reading ROM for node 0x%x\n", scan->fAddr.nodeID);
#endif
        IOFireWireDevice *	newDevice = NULL;
        do 
		{
            CSRNodeUniqueID guid;
            OSIterator *childIterator;
            if(scan->fROMSize >= 20)
            	guid = *(CSRNodeUniqueID *)(scan->fBuf+3);
            else
                guid = scan->fBuf[0];	// Best we can do.

			//
			// GUID zero is not a valid GUID. Unfortunately some devices return this as
			// their GUID until they're fully powered up.
			//
			
			if( guid == 0 )
			{
				UInt32 nodeID = FWAddressToID(scan->fAddr.nodeID);
				fNodes[nodeID] = createDummyRegistryEntry( scan );
				OSObject * prop = OSNumber::withNumber( guid, 64 );
				if( prop != NULL ) 
				{
					fNodes[nodeID]->setProperty( gFireWire_GUID, prop );
					prop->release();
				}
				continue;
			}
			
            childIterator = getClientIterator();
            if( childIterator) 
			{
                OSObject *child;
                while( (child = childIterator->getNextObject())) 
				{
                    IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
                    if(found && found->fUniqueID == guid && !found->isTerminated()) 
					{
                        newDevice = found;
                        break;
                    }
                }
                childIterator->release();
            }

            if(newDevice) 
			{
				// Just update device properties.
				#if IOFIREWIREDEBUG > 0
					IOLog("Found old device 0x%p\n", newDevice);
				#endif
				newDevice->setNodeROM(fBusGeneration, fLocalNodeID, scan);
				newDevice->retain();	// match release, since not newly created.
            }
            else 
			{
                newDevice = fFWIM->createDeviceNub(guid, scan);
                if (!newDevice)
                    continue;
					
				#if IOFIREWIREDEBUG > 0
					IOLog("Creating new device 0x%p\n", newDevice);
				#endif
				
				// we must stay busy until we've called registerService on the device
				// and all of its units
				
				newDevice->adjustBusy( 1 ); // device
				adjustBusy( 1 );  // controller
				
				FWKLOG(( "IOFireWireController@0x%08lx::readDeviceROM adjustBusy(1)\n", (UInt32)this ));
				
				// hook this device into the device tree now
				
				// we won't rediscover this device after a bus reset if the device is
				// not in the registry.  if we attached later and got a bus reset before
				// we had attached the device we would leak the device object
				
				if( !newDevice->attach(this) )
                {
					// if we failed to attach, I guess we're not busy anymore
					newDevice->adjustBusy( -1 );  // device
					adjustBusy( -1 );  // controller
					FWKLOG(( "IOFireWireController@0x%08lx::readDeviceROM adjustBusy(-1)\n", (UInt32)this ));
					continue;
                }
				
				// we will register this service once we finish reading the config rom
				newDevice->setRegistrationState( IOFireWireDevice::kDeviceNeedsRegisterService );

				// this will start the config ROM read
				newDevice->setNodeROM( fBusGeneration, fLocalNodeID, scan );

            }
			
            UInt32 nodeID = FWAddressToID(scan->fAddr.nodeID);
            fNodes[nodeID] = newDevice;
            fNodes[nodeID]->retain();
		
		} while (false);
        
		if (newDevice)
            newDevice->release();
        
		scan->fCmd->release();
        IOFree(scan, sizeof(*scan));
        fNumROMReads--;
        if(fNumROMReads == 0)
		{
            finishedBusScan();
        }

    }
	
	FWKLOG(( "IOFireWireController::readDeviceROM exited\n" ));
}

// createDummyRegistryEntry
//
// if we are unable to successfully read the BIB of a a device, we create
// a dummy registry entry by calling this routine

IORegistryEntry * IOFireWireController::createDummyRegistryEntry( IOFWNodeScan *scan )
{
	OSDictionary *propTable;
	OSObject * prop;
	propTable = OSDictionary::withCapacity(3);
	prop = OSNumber::withNumber(scan->fAddr.nodeID, 16);
	propTable->setObject(gFireWireNodeID, prop);
	prop->release();

	prop = OSNumber::withNumber((scan->fSelfIDs[0] & kFWSelfID0SP) >> kFWSelfID0SPPhase, 32);
	propTable->setObject(gFireWireSpeed, prop);
	prop->release();

	prop = OSData::withBytes(scan->fSelfIDs, scan->fNumSelfIDs*sizeof(UInt32));
	propTable->setObject(gFireWireSelfIDs, prop);
	prop->release();

	IORegistryEntry * newPhy;
	newPhy = new IORegistryEntry;
	if(newPhy) 
	{
		if(!newPhy->init(propTable)) 
		{
			newPhy->release();	
			newPhy = NULL;
		}
        if(getSecurityMode() == kIOFWSecurityModeNormal) {
            // enable physical access for FireBug and other debug tools without a ROM
            setNodeIDPhysicalFilter( scan->fAddr.nodeID & 0x3f, true );
        }
        if(propTable)
            propTable->release();
	}
	
	return newPhy;
}

// finishedBusScan
//
//

void IOFireWireController::finishedBusScan()
{
	FWKLOG(( "IOFireWireController::finishedBusScan entered\n" ));
	
    // These magic numbers come from P1394a, draft 4, table C-2.
    // This works for cables up to 4.5 meters and PHYs with
    // PHY delay up to 144 nanoseconds.  Note that P1394a PHYs
    // are allowed to have delay >144nS; we don't cope yet.
    static UInt32 gaps[17] = {63, 5, 7, 8, 10, 13, 16, 18, 21,
                            24, 26, 29, 32, 35, 37, 40, 43};
    
    // Now do simple bus manager stuff, if there isn't a better candidate.
    // This might cause us to issue a bus reset...
    // Skip if we're about to reset anyway, since we might be in the process of setting
    // another node to root.
    if( !fBusResetScheduled && !fBusMgr && fLocalNodeID == fIRMNodeID) {
        int maxHops;
        int i;
        // Do lazy gap count optimization.  Assume the bus is a daisy-chain (worst
        // case) so hop count == root ID.

        // a little note on the gap count optimization - all I do is set an internal field to our optimal
        // gap count and then reset the bus. my new soft bus reset code sets the gap count before resetting
        // the bus (for another reason) and I just rely on that fact.

        maxHops = fRootNodeID;
        if (maxHops > 16) maxHops = 16;
        fGapCount = gaps[maxHops] << kFWPhyConfigurationGapCntPhase;
        if(fRootNodeID == 0) {
            // If we're the only node, clear root hold off.
            fFWIM->setRootHoldOff(false);
        }
        else {
            
            // Send phy packet to get rid of other root hold off nodes
            // Set gap count too.
            fFWIM->sendPHYPacket((kFWConfigurationPacketID << kFWPhyPacketIDPhase) |
                        ((fLocalNodeID & 63) << kFWPhyPacketPhyIDPhase) | kFWPhyConfigurationR );
            fFWIM->setRootHoldOff(true);

            // If we aren't root, issue a bus reset so that we will be.
            if(fRootNodeID != (fLocalNodeID & 63)) {
		//		IOLog( "IOFireWireController::finishedBusScan - make us root\n" );
                resetBus();
				FWKLOG(( "IOFireWireController::finishedBusScan exited\n" ));
				return;			// We'll be back...
            }
        }

        // If we got here we're root, so turn on cycle master
        fFWIM->setCycleMaster(true);

        // Finally set gap count if any nodes don't have the right gap.
        // Only bother if we aren't the only node on the bus.
        if(fRootNodeID != 0) {
            for( i = 0; i <= fRootNodeID; i++ ) {
                // is the gap count set to what we want it to be?
                if( (*fNodeIDs[i] & kFWSelfID0GapCnt) != fGapCount ) {
                    // Nope, send phy config packet and do bus reset.
					fDelayedPhyPacket = (kFWConfigurationPacketID << kFWPhyPacketIDPhase) | 
										((fLocalNodeID & 63) << kFWPhyPacketPhyIDPhase) | 
										kFWPhyConfigurationR | fGapCount | kFWPhyConfigurationT;
				//	IOLog( "IOFireWireController::finishedBusScan - set gap count\n" );
                    resetBus();
					FWKLOG(( "IOFireWireController::finishedBusScan exited\n" ));
                    return;			// We'll be back...
                }
            }
        }
    }


	//
    // Don't change to the waiting prune state if we're about to bus reset again anyway.
    //
	
	if(fBusState == kScanning) 
	{
		bool 			wouldTerminateDevice = false;
		OSIterator *	childIterator;
		
		//
		// check if we would terminate a device if the prune timer ran right now
		//
		
		childIterator = getClientIterator();
		if( childIterator ) 
		{
			OSObject *child;
			while( (child = childIterator->getNextObject())) 
			{
				IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
				if( found && !found->isTerminated() && found->fNodeID == kFWBadNodeID ) 
				{
					wouldTerminateDevice = true;
				}
			}
			childIterator->release();
		}
		
		//
		// if we found all of our devices, set the prune delay to normal
		//

		if( (fRootNodeID == 0) && (fDevicePruneDelay < kOnlyNodeDevicePruneDelay) )
		{
			// if we're the only node increase the prune delay
			// because we won't be causing a bus reset for gap 
			// count optimization.  
			
			fDevicePruneDelay = kOnlyNodeDevicePruneDelay;
		}	
				
		if( !wouldTerminateDevice )
		{
			fDevicePruneDelay = kNormalDevicePruneDelay;
		}
		
        fBusState = kWaitingPrune; 	// Indicate end of bus scan
        fDelayedStateChangeCmd->reinit(1000 * fDevicePruneDelay, delayedStateChange, NULL); // One second
        fDelayedStateChangeCmd->submit();
    }

    // Run all the commands that are waiting for reset processing to end
    IOFWCmdQ &resetQ(getAfterResetHandledQ());
    resetQ.executeQueue(true);

    // Tell all active isochronous channels to re-allocate bandwidth
    IOFWIsochChannel *found;
    fAllocChannelIterator->reset();
    while( (found = (IOFWIsochChannel *) fAllocChannelIterator->getNextObject())) {
        found->handleBusReset();
    }

    // Anything on queue now is associated with a device not on the bus, I think...
    IOFWCommand *cmd;
    while( (cmd = resetQ.fHead) ) 
	{
        cmd->cancel(kIOReturnTimeout);
    }

	FWKLOG(( "IOFireWireController::finishedBusScan exited\n" ));
}

// countNodeIDChildren
//
//

UInt32 IOFireWireController::countNodeIDChildren( UInt16 nodeID )
{
	UInt32 id0, idn;
	UInt32 *idPtr;
	int i;
	int children = 0;
	int ports;
	UInt32 port;
	int mask, shift;
	
	// get type 0 self id
	i = nodeID & 63;
	idPtr = fNodeIDs[i];
	id0 = *idPtr++;
	mask = kFWSelfID0P0;
	shift = kFWSelfID0P0Phase;
	
	// count children
	// 3 ports in type 0 self id
	for(ports=0; ports<3; ports++) 
	{
		port = (id0 & mask) >> shift;
		if(port == kFWSelfIDPortStatusChild)
			children++;
		mask >>= 2;
		shift -= 2;
	}

	// any more self ids for this node?
	if(fNodeIDs[i+1] > idPtr) 
	{
		// get type 1 self id
		idn = *idPtr++;
		mask = kFWSelfIDNPa;
		shift = kFWSelfIDNPaPhase;
		
		// count children
		// 8 ports in type 1 self id
		for(ports=0; ports<8; ports++) 
		{
			port = (idn & mask) >> shift;
			if(port == kFWSelfIDPortStatusChild)
				children++;
			mask >>= 2;
			shift -= 2;
		}
		
		// any more self ids for this node?
		if(fNodeIDs[i+1] > idPtr) 
		{
			// get type 2 self id
			idn = *idPtr++;
			mask = kFWSelfIDNPa;
			shift = kFWSelfIDNPaPhase;
			
			// count children
			// 5 ports in type 2 self id
			for(ports=0; ports<5; ports++) 
			{
				port = (idn & mask) >> shift;
				if(port == kFWSelfIDPortStatusChild)
					children++;
				mask >>= 2;
				shift -= 2;
			}
		}
	}
	
	return children;
}

// buildTopology
//
//

void IOFireWireController::buildTopology(bool doFWPlane)
{
    int i, maxDepth;
    IORegistryEntry *root;
    struct FWNodeScan
    {
        int nodeID;
        int childrenRemaining;
        IORegistryEntry *node;
    };
    FWNodeScan scanList[kFWMaxNodesPerBus];
    FWNodeScan *level;
    maxDepth = 0;
    root = fNodes[fRootNodeID];
    level = scanList;

    // First build the topology.
	
	// iterate over all self ids starting with root id
	for(i=fRootNodeID; i>=0; i--) 
	{
        UInt32 id0;
        UInt8 speedCode;
        IORegistryEntry *node = fNodes[i];
        int children = 0;
    
		// count the children for this self id
		children = countNodeIDChildren( i );

        // Add node to bottom of tree
        level->nodeID = i;
        level->childrenRemaining = children;
        level->node = node;

        // Add node's self speed to speedmap
        id0 = *fNodeIDs[i];
		speedCode = (id0 & kFWSelfID0SP) >> kFWSelfID0SPPhase;
                
        if( !doFWPlane )
        {
        	if( speedCode == kFWSpeedReserved )
        		speedCode = kFWSpeed800MBit | kFWSpeedUnknownMask;	// Remember that we don't know how fast it is
        }
        
        fSpeedCodes[(kFWMaxNodesPerBus + 1)*i + i] = speedCode;
		fHopCounts[(kFWMaxNodesPerBus + 1)*i + i] = 0;
		
        // Add to parent
        // Compute rest of this node's speed map entries unless it's the root.
        // We only need to compute speeds between this node and all higher node numbers.
        // These speeds will be the minimum of this node's speed and the speed between
        // this node's parent and the other higher numbered nodes.
        if (i != fRootNodeID) 
		{
            int parentNodeNum, scanNodeNum;
            parentNodeNum = (level-1)->nodeID;
            if(doFWPlane)
            {
                node->attachToParent((level-1)->node, gIOFireWirePlane);
            }
           	else
           	{
				for (scanNodeNum = i + 1; scanNodeNum <= fRootNodeID; scanNodeNum++)
				{
					UInt8 scanSpeedCode;
					
					// Get speed code between parent and scan node.
					scanSpeedCode = fSpeedCodes[(kFWMaxNodesPerBus + 1)*parentNodeNum + scanNodeNum];
					
					// Set speed map entry to minimum of scan speed and node's speed.
					if ( (speedCode & ~kFWSpeedUnknownMask) < (scanSpeedCode & ~kFWSpeedUnknownMask) )
					{
						scanSpeedCode = speedCode;
					}
					
					if( (speedCode & kFWSpeedUnknownMask) || (scanSpeedCode & kFWSpeedUnknownMask) )
					{
						scanSpeedCode |= kFWSpeedUnknownMask;
					}
					
					fSpeedCodes[(kFWMaxNodesPerBus + 1)*i + scanNodeNum] = scanSpeedCode;
					fSpeedCodes[(kFWMaxNodesPerBus + 1)*scanNodeNum + i] = scanSpeedCode;
					
					// calculate hop counts
					
					UInt8 hops;
					
					// Get the hop count parent and scan node.
					hops = fHopCounts[(kFWMaxNodesPerBus + 1)*parentNodeNum + scanNodeNum];
					
					fHopCounts[(kFWMaxNodesPerBus + 1)*i + scanNodeNum] = hops + 1;
					fHopCounts[(kFWMaxNodesPerBus + 1)*scanNodeNum + i] = hops + 1;

				}
			}
        }
		
        // Find next child port.
        if (i > 0) 
		{
            while (level->childrenRemaining == 0) 
			{
                // Go up one level in tree.
                level--;
                if(level < scanList) 
				{
                    IOLog("SelfIDs don't build a proper tree (missing selfIDS?)!!\n");
                    return;
                }
                // One less child to scan.
                level->childrenRemaining--;
            }
            // Go down one level in tree.
            level++;
            if(level - scanList > maxDepth) 
			{
                maxDepth = level - scanList;
            }
        }
    }


#if (DEBUGGING_LEVEL > 0)
    if(doFWPlane) {
        IOLog("max FireWire tree depth is %d\n", maxDepth);
        IOLog("FireWire Speed map:\n");
        for(i=0; i <= fRootNodeID; i++) {
            int j;
            for(j=0; j <= i; j++) {
                IOLog("%d ", fSpeedCodes[(kFWMaxNodesPerBus + 1)*i + j]);
            }
            IOLog("\n");
        }
    }
#endif
#if 0
    if( doFWPlane ) 
	{
        IOLog( "FireWire Hop Counts:\n" );
        for( i=0; i <= fRootNodeID; i++ ) 
		{
            int j;
            for( j=0; j <= i; j++ ) 
			{
                IOLog( "%d ", fHopCounts[(kFWMaxNodesPerBus + 1)*i + j] );
            }
            IOLog( "\n" );
        }
    }
#endif

    // Finally attach the full topology into the IOKit registry
    if(doFWPlane)
        root->attachToParent(IORegistryEntry::getRegistryRoot(), gIOFireWirePlane);
}

// updatePlane
//
//

void IOFireWireController::updatePlane()
{
    OSIterator *childIterator;
		
	fDevicePruneDelay = kNormalDevicePruneDelay;

    childIterator = getClientIterator();
    if( childIterator) {
        OSObject *child;
        while( (child = childIterator->getNextObject())) {
            IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
            if(found && !found->isTerminated() && found->fNodeID == kFWBadNodeID) {
                if( found->isOpen() )
                {
                    //IOLog( "IOFireWireController : message request close device object %p\n", found);
                    // send our custom requesting close message
                    messageClient( kIOFWMessageServiceIsRequestingClose, found );
                }
                else
                {	
					found->setTerminated( true );
					IOFireWireDevice::terminateDevice( found );
                }
            }
        }
        childIterator->release();
    }

    buildTopology(true);
	
	messageClients( kIOFWMessageTopologyChanged );
	
	// reset generation property to current FireWire Generation
	char busGenerationStr[32];
	sprintf(busGenerationStr, "%lx", fBusGeneration);
	setProperty( kFireWireGenerationID, busGenerationStr);
	FWKLOG(("IOFireWireController::updatePlane reset generation to '%s'\n", busGenerationStr));
	
	fUseHalfSizePackets = fRequestedHalfSizePackets;
}

#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// physical access
//

// setPhysicalAccessMode
//
//

void IOFireWireController::setPhysicalAccessMode( IOFWPhysicalAccessMode mode )
{
	closeGate();

	//
	// enable physical access in normal security mode
	//
	
	if( mode == kIOFWPhysicalAccessEnabled &&
		getSecurityMode() == kIOFWSecurityModeNormal )
	{
		fPhysicalAccessMode = kIOFWPhysicalAccessEnabled;

		FWKLOG(( "IOFireWireController::setPhysicalAccessMode - enable physical access\n" ));

		//
		// disabling physical accesses will have cleared all previous filter state
		//
		// when physical access is enabled we mimic the filter configuration process 
		// after a bus reset. we let each node enable its physical filter if desired
		//
		
		OSIterator * iterator = getClientIterator();
		OSObject * child = NULL;
		while( (child = iterator->getNextObject()) ) 
		{
			IOFireWireDevice * found = OSDynamicCast(IOFireWireDevice, child);
			if( found && !found->isTerminated() )
			{
				// if we found an active device, ask it to reconfigure it's
				// physical filter settings
				found->configurePhysicalFilter();
			}
		}
		
		iterator->release();
	}
	
	//
	// disable physical access
	//
	
	if( mode == kIOFWPhysicalAccessDisabled )
	{
		fPhysicalAccessMode = kIOFWPhysicalAccessDisabled;

		FWKLOG(( "IOFireWireController::setPhysicalAccessMode - disable physical access\n" ));
			
		// shut them all down!
		fFWIM->setNodeIDPhysicalFilter( kIOFWAllPhysicalFilters, false );
	}
	
	//
	// disable physical access for this bus generation if physical access
	// is not already permanently disabled
	//
	
	if( mode == kIOFWPhysicalAccessDisabledForGeneration &&
		fPhysicalAccessMode != kIOFWPhysicalAccessDisabled )
	{
		fPhysicalAccessMode = kIOFWPhysicalAccessDisabledForGeneration;

		FWKLOG(( "IOFireWireController::setPhysicalAccessMode - disable physical access for this bus generation\n" ));
	
		// shut them all down!
		fFWIM->setNodeIDPhysicalFilter( kIOFWAllPhysicalFilters, false );
	}
	
	openGate();
}

// getPhysicalAccessMode
//
//

IOFWPhysicalAccessMode IOFireWireController::getPhysicalAccessMode( void )
{
	return fPhysicalAccessMode;
}

// physicalAccessProcessBusReset
//
//

void IOFireWireController::physicalAccessProcessBusReset( void )
{
	//
	// reenable physical access if it was only disabled for a generation
	// and we're in normal security mode
	//
	
	if( fPhysicalAccessMode == kIOFWPhysicalAccessDisabledForGeneration && 
		getSecurityMode() == kIOFWSecurityModeNormal )
	{
		fPhysicalAccessMode = kIOFWPhysicalAccessEnabled;

		FWKLOG(( "IOFireWireController::physicalAccessProcessBusReset - re-enable physical access because of bus reset\n" ));
		
		// we don't reconfigure the physical filters here because :
		// 1. a bus reset has just occured and all node ids are set to kFWBadNodeID
		// 2. reconfiguring filters is done automatically after receiving self-ids
	}
}

// setNodeIDPhysicalFilter
//
//

void IOFireWireController::setNodeIDPhysicalFilter( UInt16 nodeID, bool state )
{
	// only configure node filters if the family is allowing physical access
	if( fPhysicalAccessMode == kIOFWPhysicalAccessEnabled )
	{
		FWKLOG(( "IOFireWireController::setNodeIDPhysicalFilter - set physical access for node 0x%x to %d\n", nodeID, state ));

		fFWIM->setNodeIDPhysicalFilter( nodeID, state );
	}
}
	
#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// security
//

// initSecurityState
//
//

void IOFireWireController::initSecurity( void )
{

	//
	// assume security mode is normal
	//
				
	IOFWSecurityMode mode = kIOFWSecurityModeNormal;
				
	//
	// check OpenFirmware security mode
	//
	
	{
		IORegistryEntry * options = IORegistryEntry::fromPath( "/options", gIODTPlane );
		
		if( options != NULL )
		{
			OSString * securityModeProperty = OSDynamicCast( OSString, options->getProperty("security-mode") );
	
			if( securityModeProperty != NULL && strcmp("none", securityModeProperty->getCStringNoCopy()) != 0 )
			{
				// set security mode to secure/permanent
				mode = kIOFWSecurityModeSecurePermanent;
			}
			
			options->release();
			options = NULL;
		}
	}
	
	//
	// handle secruity keyswitch
	//
	
	if( mode != kIOFWSecurityModeSecurePermanent )
	{

		//
		// check state of secruity keyswitch
		//
		
		OSIterator *	iterator		= NULL;
		OSBoolean *		keyswitchState	= NULL;
			
		iterator = getMatchingServices( nameMatching("AppleKeyswitch") );
		if( iterator != NULL )
		{
			OSObject * obj = NULL;
			if( (obj = iterator->getNextObject()) )
			{
				IOService *	service = (IOService*)obj;
				keyswitchState = OSDynamicCast( OSBoolean, service->getProperty( "Keyswitch" ) );
				
				if( keyswitchState->isTrue() )
				{
					// set security mode to secure
					mode = kIOFWSecurityModeSecure;
				}
			}
			
			iterator->release();
			iterator = NULL;
		}
		
		//
		// add notification for changes to secruity keyswitch
		//
		
		
		fKeyswitchNotifier = addNotification( gIOMatchedNotification, nameMatching( "AppleKeyswitch" ),
											  (IOServiceNotificationHandler)&IOFireWireController::serverKeyswitchCallback,
											  this, 0 );
		
	}

	//
	// now that we've determined our security mode, set it
	//
	
	setSecurityMode( mode );

}

// freeSecurity
//
//

void IOFireWireController::freeSecurity( void )
{
	// remove notification
										  
	if( fKeyswitchNotifier != NULL )
	{
		fKeyswitchNotifier->remove();
		fKeyswitchNotifier = NULL;
	}
}

// serverKeyswitchCallback
//
//

bool IOFireWireController::serverKeyswitchCallback( void * target, void * refCon, IOService * service )
{
	OSBoolean *				keyswitchState	= NULL;
	IOFireWireController *	me				= NULL;
	
	keyswitchState = OSDynamicCast( OSBoolean, service->getProperty( "Keyswitch" ) );
	
	me = OSDynamicCast( IOFireWireController, (OSObject *)target );
	
	if( keyswitchState != NULL && me != NULL )
	{
		// Is the key unlocked?
		
		if( keyswitchState->isFalse() )
		{
			// Key is unlocked, set security mode to normal
			
			me->setSecurityMode( kIOFWSecurityModeNormal );
		}
		else if( keyswitchState->isTrue() )
		{
			// Key is locked, set security mode to secure
	
			me->setSecurityMode( kIOFWSecurityModeSecure );
		}
		
	}
	
	return true;	
}

// setSecurityMode
//
//

void IOFireWireController::setSecurityMode( IOFWSecurityMode mode )
{
	closeGate();

	fSecurityMode = mode;
	
	switch( fSecurityMode )
	{
		case kIOFWSecurityModeNormal:
			
			FWKLOG(( "IOFireWireController::setSecurityMode - set mode to normal\n" ));
	
			// enable physical access
			fFWIM->setSecurityMode( mode );
			setPhysicalAccessMode( kIOFWPhysicalAccessEnabled );
			break;
			
		case kIOFWSecurityModeSecure:
		case kIOFWSecurityModeSecurePermanent:
		
			FWKLOG(( "IOFireWireController::setSecurityMode - set mode to secure\n" ));
	
			// disable physical access
			fFWIM->setSecurityMode( mode );
			setPhysicalAccessMode( kIOFWPhysicalAccessDisabled );
			break;
			
		default:
			IOLog( "IOFireWireController::setSecurityMode - illegal security mode = 0x%x\n", fSecurityMode );
			break;
	}
	
	openGate();
}

// getSecurityMode
//
//

IOFWSecurityMode IOFireWireController::getSecurityMode( void )
{
	return fSecurityMode;
}

#pragma mark -

/////////////////////////////////////////////////////////////////////////////
// local config rom
//

// getRootDir
//
//

IOLocalConfigDirectory * IOFireWireController::getRootDir() const 
{ 
	return fRootDir; 
}

// AddUnitDirectory
//
//

IOReturn IOFireWireController::AddUnitDirectory(IOLocalConfigDirectory *unitDir)
{
    IOReturn res;
    
	closeGate();
    
	getRootDir()->addEntry(kConfigUnitDirectoryKey, unitDir);

    res = UpdateROM();
    if(res == kIOReturnSuccess)
        res = resetBus();
    
	openGate();
    
	return res;
}

// RemoveUnitDirectory
//
//

IOReturn IOFireWireController::RemoveUnitDirectory(IOLocalConfigDirectory *unitDir)
{
    IOReturn res;
    
	closeGate();
    
	getRootDir()->removeSubDir(unitDir);

    res = UpdateROM();
    if(res == kIOReturnSuccess)
        res = resetBus();
    
	openGate();
    
	return res;
}

// UpdateROM()
//
//   Instantiate the local Config ROM.
//   Always causes at least one bus reset.

IOReturn IOFireWireController::UpdateROM()
{
    UInt32 *				hack;
    UInt32 					crc;
    unsigned int 			numQuads;
    OSData *				rom;
    IOReturn				ret;
    UInt32					generation;
    IOFireWireLocalNode *	localNode;

    // Increment the 4 bit generation field, make sure it is at least two.
    generation = fROMHeader[2] & kFWBIBGeneration;
    generation += (1 << kFWBIBGenerationPhase);
    generation &= kFWBIBGeneration;
    if(generation < (2 << kFWBIBGenerationPhase))
        generation = (2 << kFWBIBGenerationPhase);

    fROMHeader[2] = (fROMHeader[2] & ~kFWBIBGeneration) | generation;
    
    rom = OSData::withBytes(&fROMHeader, sizeof(fROMHeader));
    fRootDir->compile(rom);

    // Now hack in correct CRC and length.
    hack = (UInt32 *)rom->getBytesNoCopy();
    numQuads = rom->getLength()/sizeof(UInt32) - 1;
    crc = FWComputeCRC16 (hack + 1, numQuads);
    *hack = (((sizeof(fROMHeader)/sizeof(UInt32)-1) <<
              kConfigBusInfoBlockLengthPhase) &
                                        kConfigBusInfoBlockLength) |
        ((numQuads << kConfigROMCRCLengthPhase) & kConfigROMCRCLength) |
        ((crc << kConfigROMCRCValuePhase) & kConfigROMCRCValue);

    localNode = getLocalNode(this);
    if(localNode)
        localNode->setProperty(gFireWireROM, rom);
    
#if 0
    {
        unsigned int i;
        IOLog("--------- FW Local ROM: --------\n");
        for(i=0; i<numQuads+1; i++)
            IOLog("ROM[%d] = 0x%x\n", i, hack[i]);
    }
#endif
    if(fROMAddrSpace) 
	{
        freeAddress( fROMAddrSpace );
        fROMAddrSpace->release();
        fROMAddrSpace = NULL;
    }
    
    fROMAddrSpace = IOFWPseudoAddressSpace::simpleReadFixed( this,
        FWAddress(kCSRRegisterSpaceBaseAddressHi, kConfigROMBaseAddress),
        (numQuads+1)*sizeof(UInt32), rom->getBytesNoCopy());
    ret = allocAddress(fROMAddrSpace);
    if(kIOReturnSuccess == ret) 
	{
        ret = fFWIM->updateROM(rom);
    }
    rom->release();
    return ret ;
}

#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// async request transmit
//

// allocTrans
//
//

AsyncPendingTrans *IOFireWireController::allocTrans(IOFWAsyncCommand *cmd)
{
    unsigned int i;
    unsigned int tran;

    tran = fLastTrans;
    for(i=0; i<kMaxPendingTransfers; i++) {
        AsyncPendingTrans *t;
        tran++;
        if(tran >= kMaxPendingTransfers)
            tran = 0;
        t = &fTrans[tran];
        if(!t->fInUse) {
            t->fHandler = cmd;
            t->fInUse = true;
            t->fTCode = tran;
            fLastTrans = tran;
            return t;
        }
    }
    IOLog("Out of FireWire transaction labels!\n");
    return NULL;
}

// freeTrans
//
//

void IOFireWireController::freeTrans(AsyncPendingTrans *trans)
{
    // No lock needed - can't have two users of a tcode.
    trans->fHandler = NULL;
    trans->fInUse = false;
}

// asyncRead
//
//

// Route packet sending to FWIM if checks out OK
IOReturn IOFireWireController::asyncRead(	UInt32 				generation, 
											UInt16 				nodeID, 
											UInt16 				addrHi, 
											UInt32 				addrLo,
											int 				speed, 
											int 				label, 
											int 				size, 
											IOFWAsyncCommand *	cmd)
{
    if( !checkGeneration(generation) ) 
	{
        return kIOFireWireBusReset;
    }

    // Check if local node

    if( nodeID == fLocalNodeID ) 
	{
        UInt32 rcode;
        IOMemoryDescriptor *buf;
        IOByteCount offset;
        IOFWSpeed temp = (IOFWSpeed)speed;
        
		rcode = doReadSpace(	nodeID, 
								temp, 
								FWAddress(addrHi, addrLo), 
								size,
								&buf, 
								&offset, 
								(IOFWRequestRefCon)label );
								
        if(rcode == kFWResponseComplete)
		{
			void * bytes = IOMalloc( size );
			
			buf->readBytes( offset, bytes, size );
            
			cmd->gotPacket( rcode, bytes, size );
			
			IOFree( bytes, size );
        }
		else
        {
		    cmd->gotPacket( rcode, NULL, 0 );
        }
		
		return kIOReturnSuccess;
    }
    else
	{
		// reliabilty is more important than speed for IRM access
		// perform IRM access at s100
		
		int actual_speed = speed;
		if( addrHi == kCSRRegisterSpaceBaseAddressHi )
        {
			if( (addrLo == kCSRBandwidthAvailable) ||
				(addrLo == kCSRChannelsAvailable31_0) ||
				(addrLo == kCSRChannelsAvailable63_32) ||
				(addrLo == kCSRBusManagerID) )
			{
				actual_speed = kFWSpeed100MBit;
			}
		}
		
        return fFWIM->asyncRead( nodeID, addrHi, addrLo, actual_speed, label, size, cmd );
	}
}

// asyncWrite
//
// DEPRECATED

IOReturn IOFireWireController::asyncWrite(	UInt32 				generation, 
											UInt16 				nodeID, 
											UInt16 				addrHi, 
											UInt32 				addrLo,
											int 				speed, 
											int 				label, 
											void *				data, 
											int 				size, 
											IOFWAsyncCommand *	cmd)
{
	IOLog( "IOFireWireController::asyncWrite : DEPRECATED API\n" );
	return kIOReturnUnsupported;
}

// asyncWrite
//
//

IOReturn IOFireWireController::asyncWrite(	UInt32 					generation, 
											UInt16 					nodeID, 
											UInt16 					addrHi, 
											UInt32 					addrLo,
											int 					speed, 
											int 					label, 
											IOMemoryDescriptor *	buf, 
											IOByteCount 			offset,
											int 					size, 
											IOFWAsyncCommand *		cmd )
{
	return asyncWrite(	generation,
						nodeID,
						addrHi,
						addrLo,
						speed,
						label,
						buf,
						offset,
						size,
						cmd,
						kIOFWWriteFlagsNone );
}

// asyncWrite
//
//

IOReturn IOFireWireController::asyncWrite(	UInt32 					generation, 
											UInt16 					nodeID, 
											UInt16 					addrHi, 
											UInt32 					addrLo,
											int 					speed, 
											int 					label, 
											IOMemoryDescriptor *	buf, 
											IOByteCount 			offset,
											int 					size, 
											IOFWAsyncCommand *		cmd,
											IOFWWriteFlags 			flags )
{
//	IOLog( "IOFireWireController::asyncWrite\n" );

    if( !checkGeneration(generation) ) 
	{
        return kIOFireWireBusReset;
    }

    // Check if local node
    if( nodeID == fLocalNodeID ) 
	{
        UInt32 rcode;
        IOFWSpeed temp = (IOFWSpeed)speed;
        
		void * bytes = IOMalloc( size );
			
		buf->readBytes( offset, bytes, size );
            
		rcode = doWriteSpace(	nodeID, 
								temp, 
								FWAddress( addrHi, addrLo ), 
								size,
								bytes, 
								(IOFWRequestRefCon)label );
        
		IOFree( bytes, size );
			
		cmd->gotPacket(rcode, NULL, 0);
        
		return kIOReturnSuccess;
    }
    else
	{
		// reliabilty is more important than speed for IRM access
		// perform IRM access at s100
	
		// actually writes to the IRM are not allowed, so we are doing 
		// this more for consistency than necessity
		
		int actual_speed = speed;
		if( addrHi == kCSRRegisterSpaceBaseAddressHi )
        {
			if( (addrLo == kCSRBandwidthAvailable) ||
				(addrLo == kCSRChannelsAvailable31_0) ||
				(addrLo == kCSRChannelsAvailable63_32) ||
				(addrLo == kCSRBusManagerID) )
			{
				actual_speed = kFWSpeed100MBit;
			}
		}
		
        return fFWIM->asyncWrite( 	nodeID, 
									addrHi, 
									addrLo, 
									actual_speed, 
									label, 
									buf, 
									offset, 
									size, 
									cmd,
									flags );
	}
}

// asyncLock
//
// DEPRECATED

IOReturn IOFireWireController::asyncLock(	UInt32 				generation, 
											UInt16 				nodeID, 
											UInt16 				addrHi, 
											UInt32 				addrLo,
											int 				speed, 
											int 				label, 
											int 				type, 
											void *				data, 
											int 				size, 
											IOFWAsyncCommand *	cmd )
{
	IOLog( "IOFireWireController::asyncLock : DEPRECATED API\n" );
	return kIOReturnUnsupported;
}

// asyncLock
//
//

IOReturn IOFireWireController::asyncLock(	UInt32 					generation, 
											UInt16 					nodeID, 
											UInt16 					addrHi, 
											UInt32 					addrLo,
											int 					speed, 
											int 					label, 
											int 					type, 
											IOMemoryDescriptor *	buf, 
											IOByteCount 			offset,
											int 					size, 
											IOFWAsyncCommand *		cmd )
					
{
    if( !checkGeneration(generation) ) 
	{
        return kIOFireWireBusReset;
    }

    // Check if local node
    if( nodeID == fLocalNodeID ) 
	{
        UInt32 rcode;
        UInt32 retVals[2];
        UInt32 retSize = sizeof(retVals);
        
		IOFWSpeed temp = (IOFWSpeed)speed;
        IOFWRequestRefCon refcon = (IOFWRequestRefCon)(label | kRequestIsLock | (type << kRequestExtTCodeShift));
        
		void * bytes = IOMalloc( size );
			
		buf->readBytes( offset, bytes, size );
            
		rcode = doLockSpace(	nodeID, 
								temp, 
								FWAddress(addrHi, addrLo), 
								size,
								(const UInt32*)bytes, 
								retSize, 
								retVals, 
								type, 
								refcon );
		
		IOFree( bytes, size );
								
        cmd->gotPacket( rcode, retVals, retSize );
        
		return kIOReturnSuccess;
    }
    else
	{
		// reliabilty is more important than speed for IRM access
		// perform IRM access at s100
		
		int actual_speed = speed;
		if( addrHi == kCSRRegisterSpaceBaseAddressHi )
        {
			if( (addrLo == kCSRBandwidthAvailable) ||
				(addrLo == kCSRChannelsAvailable31_0) ||
				(addrLo == kCSRChannelsAvailable63_32) ||
				(addrLo == kCSRBusManagerID) )
			{
				actual_speed = kFWSpeed100MBit;
			}
		}
		
		return fFWIM->asyncLock(	nodeID, 
									addrHi, 
									addrLo, 
									actual_speed, 
									label, 
									type, 
									buf,
									offset,
									size, 
									cmd );
	}
}

// handleAsyncTimeout
//
//

IOReturn IOFireWireController::handleAsyncTimeout(IOFWAsyncCommand *cmd)
{
    return fFWIM->handleAsyncTimeout(cmd);
}

// asyncStreamWrite
//
//

IOReturn IOFireWireController::asyncStreamWrite(UInt32 generation,
                    int speed, int tag, int sync, int channel,
                    IOMemoryDescriptor *buf, IOByteCount offset,
                	int size, IOFWAsyncStreamCommand *cmd)
{
    if(!checkGeneration(generation)) {
        return (kIOFireWireBusReset);
    }

	return fFWIM->asyncStreamTransmit((UInt32)channel, speed, (UInt32) sync, (UInt32) tag, buf, offset, size, cmd);
}

// createAsyncStreamCommand
//
//

IOFWAsyncStreamCommand * IOFireWireController::createAsyncStreamCommand( UInt32 generation,
    			UInt32 channel, UInt32 sync, UInt32 tag, IOMemoryDescriptor *hostMem,
    			UInt32 size, int speed, FWAsyncStreamCallback completion, void *refcon)
{
    IOFWAsyncStreamCommand * cmd;

    cmd = new IOFWAsyncStreamCommand;
    if(cmd) {
        if(!cmd->initAll(this, generation, channel, sync, tag, hostMem,size,speed,
                         completion, refcon)) {
            cmd->release();
            cmd = NULL;
		}
    }
    return cmd;
}

/////////////////////////////////////////////////////////////////////////////
// async receive
//

// processRcvPacket
//
// dispatch received Async packet based on tCode.

void IOFireWireController::processRcvPacket(UInt32 *data, int size)
{
#if 0
    int i;
kprintf("Received packet 0x%x size %d\n", data, size);
    for(i=0; i<size; i++) {
	kprintf("0x%x ", data[i]);
    }
    kprintf("\n");
#endif
    UInt32	tCode, tLabel;
    UInt32	quad0;
    UInt16	sourceID;

    // Get first quad.
    quad0 = *data;

    tCode = (quad0 & kFWPacketTCode) >> kFWPacketTCodePhase;
    tLabel = (quad0 & kFWAsynchTLabel) >> kFWAsynchTLabelPhase;
    sourceID = (data[1] & kFWAsynchSourceID) >> kFWAsynchSourceIDPhase;

    // Dispatch processing based on tCode.
    switch (tCode)
    {
        case kFWTCodeWriteQuadlet :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("WriteQuadlet: addr 0x%x:0x%x\n", 
		(data[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, data[2]);
#endif
            processWriteRequest(sourceID, tLabel, data, &data[3], 4);
            break;

        case kFWTCodeWriteBlock :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("WriteBlock: addr 0x%x:0x%x\n", 
		(data[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, data[2]);
#endif
            processWriteRequest(sourceID, tLabel, data, &data[4],
			(data[3] & kFWAsynchDataLength) >> kFWAsynchDataLengthPhase);
            break;

        case kFWTCodeWriteResponse :
            if(fTrans[tLabel].fHandler) {
                IOFWAsyncCommand * cmd = fTrans[tLabel].fHandler;
		cmd->gotPacket((data[1] & kFWAsynchRCode)>>kFWAsynchRCodePhase, 0, 0);
            }
            else {
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("WriteResponse: label %d isn't in use!!, data1 = 0x%x\n",
                     tLabel, data[1]);
#endif
            }
            break;

        case kFWTCodeReadQuadlet :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("ReadQuadlet: addr 0x%x:0x%x\n", 
		(data[1] & kFWAsynchDestinationOffsetHigh) >>
                     kFWAsynchDestinationOffsetHighPhase, data[2]);
#endif
            {
                UInt32 ret;
                FWAddress addr((data[1] & kFWAsynchDestinationOffsetHigh) >>  kFWAsynchDestinationOffsetHighPhase, data[2]);
                IOFWSpeed speed = FWSpeed(sourceID);
                IOMemoryDescriptor *buf = NULL;
				IOByteCount offset;
                
				ret = doReadSpace(sourceID, speed, addr, 4,
                                    &buf, &offset, (IOFWRequestRefCon)(tLabel | kRequestIsQuad));
               
                if(ret == kFWResponsePending)
                    break;
                
				if( NULL != buf ) 
				{
					UInt32 quad = 0xdeadbeef;
						
					buf->readBytes( offset, &quad, 4 );
						
					fFWIM->asyncReadQuadResponse(sourceID, speed, tLabel, ret, quad );
                }
                else 
				{
                    fFWIM->asyncReadQuadResponse(sourceID, speed, tLabel, ret, 0xdeadbeef);
                }
            }
            break;

        case kFWTCodeReadBlock :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("ReadBlock: addr 0x%x:0x%x len %d\n", 
		(data[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, data[2],
		(data[3] & kFWAsynchDataLength) >> kFWAsynchDataLengthPhase);
#endif
            {
                IOReturn ret;
                int 					length = (data[3] & kFWAsynchDataLength) >> kFWAsynchDataLengthPhase ;
                FWAddress 	addr((data[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, data[2]);
                IOFWSpeed 				speed = FWSpeed(sourceID);
                IOMemoryDescriptor *	buf = NULL;
				IOByteCount offset;

                ret = doReadSpace(sourceID, speed, addr, length,
                                    &buf, &offset, (IOFWRequestRefCon)(tLabel));
                if(ret == kFWResponsePending)
                    break;
                if(NULL != buf) {
                    fFWIM->asyncReadResponse(sourceID, speed,
                                       tLabel, ret, buf, offset, length);
                }
                else {
                    fFWIM->asyncReadResponse(sourceID, speed,
                                       tLabel, ret, fBadReadResponse, 0, 4);
                }
            }
            break;

        case kFWTCodeReadQuadletResponse :
            if(fTrans[tLabel].fHandler) {
                IOFWAsyncCommand * cmd = fTrans[tLabel].fHandler;
				FWAddress commandAddress = cmd->getAddress();
				
            	if( sourceID == commandAddress.nodeID )
            	{
            	
					cmd->gotPacket((data[1] & kFWAsynchRCode)>>kFWAsynchRCodePhase,
										(const void*)(data+3), 4);
				}
				else
				{
#if (DEBUGGING_LEVEL > 0)
					DEBUGLOG( "Response from wrong node ID!\n" );
#endif
				}
				
            }
            else {
#if (DEBUGGING_LEVEL > 0)
		DEBUGLOG("ReadQuadletResponse: label %d isn't in use!!\n", tLabel);
#endif
            }
            break;

        case kFWTCodeReadBlockResponse :
        case kFWTCodeLockResponse :
            if(fTrans[tLabel].fHandler) {
            	
				IOFWAsyncCommand * cmd = fTrans[tLabel].fHandler;
				FWAddress commandAddress = cmd->getAddress();
				
            	if( sourceID == commandAddress.nodeID )
            	{
            	
					cmd->gotPacket((data[1] & kFWAsynchRCode)>>kFWAsynchRCodePhase,
					(const void*)(data+4), (data[3] & kFWAsynchDataLength)>>kFWAsynchDataLengthPhase);
				}
				else
				{
#if (DEBUGGING_LEVEL > 0)
					DEBUGLOG( "Response from wrong node ID!\n" );
#endif
				}
            }
            else {
#if (DEBUGGING_LEVEL > 0)
				DEBUGLOG("ReadBlock/LockResponse: label %d isn't in use!!\n", tLabel);
#endif
            }
            break;

        case kFWTCodeLock :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("Lock type %d: addr 0x%x:0x%x\n", 
		(data[3] & kFWAsynchExtendedTCode) >> kFWAsynchExtendedTCodePhase,
		(data[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase,
		data[2]);
#endif
            processLockRequest(sourceID, tLabel, data, &data[4],
			(data[3] & kFWAsynchDataLength) >> kFWAsynchDataLengthPhase);

            break;

        case kFWTCodeIsochronousBlock :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("Async Stream Packet\n");
#endif
            break;

        default :
#if (DEBUGGING_LEVEL > 0)
            DEBUGLOG("Unexpected tcode in Asyncrecv: %d\n", tCode);
#endif
            break;
    }	
}

/////////////////////////////////////////////////////////////////////////////
// async request receive
//

// createPhysicalAddressSpace
//
//

IOFWPhysicalAddressSpace *
IOFireWireController::createPhysicalAddressSpace(IOMemoryDescriptor *mem)
{
    IOFWPhysicalAddressSpace *space;
    space = new IOFWPhysicalAddressSpace;
    if(!space)
        return NULL;
    if(!space->initWithDesc(this, mem)) {
        space->release();
        space = NULL;
    }
    return space;
}

// createPseudoAddressSpace
//
//

IOFWPseudoAddressSpace *
IOFireWireController::createPseudoAddressSpace(FWAddress *addr, UInt32 len,
                            FWReadCallback reader, FWWriteCallback writer, void *refcon)
{
    IOFWPseudoAddressSpace *space;
    space = new IOFWPseudoAddressSpace;
    if(!space)
        return NULL;
    if(!space->initAll(this, addr, len, reader, writer, refcon)) {
        space->release();
        space = NULL;
    }
    return space;
}

// createInitialAddressSpace
//
//

IOFWPseudoAddressSpace *
IOFireWireController::createInitialAddressSpace(UInt32 addressLo, UInt32 len,
                            FWReadCallback reader, FWWriteCallback writer, void *refcon)
{
    IOFWPseudoAddressSpace *space;
    space = new IOFWPseudoAddressSpace;
    if(!space)
        return NULL;
    if(!space->initFixed(this, FWAddress(kCSRRegisterSpaceBaseAddressHi, addressLo),
            len, reader, writer, refcon)) {
        space->release();
        space = NULL;
    }
    return space;
}

// getAddressSpace
//
//

IOFWAddressSpace *
IOFireWireController::getAddressSpace(FWAddress address)
{
    closeGate();
    
	IOFWAddressSpace * found;
    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject())) {
        if(found->contains(address))
            break;
    }
    
	openGate();
    
	return found;
}

// allocAddress
//
//

IOReturn IOFireWireController::allocAddress(IOFWAddressSpace *space)
{
    /*
     * Lots of scope for optimizations here, perhaps building a hash table for
     * addresses etc.
     * Drivers may want to override this if their hardware can match addresses
     * without CPU intervention.
     */
    IOReturn res;
    
	closeGate();
    
	if(!fLocalAddresses->setObject(space))
        res = kIOReturnNoMemory;
    else
        res = kIOReturnSuccess;
    
	openGate();
    
	return res;
}

// freeAddress
//
//

void IOFireWireController::freeAddress(IOFWAddressSpace *space)
{
    closeGate();
	
	fLocalAddresses->removeObject(space);
	
	openGate();
}

// allocatePseudoAddress
//
//

IOReturn IOFireWireController::allocatePseudoAddress(FWAddress *addr, UInt32 lenDummy)
{
    unsigned int i, len;
    UInt8 * data;
    UInt8 used = 1;
    
    closeGate();
    
    if( fAllocatedAddresses == NULL ) 
    {
        fAllocatedAddresses = OSData::withCapacity(4);	// SBP2 + some spare
        fAllocatedAddresses->appendBytes(&used, 1);	// Physical always allocated
    }
    
    if( !fAllocatedAddresses )
    {   
        openGate();
        return kIOReturnNoMemory;
    }
    
    len = fAllocatedAddresses->getLength();
    data = (UInt8*)fAllocatedAddresses->getBytesNoCopy();
    for( i=0; i<len; i++ ) 
    {
        if( data[i] == 0 ) 
        {
            data[i] = 1;
            addr->addressHi = i;
            addr->addressLo = 0;
        
            openGate();
            return kIOReturnSuccess;
        }
    }
    
    if( len >= 0xfffe )
    {
        openGate();
		return kIOReturnNoMemory;
    }
    
    if( fAllocatedAddresses->appendBytes(&used, 1)) 
    {
        addr->addressHi = len;
        addr->addressLo = 0;
    
        openGate();
        return kIOReturnSuccess;
    }

    openGate();
    return kIOReturnNoMemory;      
}

// freePseudoAddress
//
//

void IOFireWireController::freePseudoAddress(FWAddress addr, UInt32 lenDummy)
{
    unsigned int len;
    UInt8 * data;
    
    closeGate();
    
    assert( fAllocatedAddresses != NULL);
    
    len = fAllocatedAddresses->getLength();
    assert(addr.addressHi < len);
    data = (UInt8*)fAllocatedAddresses->getBytesNoCopy();
    assert(data[addr.addressHi]);
    data[addr.addressHi] = 0;
    
    openGate();
}

// processWriteRequest
//
// process quad and block writes.

void IOFireWireController::processWriteRequest(UInt16 sourceID, UInt32 tLabel,
			UInt32 *hdr, void *buf, int len)
{
    UInt32 ret = kFWResponseAddressError;
    IOFWSpeed speed = FWSpeed(sourceID);
    FWAddress addr((hdr[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, hdr[2]);
    IOFWAddressSpace * found;
    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject())) {
        ret = found->doWrite(sourceID, speed, addr, len, buf, (IOFWRequestRefCon)tLabel);
        if(ret != kFWResponseAddressError)
            break;
    }
    fFWIM->asyncWriteResponse(sourceID, speed, tLabel, ret, addr.addressHi);
}

// processLockRequest
//
// process 32 and 64 bit locks.

void IOFireWireController::processLockRequest(UInt16 sourceID, UInt32 tLabel,
			UInt32 *hdr, void *buf, int len)
{
    UInt32 oldVal[2];
    UInt32 ret;
    UInt32 outLen =sizeof(oldVal);
    int type = (hdr[3] &  kFWAsynchExtendedTCode) >> kFWAsynchExtendedTCodePhase;

    FWAddress addr((hdr[1] & kFWAsynchDestinationOffsetHigh) >> kFWAsynchDestinationOffsetHighPhase, hdr[2]);
    IOFWSpeed speed = FWSpeed(sourceID);

    IOFWRequestRefCon refcon = (IOFWRequestRefCon)(tLabel | kRequestIsLock | (type << kRequestExtTCodeShift));

    ret = doLockSpace(sourceID, speed, addr, len, (const UInt32 *)buf, outLen, oldVal, type, refcon);
    if(ret != kFWResponsePending)
    {
        fFWIM->asyncLockResponse(sourceID, speed, tLabel, ret, type, oldVal, outLen);
    }
}

// doReadSpace
//
//

UInt32 IOFireWireController::doReadSpace(UInt16 nodeID, IOFWSpeed &speed, FWAddress addr, UInt32 len,
                                                IOMemoryDescriptor **buf, IOByteCount * offset,
                                                IOFWRequestRefCon refcon)
{
    IOFWAddressSpace * found;
    UInt32 ret = kFWResponseAddressError;
    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject())) {
        ret = found->doRead(nodeID, speed, addr, len, buf, offset,
                            refcon);
        if(ret != kFWResponseAddressError)
            break;
    }
    return ret;
}

// doWriteSpace
//
//

UInt32 IOFireWireController::doWriteSpace(UInt16 nodeID, IOFWSpeed &speed, FWAddress addr, UInt32 len,
                                            const void *buf, IOFWRequestRefCon refcon)
{
    IOFWAddressSpace * found;
    UInt32 ret = kFWResponseAddressError;
    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject())) {
        ret = found->doWrite(nodeID, speed, addr, len, buf, refcon);
        if(ret != kFWResponseAddressError)
            break;
    }
    return ret;
}

// doLockSpace
//
//

UInt32 IOFireWireController::doLockSpace(UInt16 nodeID, IOFWSpeed &speed, FWAddress addr, UInt32 inLen,
                                         const UInt32 *newVal,  UInt32 &outLen, UInt32 *oldVal, UInt32 type,
                                                IOFWRequestRefCon refcon)
{
    IOFWAddressSpace * found;
    UInt32 ret = kFWResponseAddressError;
    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject())) {
        ret = found->doLock(nodeID, speed, addr, inLen, newVal, outLen, oldVal, type, refcon);
        if(ret != kFWResponseAddressError)
            break;
    }

    if(ret != kFWResponseComplete) {
        oldVal[0] = 0xdeadbabe;
        outLen = 4;
    }
    return ret;
}

// handleARxReqIntComplete
//
//

void IOFireWireController::handleARxReqIntComplete( void )
{
    IOFWAddressSpace * found;

    fSpaceIterator->reset();
    while( (found = (IOFWAddressSpace *) fSpaceIterator->getNextObject()) ) 
	{
		IOFWPseudoAddressSpace * space = OSDynamicCast( IOFWPseudoAddressSpace, found );
		if( space != NULL )
		{
			space->handleARxReqIntComplete();
		}
    }
}

// isLockRequest
//
//

bool IOFireWireController::isLockRequest(IOFWRequestRefCon refcon)
{
    return ((UInt32)refcon) & kRequestIsLock;
}

// isQuadRequest
//
//

bool IOFireWireController::isQuadRequest(IOFWRequestRefCon refcon)
{
    return ((UInt32)refcon) & kRequestIsQuad;
}

// isCompleteRequest
//
//

bool IOFireWireController::isCompleteRequest(IOFWRequestRefCon refcon)
{
    return ((UInt32)refcon) & kRequestIsComplete;
}

// getExtendedTCode
//
//

UInt32 IOFireWireController::getExtendedTCode(IOFWRequestRefCon refcon)
{
    return((UInt32)refcon & kRequestExtTCodeMask) >> kRequestExtTCodeShift;
}

/////////////////////////////////////////////////////////////////////////////
// async response transmit
//

// asyncReadResponse
//
// Send async read response packets
// useful for pseudo address spaces that require servicing outside the FireWire work loop.

IOReturn IOFireWireController::asyncReadResponse(	UInt32 					generation, 
													UInt16 					nodeID, 
													int 					speed,
													IOMemoryDescriptor *	buf, 
													IOByteCount 			offset, 
													int 					size,
													IOFWRequestRefCon 		refcon )
{
    IOReturn result;
    UInt32 params = (UInt32)refcon;
    UInt32 label = params & kRequestLabel;

    closeGate();
    
	if( !checkGeneration(generation) )
	{
        result = kIOFireWireBusReset;
    }
	else if( params & kRequestIsQuad )
	{
		UInt32 quad = 0xdeadbeef;
								
		buf->readBytes( offset, &quad, 4 );

		result = fFWIM->asyncReadQuadResponse(	nodeID, 
												speed, 
												label, 
												kFWResponseComplete,
												quad );
    }
	else
    {
	    result = fFWIM->asyncReadResponse(	nodeID, 
											speed, 
											label, 
											kFWResponseComplete, 
											buf, 
											offset, 
											size );
    }
	
	openGate();

    return result;
}

// asyncLockResponse
//
// Send async lock response packets
// useful for pseudo address spaces that require servicing outside the FireWire work loop.

IOReturn IOFireWireController::asyncLockResponse( 	UInt32 					generation, 
													UInt16 					nodeID, 
													int 					speed,
													IOMemoryDescriptor *	buf, 
													IOByteCount 			offset, 
													int 					size,
													IOFWRequestRefCon 		refcon )
{
    IOReturn result;
    UInt32 params = (UInt32)refcon;
    UInt32 label = params & kRequestLabel;

    closeGate();
    
	if( !checkGeneration(generation) )
	{
        result = kIOFireWireBusReset;
    }
	else
    {	
		void * bytes = IOMalloc( size );
			
		buf->readBytes( offset, bytes, size );
    
		result = fFWIM->asyncLockResponse(	nodeID, 
											speed, 
											label, 
											kFWResponseComplete, 
											getExtendedTCode(refcon), 
											bytes, 
											size );

		IOFree( bytes, size );

    }
    
    openGate();

    return result;
}

/////////////////////////////////////////////////////////////////////////////
// timer command
//

// createDelayedCmd
//
//

IOFWDelayCommand * IOFireWireController::createDelayedCmd(UInt32 uSecDelay, FWBusCallback func, void *refcon)
{
    IOFWDelayCommand *delay;
    //IOLog("Creating delay of %d\n", uSecDelay);
    delay = new IOFWDelayCommand;
    if(!delay)
        return NULL;

    if(!delay->initWithDelay(this, uSecDelay, func, refcon)) 
	{
		delay->release();
        return NULL;
    }
	
    return delay;
}

#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// isoch
//

// createIsochChannel
//
//

IOFWIsochChannel *IOFireWireController::createIsochChannel(	bool 		doIRM, 
															UInt32 		bandwidth, 
															IOFWSpeed 	prefSpeed,
															FWIsochChannelForceStopNotificationProc	stopProc, 
															void *		stopRefCon )
{
	// NOTE: if changing this code, must also change IOFireWireUserClient::isochChannelAllocate()

    IOFWIsochChannel *channel;

    channel = new IOFWIsochChannel;
    if(!channel)
	{
		return NULL;
	}
	
    if( !channel->init(this, doIRM, bandwidth, prefSpeed, stopProc, stopRefCon) ) 
	{
		channel->release();
		channel = NULL;
    }
	
    return channel;
}

// createLocalIsochPort
//
//

IOFWLocalIsochPort *IOFireWireController::createLocalIsochPort(	bool 			talking,
																DCLCommand *	opcodes, 
																DCLTaskInfo *	info,
																UInt32 			startEvent, 
																UInt32 			startState, 
																UInt32 			startMask )
{
    IOFWLocalIsochPort *port;
    IODCLProgram *program;
	
    program = fFWIM->createDCLProgram( talking, opcodes, info, startEvent, startState, startMask );
    if(!program)
		return NULL;

    port = new IOFWLocalIsochPort;
    if( !port ) 
	{
		program->release();
		return NULL;
    }

    if(!port->init(program, this)) 
	{
		port->release();
		port = NULL;
    }

    return port;
}

// addAllocatedChannel
//
//

void IOFireWireController::addAllocatedChannel(IOFWIsochChannel *channel)
{
    closeGate();
    
	fAllocatedChannels->setObject(channel);
    
	openGate();
}

// removeAllocatedChannel
//
//

void IOFireWireController::removeAllocatedChannel(IOFWIsochChannel *channel)
{
    closeGate();
    
	fAllocatedChannels->removeObject(channel);
    
	openGate();
}

#pragma mark -
////////////////////////////////////////////////////////////////////////////

// getLocalNode
//
// static method to fetch the local node for a controller

IOFireWireLocalNode * IOFireWireController::getLocalNode(IOFireWireController *control)
{
    OSIterator *childIterator;
    IOFireWireLocalNode *localNode = NULL;
    childIterator = control->getClientIterator();
    if( childIterator) {
        OSObject * child;
        while( (child = childIterator->getNextObject())) {
            localNode = OSDynamicCast(IOFireWireLocalNode, child);
            if(localNode) {
                break;
            }
        }
        childIterator->release();
    }
    return localNode;
}

// getBusPowerManager
//
//

IOFireWirePowerManager * IOFireWireController::getBusPowerManager( void )
{
	return fBusPowerManager;
}

// getWorkLoop
//
//

IOWorkLoop * IOFireWireController::getWorkLoop() const
{
    return fWorkLoop;
}

// getLink
//
//

IOFireWireLink * IOFireWireController::getLink() const 
{ 
	return fFWIM;
}

// getCycleTime
//
//

IOReturn IOFireWireController::getCycleTime(UInt32 &cycleTime)
{
    // Have to take workloop lock, in case the hardware is sleeping.
    
	IOReturn res;
    
	closeGate();
    
	res = fFWIM->getCycleTime(cycleTime);
    
	openGate();
    
	return res;
}

// getBusCycleTime
//
//

IOReturn IOFireWireController::getBusCycleTime(UInt32 &busTime, UInt32 &cycleTime)
{
    // Have to take workloop lock, in case the hardware is sleeping.
    IOReturn res;
    UInt32 cycleSecs;
    
	closeGate();
    
	res = fFWIM->getBusCycleTime(busTime, cycleTime);
    
	openGate();
    
	if(res == kIOReturnSuccess) {
        // Bottom 7 bits of busTime should be same as top 7 bits of cycle time.
        // However, link only updates bus time every few seconds,
        // so use cycletime for overlapping bits and check for cycletime wrap
        cycleSecs = cycleTime >> 25;
        // Update bus time.
        if((busTime & 0x7F) > cycleSecs) {
            // Must have wrapped, increment top part of busTime.
            cycleSecs += 0x80;
        }
        busTime = (busTime & ~0x7F) + cycleSecs;            
    }
    return res;
}

// hopCount
//
//

UInt32 IOFireWireController::hopCount(UInt16 nodeAAddress, UInt16 nodeBAddress )
{	
	closeGate();
	
	UInt32 hops = fHopCounts[(kFWMaxNodesPerBus+1)*(nodeAAddress & 63)+(nodeBAddress & 63)];

//	IOLog( "IOFireWireController::hopCount - nodeIDA = 0x%04x, nodeIDB = 0x%04x, hops = %d\n", nodeAAddress & 63, nodeBAddress & 63, hops );
	
	openGate();
	
	return hops;
}

// hopCount
//
//

UInt32 IOFireWireController::hopCount( UInt16 nodeA )
{
	closeGate();
	
	UInt32 hops = hopCount( nodeA, fLocalNodeID );
	
	openGate();
	
	return hops;
}

// FWSpeed
//
//

IOFWSpeed IOFireWireController::FWSpeed(UInt16 nodeAddress) const
{
	return (IOFWSpeed)fSpeedCodes[(kFWMaxNodesPerBus+1)*(nodeAddress & 63)+(fLocalNodeID & 63)];
}

// FWSpeed
//
//

IOFWSpeed IOFireWireController::FWSpeed(UInt16 nodeA, UInt16 nodeB) const
{
	return (IOFWSpeed)fSpeedCodes[(kFWMaxNodesPerBus+1)*(nodeA & 63)+(nodeB & 63)];
}

// setNodeSpeed
//
//

void IOFireWireController::setNodeSpeed( UInt16 nodeAddress, IOFWSpeed speed )
{
	fSpeedCodes[(kFWMaxNodesPerBus+1)*(nodeAddress & 63)+(fLocalNodeID & 63)] = speed;
}

// maxPackLog
//
// How big (as a power of two) can packets sent to/received from the node be?

int IOFireWireController::maxPackLog(bool forSend, UInt16 nodeAddress) const
{
    int log;
	
    log = 9+FWSpeed(nodeAddress);
    if( forSend ) 
	{
        if( log > fMaxSendLog )
		{
            log = fMaxSendLog;
		}
	}
    else 
	{
		if( log > fMaxSendLog )
		{
			log = fMaxRecvLog;
		}
	}
	
	if( fUseHalfSizePackets )
	{
		if( log > 1 )
		{
			log--;
		}
	}
	
	return log;
}

// maxPackLog
//
// How big (as a power of two) can packets sent from A to B be?

int IOFireWireController::maxPackLog(UInt16 nodeA, UInt16 nodeB) const
{
    return 9+FWSpeed(nodeA, nodeB);
}

// nodeIDtoDevice
//
//

IOFireWireDevice * IOFireWireController::nodeIDtoDevice(UInt32 generation, UInt16 nodeID)
{
    OSIterator *childIterator;
    IOFireWireDevice * found = NULL;

    if(!checkGeneration(generation))
    {
	    return NULL;
    }

    childIterator = getClientIterator();

    if( childIterator) 
	{
        OSObject *child;
        while( (child = childIterator->getNextObject())) 
		{
            found = OSDynamicCast(IOFireWireDevice, child);
            if( found && 
				!found->isTerminated() && 
				found->fNodeID == nodeID )
			{
				break;
			}
		}
        childIterator->release();
    }
    return found;
}

// getGeneration
//
//

UInt32 IOFireWireController::getGeneration() const 
{
	return fBusGeneration;
}

// checkGeneration
//
//

bool IOFireWireController::checkGeneration(UInt32 gen) const 
{
	return gen == fBusGeneration;
}


// getLocalNodeID
//
//

UInt16 IOFireWireController::getLocalNodeID() const 
{
	return fLocalNodeID;
}

// getIRMNodeID
//
//

IOReturn IOFireWireController::getIRMNodeID(UInt32 &generation, UInt16 &id) const
{
	generation = fBusGeneration; 
	id = fIRMNodeID; 
	return kIOReturnSuccess;
}

// clipMaxRec2K
//
//

IOReturn IOFireWireController::clipMaxRec2K(Boolean clipMaxRec )
{
    IOReturn res;

	closeGate();
    
	//IOLog("IOFireWireController::clipMaxRec2K\n");

	res = fFWIM->clipMaxRec2K(clipMaxRec);
    
	openGate();
	
	return res;
}

// makeRoot
//
//

IOReturn IOFireWireController::makeRoot(UInt32 generation, UInt16 nodeID)
{
    IOReturn res = kIOReturnSuccess;
    nodeID &= 63;
    
	closeGate();
    
	if(!checkGeneration(generation))
        res = kIOFireWireBusReset;
    else if( fRootNodeID != nodeID ) {
        // Send phy packet to set root hold off bit for node
        res = fFWIM->sendPHYPacket((kFWConfigurationPacketID << kFWPhyPacketIDPhase) |
                    (nodeID << kFWPhyPacketPhyIDPhase) | kFWPhyConfigurationR);
        if(kIOReturnSuccess == res)
		{
	//		IOLog( "IOFireWireController::makeRoot resetBus\n" );
            res = resetBus();
		}
	}
    
    openGate();

    return res;
}

// useHalfSizePackets
//
//

void IOFireWireController::useHalfSizePackets( void )
{
	fUseHalfSizePackets = true;
	fRequestedHalfSizePackets = true;
}
 
#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// workloop lock
//

// openGate
//
//

void IOFireWireController::openGate()		
{
	fPendingQ.fSource->openGate();
}

// closeGate
//
//

void IOFireWireController::closeGate()		
{
	fPendingQ.fSource->closeGate();
}

// inGate
//
//

bool IOFireWireController::inGate()		
{
	return fPendingQ.fSource->inGate();
}

#pragma mark -
/////////////////////////////////////////////////////////////////////////////
// queues
//

// getTimeoutQ
//
//

IOFWCmdQ& IOFireWireController::getTimeoutQ() 
{ 
	return fTimeoutQ; 
}

// getPendingQ
//
//

IOFWCmdQ& IOFireWireController::getPendingQ() 
{ 
	return fPendingQ; 
}

// getAfterResetHandledQ
//
//

IOFWCmdQ &IOFireWireController::getAfterResetHandledQ() 
{ 
	return fAfterResetHandledQ;
}


