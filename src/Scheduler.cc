// $Id$

#include <cassert>
#include <algorithm>
#include "Scheduler.hh"
#include "MSXCPU.hh"
#include "EventDistributor.hh"
#include "InputEventGenerator.hh"
#include "Schedulable.hh"
#include "CommandController.hh"
#include "Leds.hh"
#include "MSXMotherBoard.hh"
#include "Timer.hh"
#include "Display.hh"

using std::swap;
using std::make_heap;
using std::push_heap;
using std::pop_heap;


namespace openmsx {

const EmuTime Scheduler::ASAP;

Scheduler::Scheduler()
	: sem(1), paused(false), powered(false), needReset(false),
	  motherboard(NULL), eventGenerator(NULL),
	  pauseSetting("pause", "pauses the emulation", paused),
	  powerSetting("power", "turn power on/off", powered),
	  leds(Leds::instance()),
	  cpu(MSXCPU::instance()),
	  commandController(CommandController::instance()),
	  quitCommand(*this),
	  resetCommand(*this)
{
	pauseCounter = 1; // power off
	emulationRunning = true;
	cpu.init(this);

	pauseSetting.addListener(this);
	powerSetting.addListener(this);

	commandController.registerCommand(&quitCommand, "quit");
	commandController.registerCommand(&quitCommand, "exit");
	commandController.registerCommand(&resetCommand, "reset");
}

Scheduler::~Scheduler()
{
	commandController.unregisterCommand(&resetCommand, "reset");
	commandController.unregisterCommand(&quitCommand, "exit");
	commandController.unregisterCommand(&quitCommand, "quit");

	powerSetting.removeListener(this);
	pauseSetting.removeListener(this);
}

Scheduler& Scheduler::instance()
{
	static Scheduler oneInstance;
	return oneInstance;
}

void Scheduler::setMotherBoard(MSXMotherBoard* motherboard_)
{
	motherboard = motherboard_;
}

void Scheduler::setEventDistributor(EventDistributor* eventDistributor)
{
	eventDistributor->registerEventListener(QUIT_EVENT, *this,
	                                       EventDistributor::NATIVE);
}

void Scheduler::unsetEventDistributor(EventDistributor* eventDistributor)
{
	eventDistributor->unregisterEventListener(QUIT_EVENT, *this,
	                                         EventDistributor::NATIVE);
}

void Scheduler::setSyncPoint(const EmuTime& timeStamp, Schedulable* device, int userData)
{
	assert(device);
	//PRT_DEBUG("Sched: registering " << device->schedName() <<
	//          " " << userData << " for emulation at " << timeStamp);

	sem.down();
	EmuTime time = timeStamp == ASAP ? scheduleTime : timeStamp;
	assert(scheduleTime <= time);

	// Push sync point into queue.
	syncPoints.push_back(SynchronizationPoint(timeStamp, device, userData));
	push_heap(syncPoints.begin(), syncPoints.end());
	// Tell CPU emulation to return early if necessary.
	// TODO: Emulation may run in parallel in a seperate thread.
	//       What we're doing here is not thread safe.
	if (time < cpu.getTargetTime()) {
		cpu.setTargetTime(time);
	}
	sem.up();

	if ((pauseCounter > 0) && eventGenerator) {
		eventGenerator->notify();
	}
}

void Scheduler::setAsyncPoint(Schedulable* device, int userData)
{
	assert(device);
	//PRT_DEBUG("Sched: registering " << device->schedName() <<
	//          " " << userData << " async for emulation ASAP");

	sem.down();

	// Push sync point into queue.
	syncPoints.push_back(SynchronizationPoint(ASAP, device, userData));
	push_heap(syncPoints.begin(), syncPoints.end());

	sem.up();

	if ((pauseCounter > 0) && eventGenerator) {
		eventGenerator->notify();
	}
}

void Scheduler::removeSyncPoint(Schedulable* device, int userData)
{
	sem.down();
	for (vector<SynchronizationPoint>::iterator it = syncPoints.begin();
	     it != syncPoints.end(); ++it) {
		SynchronizationPoint& sp = *it;
		if ((sp.getDevice() == device) &&
		    (sp.getUserData() == userData)) {
			swap(sp, syncPoints.back());
			syncPoints.pop_back();
			make_heap(syncPoints.begin(), syncPoints.end());
			break;
		}
	}
	sem.up();
}

const EmuTime& Scheduler::getCurrentTime() const
{
	return scheduleTime;
}

void Scheduler::setCurrentTime(const EmuTime& time)
{
	scheduleTime = time;
}

void Scheduler::stopScheduling()
{
	PRT_DEBUG("schedule stop @ " << scheduleTime);
	setSyncPoint(ASAP, this);
}

void Scheduler::schedule()
{
	eventGenerator = &InputEventGenerator::instance();
	while (emulationRunning) {
		// Get next sync point.
		sem.down();
		const SynchronizationPoint sp =
			  syncPoints.empty()
			? SynchronizationPoint(EmuTime::infinity, NULL, 0)
			: syncPoints.front();
		const EmuTime& time = sp.getTime();

		if (time == ASAP) {
			scheduleDevice(sp, scheduleTime);
			eventGenerator->poll();
		} else if (pauseCounter > 0) {
			sem.up();
			Display::INSTANCE->repaint();
			Timer::sleep(100 * 1000);
			eventGenerator->poll();
		} else {
			if (cpu.getTargetTime() < time) {
				sem.up();
				// Schedule CPU until first sync point.
				// This may set earlier sync point.
				PRT_DEBUG ("Sched: Scheduling CPU till " << time);
				if (needReset) {
					needReset = false;
					motherboard->resetMSX();
				}
				cpu.executeUntilTarget(time);
			} else {
				scheduleDevice(sp, time);
			}
			eventGenerator->poll();
		}
	}
}

void Scheduler::scheduleFromCPU(const EmuTime& limit)
{
	while (true) {
		// Get next sync point.
		sem.down();
		if (syncPoints.empty()) {
			sem.up();
			break;
		}
		const SynchronizationPoint sp = syncPoints.front();
		const EmuTime& time = sp.getTime();

		// Return when we've gone far enough.
		if (limit < time) {
			sem.up();
			break;
		}
		scheduleDevice(sp, (time == ASAP) ? scheduleTime : time);
	}
	scheduleTime = limit;
}

void Scheduler::scheduleDevice(const SynchronizationPoint &sp,
                               const EmuTime& time)
{
	assert(scheduleTime <= time);
	scheduleTime = time;
	pop_heap(syncPoints.begin(), syncPoints.end());
	syncPoints.pop_back();
	sem.up();
	Schedulable* device = sp.getDevice();
	assert(device);
	int userData = sp.getUserData();
	PRT_DEBUG ("Sched: Scheduling " << device->schedName()
			<< " " << userData << " till " << time);
	device->executeUntil(time, userData);
}

void Scheduler::unpause()
{
	if (paused) {
		paused = false;
		leds.setLed(Leds::PAUSE_OFF);
		--pauseCounter;
	}
}

void Scheduler::pause()
{
	if (!paused) {
		paused = true;
		leds.setLed(Leds::PAUSE_ON);
		++pauseCounter;
	}
}

void Scheduler::powerOn()
{
	if (!powered) {
		powered = true;
		powerSetting.setValue(true);
		leds.setLed(Leds::POWER_ON);
		--pauseCounter;
	}
}

void Scheduler::powerOff()
{
	if (powered) {
		powered = false;
		powerSetting.setValue(false);
		leds.setLed(Leds::POWER_OFF);
		++pauseCounter;
	}
}

// SettingListener
void Scheduler::update(const SettingLeafNode* setting)
{
	if (setting == &pauseSetting) {
		if (pauseSetting.getValue()) {
			pause();
		} else {
			unpause();
		}
	} else if (setting == &powerSetting) {
		if (powerSetting.getValue()) {
			powerOn();
		} else {
			powerOff();
		}
	} else {
		assert(false);
	}
}

// EventListener
bool Scheduler::signalEvent(const Event& /*event*/)
{
	stopScheduling();
	return true;
}

// Schedulable
void Scheduler::executeUntil(const EmuTime& /*time*/, int /*userData*/)
{
	emulationRunning = false;
}

const string& Scheduler::schedName() const
{
	static const string name("Scheduler");
	return name;
}


// class QuitCommand

Scheduler::QuitCommand::QuitCommand(Scheduler& parent_)
	: parent(parent_)
{
}

string Scheduler::QuitCommand::execute(const vector<string>& /*tokens*/)
{
	parent.stopScheduling();
	return "";
}

string Scheduler::QuitCommand::help(const vector<string>& /*tokens*/) const
{
	return "Use this command to stop the emulator\n";
}


// class ResetCmd

Scheduler::ResetCmd::ResetCmd(Scheduler& parent_)
	: parent(parent_)
{
}

string Scheduler::ResetCmd::execute(const vector<string>& /*tokens*/)
{
	parent.needReset = true;
	return "";
}
string Scheduler::ResetCmd::help(const vector<string>& /*tokens*/) const
{
	return "Resets the MSX.\n";
}

} // namespace openmsx

