/*
 *  IT87x.cpp
 *  HWSensors
 *
 *  Based on code from Open Hardware Monitor project by Michael Möller (C) 2011
 *
 *  Created by kozlek on 08/10/10.
 *  Copyright 2010 Natan Zalkin <natan.zalkin@me.com>. All rights reserved.
 *
 */

/*
 
 Version: MPL 1.1/GPL 2.0/LGPL 2.1
 
 The contents of this file are subject to the Mozilla Public License Version
 1.1 (the "License"); you may not use this file except in compliance with
 the License. You may obtain a copy of the License at
 
 http://www.mozilla.org/MPL/
 
 Software distributed under the License is distributed on an "AS IS" basis,
 WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 for the specific language governing rights and limitations under the License.
 
 The Original Code is the Open Hardware Monitor code.
 
 The Initial Developer of the Original Code is 
 Michael Möller <m.moeller@gmx.ch>.
 Portions created by the Initial Developer are Copyright (C) 2011
 the Initial Developer. All Rights Reserved.
 
 Contributor(s):
 
 Alternatively, the contents of this file may be used under the terms of
 either the GNU General Public License Version 2 or later (the "GPL"), or
 the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 in which case the provisions of the GPL or the LGPL are applicable instead
 of those above. If you wish to allow use of your version of this file only
 under the terms of either the GPL or the LGPL, and not to allow others to
 use your version of this file under the terms of the MPL, indicate your
 decision by deleting the provisions above and replace them with the notice
 and other provisions required by the GPL or the LGPL. If you do not delete
 the provisions above, a recipient may use your version of this file under
 the terms of any one of the MPL, the GPL or the LGPL.
 
 */

#include "IT87xxSensors.h"
#include "FakeSMCDefinitions.h"
#include "SuperIO.h"
#include "FakeSMCKey.h"

#define FEATURE_12MV_ADC		(1 << 0)
#define FEATURE_NEWER_AUTOPWM	(1 << 1)
#define FEATURE_OLD_AUTOPWM     (1 << 2)
#define FEATURE_16BIT_FANS		(1 << 3)
#define FEATURE_TEMP_OFFSET     (1 << 4)
#define FEATURE_TEMP_PECI		(1 << 5)
#define FEATURE_TEMP_OLD_PECI	(1 << 6)

#define super LPCSensors
OSDefineMetaClassAndStructors(IT87xxSensors, LPCSensors)

UInt8 IT87xxSensors::readByte(UInt8 reg)
{
	outb(address + ITE_ADDRESS_REGISTER_OFFSET, reg);
	return inb(address + ITE_DATA_REGISTER_OFFSET);
}

void IT87xxSensors::writeByte(UInt8 reg, UInt8 value)
{
	outb(address + ITE_ADDRESS_REGISTER_OFFSET, reg);
	outb(address + ITE_DATA_REGISTER_OFFSET, value);
}

UInt8 IT87xxSensors::temperatureSensorsLimit()
{
    return 3;
}

UInt8 IT87xxSensors::voltageSensorsLimit()
{
    return 9;
}

UInt8 IT87xxSensors::tachometerSensorsLimit()
{
    switch (model) {
        case IT8705F:
            return 3;
            
        default:
            return 5;
    }
}

float IT87xxSensors::readTemperature(UInt32 index)
{
	return readByte(ITE_TEMPERATURE_BASE_REG + index);
}

float IT87xxSensors::readVoltage(UInt32 index)
{
    UInt8 v = readByte(ITE_VOLTAGE_BASE_REG + index);
    return (float)v * (features & FEATURE_12MV_ADC ? 0.012f : 0.016f);
}

float IT87xxSensors::readTachometer(UInt32 index)
{
    UInt32 value;
    
    if (features & FEATURE_16BIT_FANS)
    {
        value = readByte(ITE_FAN_TACHOMETER_REG[index]);
        
        value |= readByte(ITE_FAN_TACHOMETER_EXT_REG[index]) << 8;
        
        return value > 0x3f && value < 0xffff ? (float)(1.35e6f + value) / (float)(value * 2) : 0;
    }
    else
    {
        value = readByte(ITE_FAN_TACHOMETER_REG[index]);
        
        int divisor = 2;
        
        if (index < 2) 
            divisor = 1 << ((readByte(ITE_FAN_TACHOMETER_DIVISOR_REGISTER) >> (3 * index)) & 0x7);
        
        return value > 0 && value < 0xff ? 1.35e6f / (float)(value * divisor) : 0; 
    }
}

UInt8 IT87xxSensors::readTachometerControl(UInt32 index)
{
    if (index >= 5)
        return 0;
    
    UInt8 control;
    
    if (features & FEATURE_NEWER_AUTOPWM) {
        control =  readByte(ITE_SMARTGUARDIAN_PWM_DUTY(index));
    }
    else {
        control = (readByte(ITE_SMARTGUARDIAN_PWM(index)) & 0x7f) << 1;
        control = (control & 0x7f) << 1;
    }
    
    return (float)(control) / 2.55f;
}

void IT87xxSensors::writeTachometerControl(UInt32 index, UInt8 percent)
{
    if (index < 5) {
        
        /* Read PWM controller */
        UInt8 pwmControl = readByte(ITE_SMARTGUARDIAN_PWM_CONTROL[index]);
        
        UInt8 pwmTempMap, pwmDuty;
        
        if (features & FEATURE_NEWER_AUTOPWM) {
            pwmTempMap = pwmControl & 0x03;
            pwmDuty = readByte(ITE_SMARTGUARDIAN_PWM_DUTY(index));
        } else {
            if (pwmControl & 0x80)	/* Automatic mode */
                pwmTempMap = pwmControl & 0x03;
            else				/* Manual mode */
                pwmDuty = pwmControl & 0x7f;
        }

        /* Manual mode */
        pwmControl = features & FEATURE_NEWER_AUTOPWM ? pwmTempMap : pwmDuty;
		writeByte(ITE_SMARTGUARDIAN_PWM(index), pwmControl);
        
        /* set SmartGuardian mode */        
        writeByte(ITE_SMARTGUARDIAN_MAIN_CONTROL, readByte(ITE_SMARTGUARDIAN_MAIN_CONTROL) | (1 << index));
        
        UInt8 control = (float)(percent) * 2.55;
        
        if (features & FEATURE_NEWER_AUTOPWM) {
            writeByte(ITE_SMARTGUARDIAN_PWM_DUTY(index), control);
        } else {
            /*
             * If we are in manual mode, write the duty cycle immediately;
             * otherwise, just store it for later use.
             */
            if (!(pwmControl & 0x80)) {
                writeByte(ITE_SMARTGUARDIAN_PWM(index), control >> 1);
            }
        }
    }
}

bool IT87xxSensors::initialize()
{
    UInt8 vendor = readByte(ITE_VENDOR_ID_REGISTER);
    
	if (vendor != ITE_VENDOR_ID) {
        HWSensorsFatalLog("invalid vendor ID=0x%x", vendor);
        model = 0;
 		return false;
    }
	
	if ((readByte(ITE_CONFIGURATION_REGISTER) & 0x10) == 0) {
        HWSensorsFatalLog("invalid configuration register value");
        model = 0;
 		return false;
    }
    
    switch (model) {
        case IT8512F:
            features = FEATURE_OLD_AUTOPWM;
            break;
        case IT8705F:
            features = FEATURE_OLD_AUTOPWM;
            break;
        case IT8712F:
            features = FEATURE_OLD_AUTOPWM;
            break;
        case IT8716F:
            features = FEATURE_16BIT_FANS | FEATURE_TEMP_OFFSET;
            break;
        case IT8718F:
            features = FEATURE_16BIT_FANS | FEATURE_TEMP_OFFSET | FEATURE_TEMP_OLD_PECI;
            break;
        case IT8720F:
            features = FEATURE_16BIT_FANS | FEATURE_TEMP_OFFSET | FEATURE_TEMP_OLD_PECI;
            break;
        case IT8721F:
           features = FEATURE_NEWER_AUTOPWM | FEATURE_12MV_ADC | FEATURE_16BIT_FANS
            | FEATURE_TEMP_OFFSET | FEATURE_TEMP_OLD_PECI | FEATURE_TEMP_PECI;
            break;
        case IT8726F:
        case IT8728F:
        case IT8752F:
        case IT8771E:
        case IT8772E:
            features = FEATURE_NEWER_AUTOPWM | FEATURE_12MV_ADC | FEATURE_16BIT_FANS
            | FEATURE_TEMP_OFFSET | FEATURE_TEMP_PECI;
            break;
            
            //case IT8782E:
            //features = FEATURE_16BIT_FANS | FEATURE_TEMP_OFFSET | FEATURE_TEMP_OLD_PECI;
            
        default:
            break;
    }
	
//    switch (model) {
//        case IT8705F:
//        case IT8721F:
//        case IT8728F:
//        case IT8771E:
//        case IT8772E:
//            voltageGain = 0.012f;
//            break;
//            
//        default:
//            voltageGain = 0.016f;
//            break;
//    }
    
//    UInt8 version = readByte(ITE_VERSION_REGISTER) & 0x0F;
//    
//    has16bitFanCounter = !((model == IT8705F && version < 3) || (model == IT8712F && version < 8));
    
    return true;
}
