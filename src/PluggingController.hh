// $Id$

#ifndef __PLUGGING_CONTROLLER__
#define __PLUGGING_CONTROLLER__

#include <vector>
#include "Command.hh"
#include "InfoTopic.hh"

using std::vector;

namespace openmsx {

class Connector;
class Pluggable;
class Scheduler;
class CommandController;
class InfoCommand;

/**
 * Central administration of Connectors and Pluggables.
 */
class PluggingController
{
public:
	static PluggingController& instance();

	/**
	 * Connectors can be (un)registered
	 * Note: it is not an error when you try to unregister a Connector
	 *       that was not registered before, in this case nothing happens
	 *
	 */
	void registerConnector(Connector *connector);
	void unregisterConnector(Connector *connector);

	/**
	 * Add a Pluggable to the registry.
	 * PluggingController has ownership of all registered Pluggables.
	 */
	void registerPluggable(Pluggable *pluggable);

	/**
	 * Removes a Pluggable from the registry.
	 * If you attempt to unregister a Pluggable that is not in the registry,
	 * nothing happens.
	 */
	void unregisterPluggable(Pluggable *pluggable);

private:
	PluggingController();
	~PluggingController();

	Connector *getConnector(const string &name);
	Pluggable *getPluggable(const string &name);

	vector<Connector *> connectors;
	vector<Pluggable *> pluggables;

	// Commands
	class PlugCmd : public SimpleCommand {
	public:
		PlugCmd(PluggingController& parent);
		virtual string execute(const vector<string> &tokens)
			throw(CommandException);
		virtual string help   (const vector<string> &tokens) const
			throw();
		virtual void tabCompletion(vector<string> &tokens) const
			throw();
	private:
		PluggingController& parent;
	} plugCmd;
	friend class PlugCmd;

	class UnplugCmd : public SimpleCommand {
	public:
		UnplugCmd(PluggingController& parent);
		virtual string execute(const vector<string> &tokens)
			throw(CommandException);
		virtual string help   (const vector<string> &tokens) const
			throw();
		virtual void tabCompletion(vector<string> &tokens) const
			throw();
	private:
		PluggingController& parent;
	} unplugCmd;
	friend class UnplugCmd;

	class PluggableInfo : public InfoTopic {
	public:
		PluggableInfo(PluggingController& parent);
		virtual string execute(const vector<string> &tokens) const
			throw(CommandException);
		virtual string help   (const vector<string> &tokens) const
			throw();
		virtual void tabCompletion(vector<string> &tokens) const
			throw();
	private:
		PluggingController& parent;
	} pluggableInfo;
	friend class PluggableInfo;

	class ConnectorInfo : public InfoTopic {
	public:
		ConnectorInfo(PluggingController& parent);
		virtual string execute(const vector<string> &tokens) const
			throw(CommandException);
		virtual string help   (const vector<string> &tokens) const
			throw();
		virtual void tabCompletion(vector<string> &tokens) const
			throw();
	private:
		PluggingController& parent;
	} connectorInfo;
	friend class ConnectorInfo;

	Scheduler& scheduler;
	CommandController& commandController;
	InfoCommand& infoCommand;
};

} // namespace openmsx

#endif //__PLUGGING_CONTROLLER__
