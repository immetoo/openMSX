#include "MidiSessionALSA.hh"
#include "CliComm.hh"
#include "MidiInConnector.hh"
#include "MidiInDevice.hh"
#include "MidiOutDevice.hh"
#include "PlugException.hh"
#include "PluggingController.hh"
#include "serialize.hh"
#include "EventDistributor.hh"
#include "EventListener.hh"
#include "circular_buffer.hh"
#include "Scheduler.hh"
#include <cstdio>
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>
#include <memory>


namespace openmsx {

// MidiOutALSA ==============================================================

class MidiOutALSA final : public MidiOutDevice {
public:
	MidiOutALSA(
			snd_seq_t& seq,
			snd_seq_client_info_t& cinfo, snd_seq_port_info_t& pinfo);
	~MidiOutALSA() override;
	MidiOutALSA(const MidiOutALSA&) = delete;
	MidiOutALSA& operator=(const MidiOutALSA&) = delete;

	// Pluggable
	void plugHelper(Connector& connector, EmuTime::param time) override;
	void unplugHelper(EmuTime::param time) override;
	[[nodiscard]] const std::string& getName() const override;
	[[nodiscard]] std::string_view getDescription() const override;

	// MidiOutDevice
	void recvMessage(
			const std::vector<uint8_t>& message, EmuTime::param time) override;

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	void connect();
	void disconnect();

	snd_seq_t& seq;
	snd_midi_event_t* event_parser;
	int sourcePort;
	int destClient;
	int destPort;
	std::string name;
	std::string desc;
	bool connected;
};

MidiOutALSA::MidiOutALSA(
		snd_seq_t& seq_,
		snd_seq_client_info_t& cinfo, snd_seq_port_info_t& pinfo)
	: seq(seq_)
	, sourcePort(-1)
	, destClient(snd_seq_port_info_get_client(&pinfo))
	, destPort(snd_seq_port_info_get_port(&pinfo))
	, name(snd_seq_client_info_get_name(&cinfo))
	, desc(snd_seq_port_info_get_name(&pinfo))
	, connected(false)
{
}

MidiOutALSA::~MidiOutALSA()
{
	if (connected) {
		disconnect();
	}
}

void MidiOutALSA::plugHelper(Connector& /*connector_*/, EmuTime::param /*time*/)
{
	connect();
}

void MidiOutALSA::unplugHelper(EmuTime::param /*time*/)
{
	disconnect();
}

void MidiOutALSA::connect()
{
	sourcePort = snd_seq_create_simple_port(
		&seq, "MIDI out pluggable",
		0, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	if (sourcePort < 0) {
		throw PlugException(
			"Failed to create ALSA port: ", snd_strerror(sourcePort));
	}

	int err = snd_seq_connect_to(&seq, sourcePort, destClient, destPort);
	if (err) {
		snd_seq_delete_simple_port(&seq, sourcePort);
		throw PlugException(
			"Failed to connect to ALSA port "
			"(", destClient, ':', destPort, ")"
			": ", snd_strerror(err));
	}

	snd_midi_event_new(MAX_MESSAGE_SIZE, &event_parser);

	connected = true;
}

void MidiOutALSA::disconnect()
{
	snd_midi_event_free(event_parser);
	snd_seq_disconnect_to(&seq, sourcePort, destClient, destPort);
	snd_seq_delete_simple_port(&seq, sourcePort);

	connected = false;
}

const std::string& MidiOutALSA::getName() const
{
	return name;
}

std::string_view MidiOutALSA::getDescription() const
{
	return desc;
}

void MidiOutALSA::recvMessage(
		const std::vector<uint8_t>& message, EmuTime::param /*time*/)
{
	snd_seq_event_t ev;
	snd_seq_ev_clear(&ev);

	// Set routing.
	snd_seq_ev_set_source(&ev, sourcePort);
	snd_seq_ev_set_subs(&ev);

	// Set message.
	long encodeLen = snd_midi_event_encode(
			event_parser, message.data(), message.size(), &ev);
	if (encodeLen < 0) {
		std::cerr << "Error encoding MIDI message of type "
		          << std::hex << int(message[0]) << std::dec
		          << ": " << snd_strerror(encodeLen) << '\n';
		return;
	}
	if (ev.type == SND_SEQ_EVENT_NONE) {
		std::cerr << "Incomplete MIDI message of type "
		          << std::hex << int(message[0]) << std::dec << '\n';
		return;
	}

	// Send event.
	snd_seq_ev_set_direct(&ev);
	int err = snd_seq_event_output(&seq, &ev);
	if (err < 0) {
		std::cerr << "Error sending MIDI event: "
		          << snd_strerror(err) << '\n';
	}
	snd_seq_drain_output(&seq);
}

template<typename Archive>
void MidiOutALSA::serialize(Archive& ar, unsigned /*version*/)
{
	if (ar.isLoader()) {
		connect();
	}
}
INSTANTIATE_SERIALIZE_METHODS(MidiOutALSA);
REGISTER_POLYMORPHIC_INITIALIZER(Pluggable, MidiOutALSA, "MidiOutALSA");


// MidiInALSA ==============================================================

class MidiInALSA final : public MidiInDevice, private EventListener {
public:
	MidiInALSA(
			EventDistributor& eventDistributor, Scheduler& scheduler,
			snd_seq_t& seq,
			snd_seq_client_info_t& cinfo, snd_seq_port_info_t& pinfo);
	~MidiInALSA() override;

	// Pluggable
	void plugHelper(Connector& connector, EmuTime::param time) override;
	void unplugHelper(EmuTime::param time) override;
	const std::string& getName() const override;
	string_view getDescription() const override;

	// MidiInDevice
	void signal(EmuTime::param time) override;

	template<typename Archive>
	void serialize(Archive& ar, unsigned version);

private:
	void run();
	void connect();
	void disconnect();

	// EventListener
	int signalEvent(const std::shared_ptr<const Event>& event) override;

	snd_seq_t& seq;
	EventDistributor& eventDistributor;
	Scheduler& scheduler;
	std::thread thread;
	cb_queue<byte> queue;
	std::mutex queueMutex;
	snd_midi_event_t* event_parser;
	snd_seq_addr_t sender, dest;
	snd_seq_port_subscribe_t *subs;
	int sourceClient;
	int sourcePort;
	int destClient;
	int destPort;
	std::string name;
	std::string desc;
	bool connected;
};

MidiInALSA::MidiInALSA(
		EventDistributor& eventDistributor_, Scheduler& scheduler_,
		snd_seq_t& seq_,
		snd_seq_client_info_t& cinfo, snd_seq_port_info_t& pinfo)
	: seq(seq_)
	, eventDistributor(eventDistributor_)
	, scheduler(scheduler_)
	, sourceClient(snd_seq_client_id(&seq))
	, sourcePort(-1)
	, destClient(snd_seq_port_info_get_client(&pinfo))
	, destPort(snd_seq_port_info_get_port(&pinfo))
	, name(snd_seq_client_info_get_name(&cinfo))
	, desc(snd_seq_port_info_get_name(&pinfo))
	, connected(false)
{
	eventDistributor.registerEventListener(OPENMSX_MIDI_IN_ALSA_EVENT, *this);
}

MidiInALSA::~MidiInALSA() {
	if (connected) {
		disconnect();
	}
	eventDistributor.unregisterEventListener(OPENMSX_MIDI_IN_ALSA_EVENT, *this);
}

void MidiInALSA::plugHelper(Connector& connector_, EmuTime::param /*time*/)
{
	connect();

	setConnector(&connector_); // base class will do this in a moment,
	                           // but thread already needs it
	thread = std::thread([this]() { run(); });
}

void MidiInALSA::unplugHelper(EmuTime::param /*time*/)
{
	disconnect();
	thread.join();
}

void MidiInALSA::connect()
{
	sourcePort = snd_seq_create_simple_port(
		&seq, "MIDI in pluggable",
		SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	if (sourcePort < 0) {
		throw PlugException(
                        "Failed to create ALSA port: ", snd_strerror(sourcePort));
	}
	sender.client = destClient;
	sender.port = destPort;
	dest.client = sourceClient;
	dest.port = sourcePort;
	snd_seq_port_subscribe_alloca(&subs);
	snd_seq_port_subscribe_set_sender(subs, &sender);
	snd_seq_port_subscribe_set_dest(subs, &dest);
	snd_seq_port_subscribe_set_queue(subs, 1);
	snd_seq_port_subscribe_set_time_update(subs, 1);
	snd_seq_port_subscribe_set_time_real(subs, 1);
	int err = snd_seq_subscribe_port(&seq, subs);

	if (err) {
		snd_seq_delete_simple_port(&seq, sourcePort);
		throw PlugException(
			"Failed to subscribe to ALSA port "
			"(", destClient, ':', destPort, ")"
			": ", snd_strerror(err));
	}

	snd_midi_event_new(MidiOutDevice::MAX_MESSAGE_SIZE, &event_parser);

	connected = true;
}

void MidiInALSA::disconnect()
{
	connected = false;

	snd_midi_event_free(event_parser);
	snd_seq_unsubscribe_port(&seq, subs);
	snd_seq_delete_simple_port(&seq, sourcePort);
}

const std::string& MidiInALSA::getName() const
{
	return name;
}

string_view MidiInALSA::getDescription() const
{
	return desc;
}

void MidiInALSA::run()
{
	unsigned char buf;
	snd_seq_event_t* ev;
	while (true) {
		if (!connected) {
			break;
		}
		int pending = snd_seq_event_input_pending(&seq, 0);
		if (pending == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		int res = snd_seq_event_input(&seq, &ev);
		if (res == -EAGAIN || res == -ENOSPC) {
			continue;
		}
		assert(isPluggedIn());

		long num = snd_midi_event_decode(event_parser, &buf, 12, ev);
		if (num < 0) {
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(queueMutex);
			queue.push_back(buf);
		}
		eventDistributor.distributeEvent(
			std::make_shared<SimpleEvent>(OPENMSX_MIDI_IN_ALSA_EVENT));
	}
}

// MidiInDevice
void MidiInALSA::signal(EmuTime::param time)
{
	auto* conn = static_cast<MidiInConnector*>(getConnector());
	if (!conn->acceptsData()) {
		std::lock_guard<std::mutex> lock(queueMutex);
		queue.clear();
		return;
	}
	if (!conn->ready()) return;

	byte data;
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		if (queue.empty()) return;
		data = queue.pop_front();
	}
	conn->recvByte(data, time);
}

// EventListener
int MidiInALSA::signalEvent(const std::shared_ptr<const Event>& /*event*/)
{
	if (isPluggedIn()) {
		signal(scheduler.getCurrentTime());
	} else {
		std::lock_guard<std::mutex> lock(queueMutex);
		queue.clear();
	}
	return 0;
}

template<typename Archive>
void MidiInALSA::serialize(Archive& ar, unsigned /*version*/)
{
	if (ar.isLoader()) {
		connect();
	}
}
INSTANTIATE_SERIALIZE_METHODS(MidiInALSA);
REGISTER_POLYMORPHIC_INITIALIZER(Pluggable, MidiInALSA, "MidiInALSA");


// MidiSessionALSA ==========================================================

std::unique_ptr<MidiSessionALSA> MidiSessionALSA::instance;

void MidiSessionALSA::registerAll(
		PluggingController& controller, CliComm& cliComm,
		EventDistributor& eventDistributor, Scheduler& scheduler)
{
	if (!instance) {
		// Open the sequencer.
		snd_seq_t* seq;
		int err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
		if (err < 0) {
			cliComm.printError(
				"Could not open sequencer: ", snd_strerror(err));
			return;
		}
		snd_seq_set_client_name(seq, "openMSX");
		instance.reset(new MidiSessionALSA(*seq));
	}
	instance->scanClients(controller, eventDistributor, scheduler);
}

MidiSessionALSA::MidiSessionALSA(snd_seq_t& seq_)
	: seq(seq_)
{
}

MidiSessionALSA::~MidiSessionALSA()
{
	// While the Pluggables still have a copy of this pointer, they won't
	// be accessing it anymore when openMSX is exiting.
	snd_seq_close(&seq);
}

void MidiSessionALSA::scanClients(PluggingController& controller,
		EventDistributor& eventDistributor, Scheduler& scheduler)
{
	// Iterate through all clients.
	snd_seq_client_info_t* cinfo;
	snd_seq_client_info_alloca(&cinfo);
	snd_seq_client_info_set_client(cinfo, -1);
	while (snd_seq_query_next_client(&seq, cinfo) >= 0) {
		int client = snd_seq_client_info_get_client(cinfo);
		if (client == SND_SEQ_CLIENT_SYSTEM) {
			continue;
		}

		// TODO: When there is more than one usable port per client,
		//       register them as separate pluggables.
		snd_seq_port_info_t* pinfo;
		snd_seq_port_info_alloca(&pinfo);
		snd_seq_port_info_set_client(pinfo, client);
		snd_seq_port_info_set_port(pinfo, -1);
		while (snd_seq_query_next_port(&seq, pinfo) >= 0) {
			unsigned int type = snd_seq_port_info_get_type(pinfo);
			if (!(type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)) {
				continue;
			}
			constexpr unsigned int wrcaps =
					SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
			if ((snd_seq_port_info_get_capability(pinfo) & wrcaps) == wrcaps) {
				controller.registerPluggable(std::make_unique<MidiOutALSA>(
						seq, *cinfo, *pinfo
						));
			}
			constexpr unsigned int rdcaps =
					SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
			if ((snd_seq_port_info_get_capability(pinfo) & rdcaps) == rdcaps) {
				controller.registerPluggable(std::make_unique<MidiInALSA>(
						eventDistributor, scheduler, seq, *cinfo, *pinfo
						));
			}
		}
	}
}

} // namespace openmsx
