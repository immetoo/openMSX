// $Id$

#ifndef __KEYEVENTINSERTER_HH__
#define __KEYEVENTINSERTER_HH__

#include <string>
#include <SDL/SDL.h>
#include "CommandLineParser.hh"

namespace openmsx {

class EmuTime;


class KeyEventInserterCLI : public CLIOption
{
	public:
		KeyEventInserterCLI();
		virtual bool parseOption(const string &option,
		                         list<string> &cmdLine);
		virtual const string& optionHelp() const;
};


class KeyEventInserter
{
	public:
		KeyEventInserter(const EmuTime &time);
		void enter(const string &str, const EmuTime &time);

	private:
		static const SDLKey keymap[256][4];
};

} // namespace openmsx

#endif
