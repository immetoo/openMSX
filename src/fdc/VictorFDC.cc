#include "VictorFDC.hh"
#include "CacheLine.hh"
#include "DriveMultiplexer.hh"
#include "WD2793.hh"
#include "serialize.hh"

// This implementation is documented in the HC-95 service manual:
//
// FDD interface:
// 7FF8   I/O            FDC STATUS/COMMAND
// 7FF9   I/O            FDC TRACK REGISTER
// 7FFA   I/O            FDC SECTOR REGISTER
// 7FFB   I/O            FDC DATA REGISTER
// 7FFC   I/O   bit 0    A DRIVE MOTOR ON/OFF        "1" ON
//        I/O   bit 1    B DRIVE MOTOR ON/OFF        "1" ON
//        I/O   bit 2    DRIVE SELECT "0" A DRIVE    "1" B DRIVE
//        O     bit 3    SIDE SELECT "0" SIDE 0      "1" SIDE 1
//        I              SIDE SELECT "1" SIDE 0      "0" SIDE 1
//        I/O   bit 4    DRIVE ENABLE                "0" ENABLE
//              bit 5    unused
//        I     bit 6    FDC DATA REQUEST            "1" REQUEST
//        I     bit 7    FDC INTERRUPT REQUEST       "1" REQUEST

namespace openmsx {

static const int DRIVE_A_MOTOR = 0x01;
static const int DRIVE_B_MOTOR = 0x02;
static const int DRIVE_SELECT  = 0x04;
static const int SIDE_SELECT   = 0x08;
static const int DRIVE_DISABLE = 0x10; // renamed due to inverse logic
static const int DATA_REQUEST  = 0x40;
static const int INTR_REQUEST  = 0x80;


VictorFDC::VictorFDC(const DeviceConfig& config)
	: WD2793BasedFDC(config)
{
	reset(getCurrentTime());
}

void VictorFDC::reset(EmuTime::param time)
{
	WD2793BasedFDC::reset(time);
	// initialize in such way that drives are disabled
	// (and motors off, etc.)
	// TODO: test on real machine (this is an assumption)
	writeMem(0x7FFC, DRIVE_DISABLE, time);
}

byte VictorFDC::readMem(word address, EmuTime::param time)
{
	byte value;
	switch (address) {
	case 0x7FF8:
		value = controller->getStatusReg(time);
		break;
	case 0x7FF9:
		value = controller->getTrackReg(time);
		break;
	case 0x7FFA:
		value = controller->getSectorReg(time);
		break;
	case 0x7FFB:
		value = controller->getDataReg(time);
		break;
	case 0x7FFC:
		value = driveControls;
		if (controller->getIRQ(time))  value |= INTR_REQUEST;
		if (controller->getDTRQ(time)) value |= DATA_REQUEST;
		value ^= SIDE_SELECT; // inverted
		break;
	default:
		value = VictorFDC::peekMem(address, time);
		break;
	}
	return value;
}

byte VictorFDC::peekMem(word address, EmuTime::param time) const
{
	byte value;
	switch (address) {
	case 0x7FF8:
		value = controller->peekStatusReg(time);
		break;
	case 0x7FF9:
		value = controller->peekTrackReg(time);
		break;
	case 0x7FFA:
		value = controller->peekSectorReg(time);
		break;
	case 0x7FFB:
		value = controller->peekDataReg(time);
		break;
	case 0x7FFC:
		value = driveControls;
		if (controller->peekIRQ(time))  value |= INTR_REQUEST;
		if (controller->peekDTRQ(time)) value |= DATA_REQUEST;
		value ^= SIDE_SELECT; // inverted
		break;
	default:
		if ((0x4000 <= address) && (address < 0x8000)) {
			// ROM only visible in 0x4000-0x7FFF
			value = MSXFDC::peekMem(address, time);
		} else {
			value = 255;
		}
		break;
	}
	return value;
}

const byte* VictorFDC::getReadCacheLine(word start) const
{
	if ((start & CacheLine::HIGH) == (0x7FF8 & CacheLine::HIGH)) {
		// FDC at 0x7FF8-0x7FFC
		return nullptr;
	} else if ((0x4000 <= start) && (start < 0x8000)) {
		// ROM at 0x4000-0x7FFF
		return MSXFDC::getReadCacheLine(start);
	} else {
		return unmappedRead;
	}
}

void VictorFDC::writeMem(word address, byte value, EmuTime::param time)
{
	switch (address) {
	case 0x7FF8:
		controller->setCommandReg(value, time);
		break;
	case 0x7FF9:
		controller->setTrackReg(value, time);
		break;
	case 0x7FFA:
		controller->setSectorReg(value, time);
		break;
	case 0x7FFB:
		controller->setDataReg(value, time);
		break;
	case 0x7FFC:
		DriveMultiplexer::DriveNum drive;
		if ((value & DRIVE_DISABLE) != 0) {
			drive = DriveMultiplexer::NO_DRIVE;
		} else {
			drive = ((value & DRIVE_SELECT) != 0) ? DriveMultiplexer::DRIVE_B : DriveMultiplexer::DRIVE_A;
		}
		multiplexer->selectDrive(drive, time);
		multiplexer->setSide((value & SIDE_SELECT) != 0);
		multiplexer->setMotor((drive == DriveMultiplexer::DRIVE_A) ? ((value & DRIVE_A_MOTOR) != 0) : ((value & DRIVE_B_MOTOR) != 0), time); // this is not 100% correct: the motors can be controlled independently via bit 0 and 1
		// back up for reading:
		driveControls = value & (DRIVE_A_MOTOR | DRIVE_B_MOTOR | DRIVE_SELECT | SIDE_SELECT | DRIVE_DISABLE);
		break;
	}
}

byte* VictorFDC::getWriteCacheLine(word address) const
{
	if ((address & CacheLine::HIGH) == (0x7FF8 & CacheLine::HIGH)) {
		// FDC at 0x7FF8-0x7FFC
		return nullptr;
	} else {
		return unmappedWrite;
	}
}


template<typename Archive>
void VictorFDC::serialize(Archive& ar, unsigned /*version*/)
{
	ar.template serializeBase<WD2793BasedFDC>(*this);
	ar.serialize("driveControls", driveControls);
}
INSTANTIATE_SERIALIZE_METHODS(VictorFDC);
REGISTER_MSXDEVICE(VictorFDC, "VictorFDC");

} // namespace openmsx
