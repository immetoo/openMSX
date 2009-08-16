// $Id$

#ifndef DISKCHANGER_HH
#define DISKCHANGER_HH

#include "DiskContainer.hh"
#include "MSXEventListener.hh"
#include "serialize_meta.hh"
#include "noncopyable.hh"
#include <vector>
#include <string>
#include <memory>

namespace openmsx {

class CommandController;
class MSXEventDistributor;
class Scheduler;
class MSXMotherBoard;
class DiskFactory;
class DiskManipulator;
class Disk;
class DiskCommand;
class TclObject;
class DiskName;

class DiskChanger : public DiskContainer, private MSXEventListener,
                    private noncopyable
{
public:
	DiskChanger(const std::string& driveName,
	            CommandController& commandController,
	            DiskFactory& diskFactory,
	            DiskManipulator& manipulator,
	            MSXMotherBoard& board,
	            bool createCommand);
	DiskChanger(const std::string& driveName,
	            CommandController& commandController,
	            DiskFactory& diskFactory,
	            DiskManipulator& manipulator,
	            bool createCommand);
	DiskChanger(MSXMotherBoard& board,
	            const std::string& driveName);
	~DiskChanger();

	void createCommand();

	const std::string& getDriveName() const;
	const DiskName& getDiskName() const;
	bool peekDiskChanged() const;
	Disk& getDisk();

	// DiskContainer
	virtual SectorAccessibleDisk* getSectorAccessibleDisk();
	virtual const std::string& getContainerName() const;
	virtual bool diskChanged();
	virtual int insertDisk(const std::string& filename);

	// for NowindCommand
	void changeDisk(std::auto_ptr<Disk> newDisk);

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	void init(const std::string& prefix, bool createCmd);
	void insertDisk(const std::vector<TclObject*>& args);
	void ejectDisk();
	void sendChangeDiskEvent(const std::vector<std::string>& args);

	// MSXEventListener
	virtual void signalEvent(shared_ptr<const Event> event,
	                         EmuTime::param time);

	CommandController& controller;
	MSXEventDistributor* msxEventDistributor;
	Scheduler* scheduler;
	DiskFactory& diskFactory;
	DiskManipulator& manipulator;

	const std::string driveName;
	std::auto_ptr<Disk> disk;

	friend class DiskCommand;
	std::auto_ptr<DiskCommand> diskCommand; // must come after driveName

	bool diskChangedFlag;
};
SERIALIZE_CLASS_VERSION(DiskChanger, 2);

} // namespace openmsx

#endif
