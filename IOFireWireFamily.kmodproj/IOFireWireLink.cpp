/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 2000 Apple Computer, Inc.  All rights reserved.
 *
 * HISTORY
 * 09 Nov 2000 wgulland created.
 *
 */

#include <IOKit/firewire/IOFireWireLink.h>
#include <IOKit/firewire/IOFireWireDevice.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

OSDefineMetaClass( IOFireWireLink, IOService )
OSDefineAbstractStructors(IOFireWireLink, IOService)

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

IOFireWireController * IOFireWireLink::createController()
{
    IOFireWireController *control;

    control = new IOFireWireController;
    if(NULL == control)
        return NULL;

    if(!control->init(this)) {
        control->release();
        control = NULL;
    }
    return control;
}

IOFWWorkLoop * IOFireWireLink::createWorkLoop()
{
    return IOFWWorkLoop::workLoop();
}

IOFireWireDevice *
IOFireWireLink::createDeviceNub(CSRNodeUniqueID guid, const IOFWNodeScan *scan)
{
    IOFireWireDevice *newDevice;
    OSDictionary *propTable;

    newDevice = new IOFireWireDevice;

    if (!newDevice)
        return NULL;

    do {
        OSObject * prop;
        propTable = OSDictionary::withCapacity(6);
        if (!propTable)
            continue;

        prop = OSNumber::withNumber(guid, 64);
        if(prop) {
            propTable->setObject(gFireWire_GUID, prop);
            prop->release();
        }
        prop = OSNumber::withNumber((scan->fSelfIDs[0] & kFWSelfID0SP) >> kFWSelfID0SPPhase, 32);
        if(prop) {
            propTable->setObject(gFireWireSpeed, prop);
            prop->release();
        }

        if(!newDevice->init(propTable, scan)) {
            newDevice->release();
            newDevice = NULL;
        }
        
//        IOLog("Guid is 0x%x:0x%x\n", scan->fBuf[3], scan->fBuf[4]);
        // QPS DVDRam, CD R/W
        if(scan->fBuf[3] == 0x0080cf02 || scan->fBuf[3] == 0x00101002) {
            newDevice->setMaxPackLog(false, true, 2);
        }
    } while (false);
    if(propTable)
        propTable->release();	// done with it after init

    return newDevice;
}

IOFireWireController * IOFireWireLink::getController() const
{
    return fControl;
}

IOWorkLoop * IOFireWireLink::getWorkLoop() const
{
    return fWorkLoop;
}

IOFWWorkLoop * IOFireWireLink::getFireWireWorkLoop() const
{
    return fWorkLoop;
}


